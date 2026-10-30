// ArkeyanLauncherTest's checks for the launcher shell: the install checklist, game runs and logs, the updater, settings.
// Called from launcher_test.cpp's main; returns the number of failures.
#include "launcher/game_install.h"
#include "launcher/game_run.h"
#include "launcher/install_checklist.h"
#include "launcher/launcher.h"
#include "launcher/report.h"
#include "launcher/updater.h"
#include "shared/collection/save_backup.h"
#include "shared/dir_list.h"
#include "shared/mini_json.h"
#include "test_env.h"
#include <miniz.h>
#include <SDL3/SDL.h>
#include <cstdio>
#if !defined(_WIN32)
#include <unistd.h>
#endif
#include <filesystem>
#include <map>
#include <optional>
#include <fstream>
#include <string>

namespace
{
    int g_shellFailures = 0;

#define SHELL_EXPECT(condition) \
    do { if (!(condition)) { fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition); g_shellFailures++; } } while (0)

    InstallProgress Feed(std::initializer_list<std::pair<const char*, uint64_t>> lines)
    {
        InstallProgress progress;
        progress.state = InstallProgress::State::Running;
        for (const auto& [line, at] : lines)
            progress.Apply(line, at);
        return progress;
    }

    void ChecklistChecks(const std::filesystem::path& scratch)
    {
        using RowState = InstallChecklist::RowState;
        // Nothing yet: eight waiting rows, the first one current.
        InstallChecklist empty = MakeInstallChecklist(Feed({}), 1000);
        SHELL_EXPECT(empty.rows.size() == 8 && empty.current == 0 && empty.overall == 0 && empty.etaMs == -1);
        SHELL_EXPECT(empty.rows.front().title == "Preparing the compiler" && empty.rows.back().id == "finish");
        SHELL_EXPECT(empty.action == "Starting the installer");

        // Two steps done, the third running at a quarter: ticks with times, a spinner, a bar, a time left.
        InstallProgress progress = Feed({ { "@step toolchain Preparing the compiler", 1000 }, { "@log downloading llvm-mingw", 1500 },
            { "@step disc Reading the disc", 61000 }, { "@step recompile Translating the game code", 91000 }, { "@progress 10 40", 100000 },
            { "@log ppc_recomp.12.cpp", 120000 } });
        InstallChecklist list = MakeInstallChecklist(progress, 121000);
        SHELL_EXPECT(list.current == 2 && list.rows[2].state == RowState::Running && list.rows[2].fraction == 0.25f);
        SHELL_EXPECT(list.rows[0].state == RowState::Done && list.rows[0].ms == 60000 && list.rows[1].ms == 30000);
        SHELL_EXPECT(list.rows[3].state == RowState::Waiting && list.rows[7].state == RowState::Waiting);
        SHELL_EXPECT(list.elapsedMs == 120000 && list.action == "ppc_recomp.12.cpp");
        SHELL_EXPECT(list.overall > 0.08f && list.overall < 0.12f); // (8 + 2 + 6/4) of 104
        // The current step: 30 s for a quarter, 90 s left of it; the rest at the measured pace (90 s for 6+2+2.5 weight).
        SHELL_EXPECT(list.etaMs > 90000 + 500000 && list.etaMs < 90000 + 900000);
        SHELL_EXPECT(InstallTimeLeft(list.etaMs).starts_with("about ") && InstallTimeLeft(30000) == "less than a minute left");
        SHELL_EXPECT(InstallTimeLeft(-1).empty() && InstallTimeLeft(3 * 3600000 + 5 * 60000) == "about 3 h 05 min left");
        SHELL_EXPECT(InstallDuration(187000) == "3:07" && InstallDuration(3723000) == "1:02:03");

        // A new step: its bar starts again, the time left waits for its progress.
        progress.Apply("@step shaders Translating the shaders", 130000);
        list = MakeInstallChecklist(progress, 131000);
        SHELL_EXPECT(list.current == 3 && list.rows[3].fraction < 0 && list.etaMs == -1 && list.action == "Working");

        // After the movie shaders, the second pass is "Finishing", whose time adds up.
        for (const char* line : { "@step compile Compiling the game", "@step link Linking the game", "@step movies Adding the movie shaders",
                 "@step shaders Translating the shaders", "@step compile Compiling the game" })
            progress.Apply(line, 200000);
        progress.Apply("@progress 1 4", 201000);
        list = MakeInstallChecklist(progress, 205000);
        SHELL_EXPECT(list.current == 7 && list.rows[7].state == RowState::Running && list.rows[6].state == RowState::Done);
        SHELL_EXPECT(list.rows[3].state == RowState::Done && list.rows[3].ms == 70000);
        progress.Apply("@done /games/x/SSARecomp", 206000);
        progress.state = InstallProgress::State::Done;
        list = MakeInstallChecklist(progress, 206000);
        SHELL_EXPECT(list.overall == 1.0f && list.current == -1);
        for (const InstallChecklist::Row& row : list.rows)
            SHELL_EXPECT(row.state == RowState::Done);

        // A failure: the row is marked, the message is for people.
        InstallProgress failed = Feed({ { "@step toolchain Preparing the compiler", 1000 }, { "@error download failed: curl exit 6", 2000 } });
        failed.state = InstallProgress::State::Failed;
        list = MakeInstallChecklist(failed, 3000);
        SHELL_EXPECT(list.rows[0].state == RowState::Failed && list.current == 0);
        SHELL_EXPECT(InstallFailureHeadline(list, failed).find("Couldn't download") != std::string::npos);
        failed = Feed({ { "@step compile Compiling the game", 1000 }, { "@error 3 files didn't compile", 2000 } });
        failed.state = InstallProgress::State::Failed;
        list = MakeInstallChecklist(failed, 3000);
        SHELL_EXPECT(list.rows[0].state == RowState::Done && list.rows[4].state == RowState::Failed);
        SHELL_EXPECT(InstallFailureHeadline(list, failed) == "Something went wrong while compiling the game.");

        // An unknown step gets its own row, before "Finishing".
        list = MakeInstallChecklist(Feed({ { "@step toolchain Preparing the compiler", 1 }, { "@step pack Packing the textures", 2 } }), 3);
        SHELL_EXPECT(list.rows.size() == 9 && list.rows[7].id == "pack" && list.rows[7].title == "Packing the textures" && list.current == 7);

        // Read back from install.log after a restart: the steps without their times, Interrupted.
        std::filesystem::create_directories(scratch / "interrupted");
        std::ofstream(scratch / "interrupted" / "install.log") << "@step toolchain Preparing the compiler\n@log x\n"
                                                                     "@step disc Reading the disc\n@progress 2 4\n";
        InstallProgress past = ReadInstallLog(scratch / "interrupted");
        SHELL_EXPECT(past.state == InstallProgress::State::Interrupted && past.steps.size() == 2);
        list = MakeInstallChecklist(past, 99999);
        SHELL_EXPECT(list.current == 1 && list.rows[1].state == RowState::Stopped && list.rows[0].state == RowState::Done);
        SHELL_EXPECT(!list.rows[0].timed && list.elapsedMs == 0 && list.etaMs == -1);
        std::ofstream(scratch / "interrupted" / "install.log", std::ios::app) << "@error disc image unreadable\n";
        SHELL_EXPECT(ReadInstallLog(scratch / "interrupted").state == InstallProgress::State::Failed);
        SHELL_EXPECT(ReadInstallLog(scratch / "nowhere").state == InstallProgress::State::Idle);
        printf("Install checklist: steps, finish pass, times, failures and read-back checked\n");
    }
}

namespace
{
    void GameRunChecks(const std::filesystem::path& scratch)
    {
        PortalProbeResult none = ParsePortalProbe("{\"done\":true,\"count\":0}\n");
        SHELL_EXPECT(none.ran && none.portals.empty() && none.error.empty());
        PortalProbeResult found = ParsePortalProbe("log noise\r\n{\"kind\":\"hid\",\"vid\":\"1430\",\"pid\":\"0150\",\"product\":\"Wii \\\"Portal\\\"\",\"opened\":true,"
            "\"status\":\"ok\"}\r\n{\"kind\":\"usb\",\"vid\":\"1430\",\"pid\":\"1f17\",\"name\":\"Xbox\",\"opened\":false,\"status\":\"error: access denied\"}\n"
            "{\"done\":true,\"count\":2}");
        SHELL_EXPECT(found.ran && found.portals.size() == 2);
        SHELL_EXPECT(found.portals[0].product == "Wii \"Portal\"" && found.portals[0].opened && found.portals[0].vid == "1430");
        SHELL_EXPECT(found.portals[1].product == "Xbox" && !found.portals[1].opened && found.portals[1].status == "error: access denied");
        // A runtime that names the model: the id, the model, the verdict.
        PortalProbeResult named = ParsePortalProbe("{\"kind\":\"usb\",\"vid\":\"1430\",\"pid\":\"1f17\",\"product\":\"Xbox 360 portal\",\"opened\":true,\"status\":\"ok\","
            "\"id\":\"01 28\",\"model\":\"Spyro's Adventure portal (Xbox 360)\",\"verdict\":\"works\",\"verdict_reason\":\"\"}\n"
            "{\"kind\":\"hid\",\"vid\":\"1430\",\"pid\":\"0150\",\"product\":\"\",\"opened\":true,\"status\":\"ok\",\"id\":\"00 90\",\"model\":\"unknown portal\","
            "\"verdict\":\"rejected\",\"verdict_reason\":\"too old\"}\n{\"done\":true,\"count\":2}\n");
        SHELL_EXPECT(named.ran && named.portals.size() == 2);
        SHELL_EXPECT(named.portals[0].id == "01 28" && named.portals[0].model == "Spyro's Adventure portal (Xbox 360)" && named.portals[0].verdict == "works");
        SHELL_EXPECT(named.portals[1].verdict == "rejected" && named.portals[1].verdictReason == "too old");
        SHELL_EXPECT(found.portals[0].id.empty() && found.portals[0].verdict.empty()); // an older runtime names nothing
        PortalProbeResult transport = ParsePortalProbe("{\"transport\":\"usb\",\"available\":false,\"status\":\"error: libusb-1.0 not found\"}\n{\"done\":true,\"count\":0}\n");
        SHELL_EXPECT(transport.ran && transport.portals.empty() && transport.notes.size() == 1 && transport.notes[0].find("libusb-1.0 not found") != std::string::npos);
        SHELL_EXPECT(!ParsePortalProbe("{\"kind\":\"hid\"").ran && !ParsePortalProbe("").ran);

#if !defined(_WIN32)
        // A stand-in runtime: a shell script that prints, then exits with a code, or answers --portal-probe.
        std::filesystem::path logs = scratch / "logs";
        std::filesystem::create_directories(logs);
        TestSetEnv("SKY_LAUNCHER_LOG_DIR", logs.string().c_str());
        std::filesystem::path script = scratch / "fake_runtime.sh";
        std::ofstream(script) << "#!/bin/sh\nif [ \"$1\" = --portal-probe ]; then echo '{\"kind\":\"hid\",\"vid\":\"1430\",\"pid\":\"0150\","
            "\"product\":\"Fake\",\"opened\":true,\"status\":\"ok\"}'; echo '{\"done\":true,\"count\":1}'; exit 0; fi\n"
            "echo out line; echo err line >&2; exit 3\n";
        std::filesystem::permissions(script, std::filesystem::perms::owner_all);
        GameRunResult run = RunGame(script.string(), "disc.iso");
        SHELL_EXPECT(run.started && run.exitCode == 3);
        std::string capture = ReadLogTail(logs / CaptureLogName);
        SHELL_EXPECT(capture.find("out line") != std::string::npos && capture.find("err line") != std::string::npos);
        SHELL_EXPECT(capture.find("exit code 3") != std::string::npos);
        RunGame(script.string(), "disc.iso");
        SHELL_EXPECT(std::filesystem::exists(logs / "skylauncher-game.prev.log"));
        std::ofstream(logs / RuntimeLogName) << "[settings] scale 1\n";
        std::string text = GameLogText();
        SHELL_EXPECT(text.find("===== SSARecomp.log =====") != std::string::npos && text.find("[settings] scale 1") != std::string::npos);
        SHELL_EXPECT(text.find("===== skylauncher-game.log =====") != std::string::npos);
        PortalProbeResult probe = RunPortalProbe(script.string());
        SHELL_EXPECT(probe.ran && probe.portals.size() == 1 && probe.portals[0].product == "Fake");
        SHELL_EXPECT(!RunGame((scratch / "nowhere").string(), "x").started);
        SHELL_EXPECT(!RunPortalProbe((scratch / "nowhere").string()).ran);
        // The tail starts on a whole line.
        std::ofstream(logs / "big.log") << "aaaa\nbbbb\ncccc\n";
        SHELL_EXPECT(ReadLogTail(logs / "big.log", 7) == "cccc\n" && ReadLogTail(logs / "big.log", 100) == "aaaa\nbbbb\ncccc\n");
        TestUnsetEnv("SKY_LAUNCHER_LOG_DIR");
#endif
        printf("Game run: capture of stdout and stderr, exit code, logs text and portal probe checked\n");
    }

    // One game at a time (GameSession): a game started here is watched to its exit (its code: a crash); a second start, of it or
    // of another game, is refused while it runs; a game found by its marker (one started by hand) counts too; and one left
    // running when the launcher closed is found by the next launcher by the marker the game keeps (save_backup.h).
    void GameSessionChecks(const std::filesystem::path& scratch)
    {
#if !defined(_WIN32)
        namespace fs = std::filesystem;
        const fs::path root = scratch / "session";
        fs::create_directories(root / "logs");
        TestSetEnv("SKY_LAUNCHER_LOG_DIR", (root / "logs").string().c_str());
        auto script = [&](const char* name, const std::string& body) {
            const fs::path path = root / name;
            std::ofstream(path) << "#!/bin/sh\n" << body;
            fs::permissions(path, fs::perms::owner_all);
            return path.string();
        };
        // Polls as the window does until the game stops (at most 10 s).
        auto waitEnd = [](GameSession& session) {
            for (const uint64_t start = SDL_GetTicks(); SDL_GetTicks() - start < 10000; SDL_Delay(20))
                if (std::optional<GameSession::Ended> ended = session.Poll(SDL_GetTicks()))
                    return ended;
            return std::optional<GameSession::Ended>();
        };
        std::map<std::string, bool> marked; // the games' markers, as this test sets them
        GameSession session({ "ssa", "giants" }, [&](const std::string& game) { return marked[game]; });
        GameLaunch launch;
        launch.runtime = script("quits.sh", "echo \"language $SKY_LANGUAGE\"\nsleep 1\nexit 0\n");
        launch.variables = { { "SKY_LANGUAGE", "fr" } };
        std::string error;
        SHELL_EXPECT(session.Running().empty() && session.Start("ssa", launch, "disc.iso", error));
        SHELL_EXPECT(session.Running() == "ssa" && session.Ours());
        SHELL_EXPECT(!session.Start("ssa", launch, "disc.iso", error) && error == "Spyro's Adventure is running"); // a double click
        SHELL_EXPECT(!session.Start("giants", launch, "disc.iso", error) && session.Running() == "ssa");        // another game
        std::optional<GameSession::Ended> ended = waitEnd(session);
        SHELL_EXPECT(ended && ended->game == "ssa" && ended->ours && ended->exitCode == 0);
        SHELL_EXPECT(session.Running().empty() && !session.Ours());
        const std::string capture = ReadLogTail(root / "logs" / CaptureLogName);
        SHELL_EXPECT(capture.find("language fr") != std::string::npos && capture.find("# exit code 0") != std::string::npos);
        // A crash: its exit code, and the next game can start.
        launch.runtime = script("crashes.sh", "exit 7\n");
        SHELL_EXPECT(session.Start("giants", launch, "disc.iso", error));
        ended = waitEnd(session);
        SHELL_EXPECT(ended && ended->game == "giants" && ended->ours && ended->exitCode == 7 && session.Running().empty());
        // A game found by its marker: nothing starts until it is gone.
        marked["giants"] = true;
        SHELL_EXPECT(!session.Poll(SDL_GetTicks()) && session.Running() == "giants" && !session.Ours());
        SHELL_EXPECT(!session.Start("ssa", launch, "disc.iso", error) && error == "Giants is running");
        marked["giants"] = false;
        ended = session.Poll(SDL_GetTicks() + 2 * GameSession::MarkerMs);
        SHELL_EXPECT(ended && ended->game == "giants" && !ended->ours && session.Running().empty());
        // Started by hand a moment ago (the next look at the markers not due yet): Start sees it at once.
        marked["ssa"] = true;
        SHELL_EXPECT(!session.Start("giants", launch, "disc.iso", error) && session.Running() == "ssa");
        marked["ssa"] = false;
        ended = session.Poll(SDL_GetTicks() + 4 * GameSession::MarkerMs);
        SHELL_EXPECT(ended && session.Running().empty());
        // A runtime that isn't there: refused, nothing runs.
        launch.runtime = (root / "nowhere").string();
        SHELL_EXPECT(!session.Start("ssa", launch, "disc.iso", error) && session.Running().empty() && !session.Ours());

        // The launcher closed while its game runs, then opened again: the game runs on, and the next launcher finds it by its
        // marker (written and removed by the stand-in as the runtime does), and starts nothing until it is gone.
        const fs::path saves = root / "saves", lock = savebackup::LockFile(saves);
        launch.runtime = script("stays.sh", "mkdir -p '" + lock.parent_path().string() + "'\necho $$ > '" + lock.string() +
            "'\nsleep 2\nrm -f '" + lock.string() + "'\n");
        auto marker = [&](const std::string&) { return savebackup::GameRunning(saves); };
        {
            GameSession first({ "ssa" }, marker);
            SHELL_EXPECT(first.Start("ssa", launch, "disc.iso", error));
            for (const uint64_t start = SDL_GetTicks(); !savebackup::GameRunning(saves) && SDL_GetTicks() - start < 5000;)
                SDL_Delay(20);
        }
        GameSession second({ "ssa" }, marker);
        SHELL_EXPECT(!second.Poll(SDL_GetTicks()) && second.Running() == "ssa" && !second.Ours());
        SHELL_EXPECT(!second.Start("ssa", launch, "disc.iso", error));
        ended = waitEnd(second);
        SHELL_EXPECT(ended && ended->game == "ssa" && !ended->ours && second.Running().empty());
        TestUnsetEnv("SKY_LAUNCHER_LOG_DIR");
        printf("One game at a time: start, refusals, exit codes, markers and a launcher restarted while its game runs checked\n");
#else
        (void)scratch;
#endif
    }
}

namespace
{
    void WriteBytes(const std::filesystem::path& file, const std::string& data)
    {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << data;
    }

    std::string ReadBytes(const std::filesystem::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), {});
    }

    std::string Sha(const std::string& data) { return Sha256Hex({ reinterpret_cast<const uint8_t*>(data.data()), data.size() }); }

    std::string FileUrl(const std::filesystem::path& file) { return TestFileUrl(file); }

    // A fake release in `folder`: latest.json, SHA256SUMS and the assets given as name -> content. `sumsFor` lists the hashes.
    void MakeRelease(const std::filesystem::path& folder, const std::string& tag, const std::map<std::string, std::string>& assets,
        const std::map<std::string, std::string>& hashes, bool withSums = true, const std::string& notes = "What\xE2\x80\x99s new\n- a thing")
    {
        std::string body;
        for (char c : notes)
            body += c == '\n' ? std::string("\\n") : c == '"' ? std::string("\\\"") : std::string(1, c);
        std::filesystem::create_directories(folder);
        std::string json = "{\"tag_name\":\"" + tag + "\",\"name\":\"Arkeyan Launcher " + tag + "\",\"body\":\"" + body + "\",\"html_url\":\"https://example.test/r\",\"prerelease\":false,\"assets\":[";
        std::string sums;
        bool first = true;
        for (const auto& [name, content] : assets)
        {
            WriteBytes(folder / name, content);
            json += std::string(first ? "" : ",") + "{\"name\":\"" + name + "\",\"size\":" + std::to_string(content.size()) + ",\"browser_download_url\":\"" +
                FileUrl(folder / name) + "\"}";
            first = false;
            sums += (hashes.count(name) ? hashes.at(name) : Sha(content)) + "  " + name + "\n";
        }
        if (withSums)
        {
            WriteBytes(folder / "SHA256SUMS", sums);
            json += ",{\"name\":\"SHA256SUMS\",\"size\":" + std::to_string(sums.size()) + ",\"browser_download_url\":\"" + FileUrl(folder / "SHA256SUMS") + "\"}";
        }
        json += "]}";
        WriteBytes(folder / "latest.json", json);
    }

    std::string MakeZip(const std::filesystem::path& file, const std::vector<std::pair<std::string, std::string>>& entries)
    {
        std::filesystem::remove(file);
        std::filesystem::create_directories(file.parent_path());
        for (const auto& [name, content] : entries)
            mz_zip_add_mem_to_archive_file_in_place(file.string().c_str(), name.c_str(), content.data(), content.size(), nullptr, 0, 6);
        return ReadBytes(file);
    }

    void WaitFor(UpdateManager& manager)
    {
        while (manager.Busy())
            SDL_Delay(20);
    }

    void ParseGamesLineChecks()
    {
        std::vector<GameUpdate> games;
        ParseGamesLine("Fixes.\n\n<!-- games: ssa=0.9.1 giants=0.8.1 swapforce=0.5.0 -->\n", "v1.2.3", games);
        SHELL_EXPECT(games.size() == 3 && games[0].id == "ssa" && games[0].version == "0.9.1" && games[2].id == "swapforce" && games[2].kit == "v1.2.3");
        ParseGamesLine("<!-- other --> <!--games:ssa=1.0.0 Bad=1.0.0 g=x h=1.0 =2.0.0 y=1.0.0-beta k=-->", "v1", games);
        SHELL_EXPECT(games.size() == 2 && games[0].id == "ssa" && games[1].id == "h"); // bad ids, versions and empty entries are skipped
        ParseGamesLine("no line here, <!-- a comment --> <!-- games: ssa=1.0.0", "v1", games);
        SHELL_EXPECT(games.empty()); // none, or never closed
        SHELL_EXPECT(StripHtmlComments("a<!-- x -->b\n<!-- games: ssa=1.0.0 -->\nc<!-- open") == "ab\n\nc");
    }

    void UpdaterChecks(const std::filesystem::path& scratch)
    {
        // Versions.
        SHELL_EXPECT(ParseSemVer("v1.2.3").valid && ParseSemVer("v1.2.3").minor == 2 && ParseSemVer("1.2").patch == 0);
        SHELL_EXPECT(!ParseSemVer("latest").valid && !ParseSemVer("").valid);
        SHELL_EXPECT(ParseSemVer("kit-2.10.1-beta.2+build5").major == 2 && ParseSemVer("kit-2.10.1-beta.2+build5").pre == "beta.2");
        SHELL_EXPECT(IsNewer("v0.10.0", "0.9.9") && !IsNewer("v0.9.9", "0.10.0") && !IsNewer("v1.0.0", "1.0.0"));
        SHELL_EXPECT(IsNewer("v1.0.0", "1.0.0-rc.1") && !IsNewer("v1.0.0-rc.1", "1.0.0") && !IsNewer("nonsense", "1.0.0"));
        SHELL_EXPECT(ParseSemVer(LauncherVersion()).valid);

        // JSON and the release answer.
        JsonValue value;
        SHELL_EXPECT(ParseJson("{\"a\":[1,2.5e1,\"x\\u00e9\\ud83d\\ude00\",true,null],\"b\":{}}", value) && value["a"][1].number == 25 &&
            value["a"][2].Str() == "x\xC3\xA9\xF0\x9F\x98\x80" && value["a"][3].boolean && value["a"][9]["x"].Str().empty());
        std::string error;
        SHELL_EXPECT(!ParseJson("{\"a\":", value, &error) && !error.empty());
        SHELL_EXPECT(!ParseJson("[1] 2", value) && !ParseJson(std::string(200, '['), value));
        ReleaseInfo release;
        SHELL_EXPECT(!ParseRelease("<html>rate limited</html>", release, &error) && error.find("JSON") != std::string::npos);
        SHELL_EXPECT(!ParseRelease("{\"message\":\"Not Found\"}", release, &error) && error.find("Not Found") != std::string::npos);
        std::filesystem::path updates = scratch / "updates";
        MakeRelease(updates / "r1", "v99.0.0", { { "ArkeyanLauncher.exe", "exe bytes" }, { "skylanders-kit-test.zip", "zip" } }, {});
        SHELL_EXPECT(ParseRelease(ReadBytes(updates / "r1" / "latest.json"), release) && release.tag == "v99.0.0");
        SHELL_EXPECT(release.notes.find("What\xE2\x80\x99s new") == 0 && release.assets.size() == 3 && FindAsset(release, "ArkeyanLauncher.exe")->size == 9);
        std::string sums = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  a b.txt\n"
                           "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855 *empty.bin\r\nshort  x\n";
        SHELL_EXPECT(FindChecksum(sums, "a b.txt").starts_with("ba7816bf") && FindChecksum(sums, "empty.bin").starts_with("e3b0c442"));
        SHELL_EXPECT(FindChecksum(sums, "x").empty() && FindChecksum(sums, "nothing").empty());
        SHELL_EXPECT(LauncherAssetName().starts_with("ArkeyanLauncher") && KitAssetName().starts_with("skylanders-kit-"));

        // Downloads: good, damaged, and a release without SHA256SUMS.
        std::string downloadError;
        auto file = DownloadVerified(release, "ArkeyanLauncher.exe", updates / "got", {}, nullptr, downloadError);
        SHELL_EXPECT(!file.empty() && ReadBytes(file) == "exe bytes" && !std::filesystem::exists(updates / "got" / "SHA256SUMS"));
        MakeRelease(updates / "r2", "v99.0.0", { { "ArkeyanLauncher.exe", "exe bytes" } }, { { "ArkeyanLauncher.exe", std::string(64, 'a') } });
        ParseRelease(ReadBytes(updates / "r2" / "latest.json"), release);
        file = DownloadVerified(release, "ArkeyanLauncher.exe", updates / "got2", {}, nullptr, downloadError);
        SHELL_EXPECT(file.empty() && downloadError.find("damaged") != std::string::npos && !std::filesystem::exists(updates / "got2" / "ArkeyanLauncher.exe"));
        MakeRelease(updates / "r3", "v99.0.0", { { "ArkeyanLauncher.exe", "exe bytes" } }, {}, false);
        ParseRelease(ReadBytes(updates / "r3" / "latest.json"), release);
        file = DownloadVerified(release, "ArkeyanLauncher.exe", updates / "got3", {}, nullptr, downloadError);
        SHELL_EXPECT(file.empty() && downloadError.find("can't be verified") != std::string::npos);
        file = DownloadVerified(release, "Other.exe", updates / "got3", {}, nullptr, downloadError);
        SHELL_EXPECT(file.empty() && downloadError.find("Other.exe") != std::string::npos);
        HttpResult missing = HttpDownload(FileUrl(updates / "nowhere.bin"), updates / "got4" / "x");
        SHELL_EXPECT(!missing.ok && !missing.error.empty() && !std::filesystem::exists(updates / "got4" / "x.part"));
        SHELL_EXPECT(!HttpDownload("http://example.test/x", updates / "got4" / "y").ok); // never plain http

        // Swapping: the new one in place, the old one kept as .old, undone when the new one is missing.
        std::filesystem::path app = updates / "swap" / "ArkeyanLauncher.exe";
        WriteBytes(app, "old");
        WriteBytes(updates / "swap" / "new", "new");
        SHELL_EXPECT(ReplaceInPlace(app, updates / "swap" / "new", error) && ReadBytes(app) == "new" && ReadBytes(app.string() + ".old") == "old");
        SHELL_EXPECT(!ReplaceInPlace(app, updates / "swap" / "absent", error) && ReadBytes(app) == "new" && !error.empty());
        CleanupOldUpdate(app);
        SHELL_EXPECT(!std::filesystem::exists(app.string() + ".old") && ReadBytes(app) == "new");

        // Zips: unpacking, a path leaving its folder, and the kit swapped in without losing the old one on failure.
        std::filesystem::path zip = updates / "kit.zip";
        MakeZip(zip, { { "ssa-kit-test/", "" }, { "ssa-kit-test/SSABuild", "binary" }, { "ssa-kit-test/SSABuild.exe", "binary" }, { "ssa-kit-test/res/a.txt", "a" } });
        std::filesystem::path kitFolder = updates / "game" / "ssa-kit";
        WriteBytes(kitFolder / "SSABuild", "old kit");
        WriteBytes(kitFolder / "SSABuild.exe", "old kit");
        SHELL_EXPECT(InstallKitZip(zip, kitFolder, error) && ReadBytes(kitFolder / "SSABuild") == "binary" && ReadBytes(kitFolder / "res" / "a.txt") == "a");
        SHELL_EXPECT(!std::filesystem::exists(kitFolder.string() + ".old") && !std::filesystem::exists(kitFolder.string() + ".new"));
        MakeZip(zip, { { "readme.txt", "no ssabuild here" } });
        SHELL_EXPECT(!InstallKitZip(zip, kitFolder, error) && error.find("kit") != std::string::npos && ReadBytes(kitFolder / "SSABuild") == "binary");
        MakeZip(zip, { { "../evil.txt", "x" } });
        SHELL_EXPECT(!ExtractZip(zip, updates / "unz", error) && error.find("damaged") != std::string::npos && !std::filesystem::exists(updates / "evil.txt"));
        SHELL_EXPECT(!ExtractZip(updates / "r1" / "latest.json", updates / "unz2", error));
        MakeZip(zip, { { "SSABuild", "first install" }, { "SSABuild.exe", "first install" } });
        SHELL_EXPECT(InstallKitZip(zip, updates / "fresh" / "ssa-kit", error) && ReadBytes(updates / "fresh" / "ssa-kit" / "SSABuild") == "first install");

        // The kit zip: SkyBuild at the root, or SSABuild (a kit for 0.4.x launchers), wrapped or not.
        {
            MakeZip(zip, { { "skylanders-kit-test/SkyBuild", "new builder" }, { "skylanders-kit-test/SkyBuild.exe", "new builder" } });
            SHELL_EXPECT(InstallKitZip(zip, updates / "fresh" / "skybuild-kit", error) && ReadBytes(updates / "fresh" / "skybuild-kit" / "SkyBuild") == "new builder");
            MakeZip(zip, { { "SkyBuild", "root" }, { "SkyBuild.exe", "root" }, { "SSABuild", "copy" }, { "SSABuild.exe", "copy" } });
            SHELL_EXPECT(InstallKitZip(zip, updates / "fresh" / "both-kit", error) && ReadBytes(updates / "fresh" / "both-kit" / "SSABuild") == "copy");
            // The kit folder is found with either builder and either folder name.
            std::filesystem::path kitSkyBuild = updates / "kits" / "skylanders-kit-test";
#if defined(_WIN32)
            WriteBytes(kitSkyBuild / "SkyBuild.exe", "x");
#else
            WriteBytes(kitSkyBuild / "SkyBuild", "x");
#endif
            auto kit = InstallKitIn(kitSkyBuild);
            SHELL_EXPECT(kit && kit->ssabuild.filename().string().starts_with("SkyBuild"));
            auto oldKit = InstallKitIn(updates / "game" / "ssa-kit");
            SHELL_EXPECT(oldKit && oldKit->ssabuild.filename().string().starts_with("SSABuild"));
        }

        // The manager: a check against the fake latest.json, then the kit update from it.
        TestSetEnv("SKY_LAUNCHER_UPDATE_URL", FileUrl(updates / "r1" / "latest.json").c_str());
        UpdateManager manager;
        UpdateManager::Settings settings;
        manager.Check(settings);
        WaitFor(manager);
        UpdateManager::State state = manager.Snapshot();
        SHELL_EXPECT(state.checked && state.launcherNewer && state.launcher.tag == "v99.0.0" && state.games.empty() && state.phase == UpdateManager::Phase::Idle);
        MakeRelease(updates / "r4", "v0.0.1", { { "ArkeyanLauncher.exe", "x" } }, {});
        TestSetEnv("SKY_LAUNCHER_UPDATE_URL", FileUrl(updates / "r4" / "latest.json").c_str());
        manager.Check(settings);
        WaitFor(manager);
        SHELL_EXPECT(manager.Snapshot().checked && !manager.Snapshot().launcherNewer);
        TestSetEnv("SKY_LAUNCHER_UPDATE_URL", FileUrl(updates / "none.json").c_str());
        manager.Check(settings);
        WaitFor(manager);
        SHELL_EXPECT(!manager.Snapshot().checked && !manager.Snapshot().checkError.empty());

        // Game updates: the hidden line of a release's notes lists each game's version (here Giants alone is ahead of what is built);
        // a body without the line: no game updates and no complaint.
        ParseGamesLineChecks();
        const std::string withLine = "Notes.\n\n<!-- games: ssa=0.9.0 giants=0.8.2 x=? -->";
        MakeRelease(updates / "r5", "v2.0.0", { { "ArkeyanLauncher.exe", "x" } }, {}, true, withLine);
        TestSetEnv("SKY_LAUNCHER_UPDATE_URL", FileUrl(updates / "r5" / "latest.json").c_str());
        settings.kitRepo = "someone/kit";
        manager.Check(settings);
        WaitFor(manager);
        state = manager.Snapshot();
        SHELL_EXPECT(state.checked && state.games.size() == 2 && state.games[1].id == "giants" && state.games[1].version == "0.8.2" &&
            state.games[1].kit == "v2.0.0");
        MakeRelease(updates / "r5c", "v2.0.0", { { "ArkeyanLauncher.exe", "x" } }, {});
        TestSetEnv("SKY_LAUNCHER_UPDATE_URL", FileUrl(updates / "r5c" / "latest.json").c_str());
        manager.Check(settings);
        WaitFor(manager);
        SHELL_EXPECT(manager.Snapshot().checked && manager.Snapshot().games.empty()); // a body without the line
        // The kit download INSTALL starts with: the release of this launcher's tag, else the latest, verified, then found.
        auto setEnv = [](const char* name, const std::string& value) { TestSetEnv(name, value.c_str()); };
        TestUnsetEnv("SKY_LAUNCHER_KIT");
        setEnv("SKY_LAUNCHER_DATA", (updates / "data").string());
        std::string launcherTag = "v" + LauncherVersion();
        std::string dlZip = MakeZip(updates / "dl" / KitAssetName(), { { "ssa-kit/", "" }, { "ssa-kit/SSABuild", "dl kit" }, { "ssa-kit/SSABuild.exe", "dl kit" }, { "ssa-kit/kit.txt", "build=b9\n" } });
        MakeRelease(updates / "dl", launcherTag, { { KitAssetName(), dlZip } }, {});
        std::filesystem::create_directories(updates / "dl-other");
        std::filesystem::copy_file(updates / "dl" / "latest.json", updates / "dl-other" / (launcherTag + ".json"));
        MakeRelease(updates / "dl-other", "v77.0.0", { { KitAssetName(), "wrong zip" } }, {}); // the latest: must not be used
        setEnv("SKY_LAUNCHER_UPDATE_URL", FileUrl(updates / "dl-other" / "latest.json"));
        std::string kitError, kitTag;
        uint64_t lastBytes = 0;
        std::filesystem::path kitDir = DownloadKit("x/y", launcherTag, DownloadedKitsFolder(), [&](uint64_t bytes, uint64_t) { lastBytes = bytes; }, nullptr, kitError, &kitTag);
        SHELL_EXPECT(!kitDir.empty() && kitDir == DownloadedKitsFolder() / launcherTag && kitTag == launcherTag && lastBytes == dlZip.size());
        SHELL_EXPECT(ReadBytes(kitDir / "SSABuild") == "dl kit" && !std::filesystem::exists(DownloadedKitsFolder() / ".download"));
        auto found = FindInstallKit({});
        SHELL_EXPECT(found && found->folder == kitDir && found->version == "b9");
        // A newer downloaded kit wins, whatever the folder dates are.
        WriteBytes(DownloadedKitsFolder() / "v0.10.0" / "SSABuild", "newer");
        WriteBytes(DownloadedKitsFolder() / "v0.10.0" / "SSABuild.exe", "newer");
        found = FindInstallKit({});
        SHELL_EXPECT(found && found->folder == DownloadedKitsFolder() / "v0.10.0");
        std::filesystem::remove_all(DownloadedKitsFolder() / "v0.10.0");
        // No release of that tag: the latest.
        std::string latestZip = MakeZip(updates / "dl-latest" / KitAssetName(), { { "SSABuild", "latest kit" }, { "SSABuild.exe", "latest kit" } });
        MakeRelease(updates / "dl-latest", "v77.0.0", { { KitAssetName(), latestZip } }, {});
        setEnv("SKY_LAUNCHER_UPDATE_URL", FileUrl(updates / "dl-latest" / "latest.json"));
        kitDir = DownloadKit("x/y", launcherTag, updates / "data2" / "kit", {}, nullptr, kitError);
        SHELL_EXPECT(kitDir == updates / "data2" / "kit" / "v77.0.0" && ReadBytes(kitDir / "SSABuild") == "latest kit");
        // A bad hash and a SHA256SUMS without the line are refused, and nothing is left behind.
        MakeRelease(updates / "dl-bad", "v78.0.0", { { KitAssetName(), latestZip } }, { { KitAssetName(), std::string(64, 'b') } });
        setEnv("SKY_LAUNCHER_UPDATE_URL", FileUrl(updates / "dl-bad" / "latest.json"));
        kitDir = DownloadKit("x/y", launcherTag, updates / "data3" / "kit", {}, nullptr, kitError);
        SHELL_EXPECT(kitDir.empty() && kitError.find("damaged") != std::string::npos && !std::filesystem::exists(updates / "data3" / "kit" / "v78.0.0"));
        MakeRelease(updates / "dl-line", "v79.0.0", { { KitAssetName(), latestZip }, { "other.bin", "x" } }, {});
        WriteBytes(updates / "dl-line" / "SHA256SUMS", Sha("x") + "  other.bin\n");
        setEnv("SKY_LAUNCHER_UPDATE_URL", FileUrl(updates / "dl-line" / "latest.json"));
        kitDir = DownloadKit("x/y", launcherTag, updates / "data3" / "kit", {}, nullptr, kitError);
        SHELL_EXPECT(kitDir.empty() && kitError.find("can't be verified") != std::string::npos);
        MakeRelease(updates / "dl-noasset", "v80.0.0", { { "ArkeyanLauncher.exe", "x" } }, {});
        setEnv("SKY_LAUNCHER_UPDATE_URL", FileUrl(updates / "dl-noasset" / "latest.json"));
        SHELL_EXPECT(DownloadKit("x/y", launcherTag, updates / "data3" / "kit", {}, nullptr, kitError).empty() && kitError.find(KitAssetName()) != std::string::npos);
        // On a thread, as the card polls it.
        setEnv("SKY_LAUNCHER_UPDATE_URL", FileUrl(updates / "dl-latest" / "latest.json"));
        KitDownloader downloader;
        downloader.Start("x/y", launcherTag, updates / "data4" / "kit");
        for (int i = 0; i < 500 && downloader.Running(); i++)
            SDL_Delay(20);
        KitDownloader::Snapshot_ result = downloader.Snapshot();
        SHELL_EXPECT(result.state == KitDownloader::State::Done && result.tag == "v77.0.0" && std::filesystem::exists(result.folder / "SSABuild"));
        TestUnsetEnv("SKY_LAUNCHER_DATA");
        TestUnsetEnv("SKY_LAUNCHER_UPDATE_URL");
        printf("Updater: versions, release JSON, SHA-256 check, swap with rollback, kit zip and the manager checked\n");
    }

    // Settings > Game data: the game's save backups, listed and restored; a restore is refused while the game runs.
    void SaveBackupChecks(const std::filesystem::path& scratch)
    {
        const std::filesystem::path saves = scratch / "save-backup" / "saves";
        const std::filesystem::path save = saves / "00000002" / "Skylanders - Global Data" / "GLOBAL.DAT";
        savebackup::SetGameRunning(false, saves);
        WriteBytes(save, "the save as the game left it");

        // A backup the game made before overwriting the save.
        savebackup::Backup backup = savebackup::BackupFile(saves, save, "20261025-093000");
        SHELL_EXPECT(backup.ok && backup.copied && backup.stamp == "20261025-093000");
        std::vector<savebackup::Entry> entries = savebackup::List(saves);
        SHELL_EXPECT(entries.size() == 1 && entries.front().date == "2026-10-25" && entries.front().time == "09:30:00");
        SHELL_EXPECT(entries.front().bytes == std::string("the save as the game left it").size());

        // A newer save on top of it; a running game refuses the restore and leaves both alone.
        WriteBytes(save, "the new save");
        savebackup::SetGameRunning(true, saves);
        savebackup::Restore refused = savebackup::RestoreBackup(saves, "20261025-093000");
        SHELL_EXPECT(!refused.ok && refused.error == "the game is running" && ReadBytes(save) == "the new save");
        savebackup::SetGameRunning(false, saves);

        // Restored byte for byte, with the new save kept as a backup of its own.
        savebackup::Restore restored = savebackup::RestoreBackup(saves, "20261025-093000");
        SHELL_EXPECT(restored.ok && restored.files == 1 && ReadBytes(save) == "the save as the game left it");
        SHELL_EXPECT(savebackup::List(saves).size() == 2); // the restore's own safety copy and the restored one
        SHELL_EXPECT(!savebackup::RestoreBackup(saves, "20260101-000000").ok);
        printf("Save backups: listed, refused while the game runs, restored byte for byte\n");
    }
}

namespace
{
    void ReportChecks(const std::filesystem::path& scratch)
    {
        launcher_report::Identity identity;
        identity.userName = "alice";
        identity.computerName = "Alices-MacBook-Pro.local";
        identity.homeFolder = "/Users/alice";
        identity.isoFolder = "/Volumes/My Games/SSA";

        // The three home folders, with both separator spellings on Windows.
        SHELL_EXPECT(launcher_report::Redact("/Users/alice/Library/Application Support/Skylanders", identity) ==
            "~/Library/Application Support/Skylanders");
        SHELL_EXPECT(launcher_report::Redact("/home/alice/.local/share/Skylanders", identity) == "~/.local/share/Skylanders");
        SHELL_EXPECT(launcher_report::Redact("C:\\Users\\alice\\AppData\\Roaming\\Skylanders", identity) ==
            "~\\AppData\\Roaming\\Skylanders");
        SHELL_EXPECT(launcher_report::Redact("C:/Users/alice/AppData/Roaming/Skylanders", identity) ==
            "~/AppData/Roaming/Skylanders");
        SHELL_EXPECT(launcher_report::Redact("C:\\Users\\alice\\AppData", identity) == "~\\AppData");
        // Another account is not touched, and neither is a longer name that starts with ours.
        SHELL_EXPECT(launcher_report::Redact("/Users/alicia/x C:\\Users\\alice2\\x", identity) ==
            "/Users/alicia/x C:\\Users\\alice2\\x");

        // The account name inside other text, whole only.
        SHELL_EXPECT(launcher_report::Redact("Logged in as alice; alicia stayed.", identity) ==
            "Logged in as <user>; alicia stayed.");
        SHELL_EXPECT(launcher_report::Redact("mail alice@example.test", identity) == "mail <user>@example.test");
        SHELL_EXPECT(launcher_report::Redact("alice's files", identity) == "<user>'s files");

        // The computer name, and its short form.
        SHELL_EXPECT(launcher_report::Redact("host Alices-MacBook-Pro.local, name Alices-MacBook-Pro", identity) ==
            "host <computer>, name <computer>");

        // The disc image's folder, before the home folder it may sit in.
        SHELL_EXPECT(launcher_report::Redact("/Volumes/My Games/SSA/disc.iso", identity) == "<iso folder>/disc.iso");
        SHELL_EXPECT(launcher_report::Redact("D:\\Games\\SSA\\disc.iso", identity) == "D:\\Games\\SSA\\disc.iso");
        launcher_report::Identity windows = identity;
        windows.isoFolder = "D:\\Games\\SSA";
        SHELL_EXPECT(launcher_report::Redact("D:\\Games\\SSA\\disc.iso", windows) == "<iso folder>\\disc.iso");
        SHELL_EXPECT(launcher_report::Redact("/Volumes/My Games/SSA2/disc.iso", identity) == "/Volumes/My Games/SSA2/disc.iso");

        // Nothing else changes: a text without the identity is returned as it is, byte for byte.
        std::string plain = "line one\r\n\tspaced  two\n[]{}%$#@! caf\xC3\xA9\n";
        SHELL_EXPECT(launcher_report::Redact(plain, identity) == plain);
        SHELL_EXPECT(launcher_report::Redact("before /Users/alice after", identity) == "before ~ after");
        SHELL_EXPECT(launcher_report::Redact("", identity).empty());

        // The whole report: every file, the versions and the GPU line, all redacted.
        std::filesystem::path data = scratch / "reportdata";
        std::filesystem::path game = data / "SSA";
        std::filesystem::create_directories(game / "install-logs");
        std::filesystem::create_directories(data / "logs");
        WriteBytes(data / "launcher.ini", "[video]\nvsync=1\n[ssa]\niso=/Users/alice/Documents/SSA.iso\n");
        WriteBytes(game / "SSARecomp.log", "[log] /Users/alice/Library/Application Support/Skylanders/SSA/SSARecomp.log\n"
            "[version] SSARecomp 1.0.0, build abc123\n[system] macOS 15\n[system] CPU Test CPU 3000\n"
            "[gpu] adapter Apple M2: vendor 106B, integrated, unified memory, 8192 MB of video memory, driver n/a\n");
        WriteBytes(game / "SSARecomp.prev.log", "previous run\n\nFault (signal 11 segv) at host address 0x0\n"
            "[crash] where: guest thread 7, lr 0x8262D6D4, ctr 0x82850DA8, r1 0x700B7D30, r3 0x40070600, r31 0x82BB0000\n"
            "[crash] guest function: sub_822A95C0, called from sub_8262D690\n");
        WriteBytes(game / "install.log", "@done /Users/alice/Applications/SSARecomp\n");
        WriteBytes(game / "install.prev.log", "previous install\n");
        WriteBytes(game / "install-logs" / "compile.txt", "compiled on Alices-MacBook-Pro.local\n");
        WriteBytes(data / "logs" / "skylauncher-game.log", "started\n");
        WriteBytes(data / "logs" / "migration.log", "moved /Users/alice/Library/Application Support/SSARecomp\n");

        launcher_report::Sources sources;
        sources.runtimeFolder = game;
        sources.logFolder = data / "logs";
        sources.settingsFile = data / "launcher.ini";
        sources.iso = "/Users/alice/Documents/SSA.iso";
        sources.userName = identity.userName;
        sources.computerName = identity.computerName;
        sources.homeFolder = identity.homeFolder;
        sources.versions = launcher_report::ReadVersions("0.4.0", "1.2.3", game);
        std::string report = launcher_report::BuildReport(sources);
        SHELL_EXPECT(report.find("Launcher: 0.4.0\n") != std::string::npos && report.find("Kit: 1.2.3\n") != std::string::npos);
        SHELL_EXPECT(report.find("Game build: SSARecomp 1.0.0, build abc123\n") != std::string::npos);
        SHELL_EXPECT(report.find("GPU: Apple M2") != std::string::npos);
        SHELL_EXPECT(report.find("===== launcher.ini =====") != std::string::npos);
        SHELL_EXPECT(report.find("===== migration.log =====") != std::string::npos);
        SHELL_EXPECT(report.find("===== SSARecomp.log =====") != std::string::npos);
        SHELL_EXPECT(report.find("===== SSARecomp.prev.log =====") != std::string::npos);
        SHELL_EXPECT(report.find("===== skylauncher-game.log =====") != std::string::npos);
        SHELL_EXPECT(report.find("===== install.log =====") != std::string::npos);
        SHELL_EXPECT(report.find("===== install.prev.log =====") != std::string::npos);
        SHELL_EXPECT(report.find("===== install-logs/compile.txt =====") != std::string::npos);
        SHELL_EXPECT(report.find("previous run") != std::string::npos && report.find("previous install") != std::string::npos);
        // Where the game stopped: the last fault block of the log, from this run's log or else the previous run's, quoted once under
        // its own heading and named in the header; the CPU line comes from the log.
        SHELL_EXPECT(report.find("CPU: Test CPU 3000\n") != std::string::npos);
        SHELL_EXPECT(report.find("Last crash: yes (the previous run), see below\n") != std::string::npos);
        SHELL_EXPECT(report.find("===== last crash (the previous run) =====\nFault (signal 11 segv)") != std::string::npos);
        SHELL_EXPECT(report.find("[crash] guest function: sub_822A95C0, called from sub_8262D690") != std::string::npos);
        SHELL_EXPECT(launcher_report::CrashDetails("no fault here\n").empty());
        SHELL_EXPECT(launcher_report::CrashDetails("Fault (a)\nold\nFault (b)\nnew\n") == "Fault (b)\nnew\n");
        SHELL_EXPECT(report.find("alice") == std::string::npos && report.find("Alices-MacBook-Pro") == std::string::npos);
        SHELL_EXPECT(report.find("~") != std::string::npos && report.find("<iso folder>/SSA.iso") != std::string::npos);
        SHELL_EXPECT(launcher_report::FileName().starts_with("ArkeyanLauncher-report-") &&
            launcher_report::FileName().ends_with(".txt"));
        // The file the Support page and the crash dialog write: the first folder that takes it, else the next.
        {
            std::filesystem::path blocked = scratch / "reportblocked";
            WriteBytes(blocked, "a file where a folder is needed");
            std::filesystem::path desktop = scratch / "reportdesktop";
            std::filesystem::path saved = launcher_report::SaveToFirstFolder("text", "r.txt", { blocked / "Desktop", desktop });
            SHELL_EXPECT(saved == desktop / "r.txt" && std::filesystem::exists(saved));
            SHELL_EXPECT(launcher_report::SaveToFirstFolder("text", "r.txt", { blocked / "Desktop" }).empty());
        }
        printf("Report: the three home folders, names in text, computer and ISO folder redacted; nothing else changed\n");
    }
}

namespace
{
    // Which kit a launcher builds with: what it builds (games=), from which release, and the kit next to the launcher against
    // the downloaded ones (docs/updates.md, "The kit follows the launcher").
#if defined(_WIN32)
    bool Superuser() { return true; } // folder permissions don't block listing the same way
#else
    bool Superuser() { return geteuid() == 0; }
#endif

    // Folder walks never throw: a missing, plain-file or unreadable folder lists as empty (the start crash of 0.4.1).
    void DirectoryListChecks(const std::filesystem::path& scratch)
    {
        namespace fs = std::filesystem;
        fs::path root = scratch / "dir-list";
        fs::remove_all(root);
        fs::create_directories(root / "a");
        std::ofstream(root / "b.txt") << "x";
        std::error_code error;
        SHELL_EXPECT(ListDirectory(root, error).size() == 2 && !error);
        SHELL_EXPECT(ListDirectory(root / "missing", error).empty());
        SHELL_EXPECT(ListDirectory(root / "b.txt", error).empty());
        fs::create_directories(root / "locked");
        std::ofstream(root / "locked" / "c.txt") << "x";
        fs::permissions(root / "locked", fs::perms::none, error);
        bool threw = false;
        try { SHELL_EXPECT(ListDirectory(root / "locked", error).empty() || Superuser()); }
        catch (...) { threw = true; }
        SHELL_EXPECT(!threw);
        fs::permissions(root / "locked", fs::perms::owner_all, error);
        fs::remove_all(root, error);
    }

    void KitFollowChecks(const std::filesystem::path& scratch)
    {
        std::filesystem::path root = scratch / "kitfollow";
        auto setEnv = [](const char* name, const std::string& value) { TestSetEnv(name, value.c_str()); };
        auto kit = [&](const std::filesystem::path& folder, const std::string& text) {
            WriteBytes(folder / "SSABuild", "x");
            WriteBytes(folder / "SSABuild.exe", "x");
            WriteBytes(folder / "kit.txt", text);
        };
        TestUnsetEnv("SKY_LAUNCHER_KIT");
        setEnv("SKY_LAUNCHER_DATA", (root / "data").string());

        // What a kit builds: games= in kit.txt; a kit from before 0.5.0 says nothing and builds Spyro's Adventure only.
        kit(root / "old", "version=0.3.1\nbuild=a\n");
        kit(root / "multi", "version=0.5.0\nbuild=b\ngames=ssa, giants\n");
        std::optional<InstallKit> old = InstallKitIn(root / "old"), multi = InstallKitIn(root / "multi");
        SHELL_EXPECT(old && old->Builds("ssa") && !old->Builds("giants"));
        SHELL_EXPECT(multi && multi->Builds("ssa") && multi->Builds("giants") && !multi->Builds("swapforce"));
        kit(root / "three", "version=0.6.0\nbuild=d\ngames=ssa,giants,swapforce\n");
        std::optional<InstallKit> three = InstallKitIn(root / "three");
        SHELL_EXPECT(three && three->Builds("ssa") && three->Builds("giants") && three->Builds("swapforce") && !three->Builds("trapteam"));

        // From an older release than the launcher: the kit.txt version, or the folder's release tag; the player's own never.
        SHELL_EXPECT(KitOlderThanLauncher(*old, "0.4.0") && !KitOlderThanLauncher(*old, "0.3.1") && !KitOlderThanLauncher(*multi, "0.4.0"));
        old->tag = "v0.4.0";
        SHELL_EXPECT(!KitOlderThanLauncher(*old, "0.4.0") && KitOlderThanLauncher(*old, "0.4.1"));
        old->chosen = true;
        SHELL_EXPECT(!KitOlderThanLauncher(*old, "9.0.0"));
        kit(root / "none", "build=c\n");
        SHELL_EXPECT(!KitOlderThanLauncher(*InstallKitIn(root / "none"), "9.0.0"));

        // Next to the launcher or downloaded: the kit next to it, unless a downloaded one comes from a newer release.
        setEnv("SKY_LAUNCHER_BASE_PATH", (root / "base").string());
        kit(root / "base" / "ssa-kit-windows-x64", "version=0.3.1\nbuild=beside\n");
        auto found = FindInstallKit({});
        SHELL_EXPECT(found && found->version == "beside" && found->tag.empty() && !found->chosen);
        kit(DownloadedKitsFolder() / "v0.4.0", "version=0.5.0\nbuild=downloaded\ngames=ssa,giants\n");
        found = FindInstallKit({});
        SHELL_EXPECT(found && found->version == "downloaded" && found->tag == "v0.4.0" && found->Builds("giants"));
        kit(root / "base" / "ssa-kit-windows-x64", "version=0.5.0\nbuild=beside\n");
        found = FindInstallKit({});
        SHELL_EXPECT(found && found->version == "beside");
        // The player's own choice wins over both, and is never called old.
        setEnv("SKY_LAUNCHER_KIT", (root / "old").string());
        found = FindInstallKit({});
        SHELL_EXPECT(found && found->chosen && found->version == "a" && !KitOlderThanLauncher(*found, "9.0.0"));
        TestUnsetEnv("SKY_LAUNCHER_KIT");
        TestUnsetEnv("SKY_LAUNCHER_BASE_PATH");
        TestUnsetEnv("SKY_LAUNCHER_DATA");
        printf("Kit: games=, the release it comes from, next to the launcher against downloaded, the player's own\n");
    }
}

// The kit of a fake release for ArkeyanLauncherTest's launcher checks (tests/launcher_test.cpp): `kitFolder`'s files in a zip, as the kit
// asset of a release for each of `tags`, the last one the latest; `<tag>.json` is the release of that tag, as the launcher asks for
// its own; `gamesLine` (when not empty) is the release's hidden games line, "ssa=0.9.0 giants=0.8.1". `folder` is what SKY_LAUNCHER_UPDATE_URL points into. Where `zip` exists the Unix modes are kept (so the kit's SSABuild
// runs once unpacked).
void MakeKitRelease(const std::filesystem::path& folder, const std::vector<std::string>& tags, const std::filesystem::path& kitFolder,
    const std::string& gamesLine)
{
    std::filesystem::create_directories(folder);
    std::filesystem::path zip = folder / KitAssetName();
    std::filesystem::remove(zip);
    std::filesystem::remove_all(folder / "ssa-kit");
    std::filesystem::copy(kitFolder, folder / "ssa-kit", std::filesystem::copy_options::recursive);
    bool zipped = false;
#if !defined(_WIN32)
    zipped = std::system(("cd '" + folder.string() + "' && zip -qr '" + KitAssetName() + "' ssa-kit").c_str()) == 0;
#endif
    if (!zipped)
    {
        mz_zip_archive archive{};
        mz_zip_writer_init_file(&archive, zip.string().c_str(), 0);
        for (const auto& entry : std::filesystem::directory_iterator(kitFolder))
            if (entry.is_regular_file())
                mz_zip_writer_add_file(&archive, ("ssa-kit/" + entry.path().filename().string()).c_str(), entry.path().string().c_str(), nullptr, 0, 6);
        mz_zip_writer_finalize_archive(&archive);
        mz_zip_writer_end(&archive);
    }
    std::filesystem::remove_all(folder / "ssa-kit");
    std::string bytes = ReadBytes(zip);
    for (const std::string& tag : tags)
    {
        std::map<std::string, std::string> assets{ { KitAssetName(), bytes } };
        MakeRelease(folder, tag, assets, {}, true,
            "What\xE2\x80\x99s new\n- a thing" + (gamesLine.empty() ? std::string() : "\n\n<!-- games: " + gamesLine + " -->"));
        std::filesystem::copy_file(folder / "latest.json", folder / (tag + ".json"), std::filesystem::copy_options::overwrite_existing);
    }
}

int ShellChecks(const std::filesystem::path& scratch)
{
    ChecklistChecks(scratch);
    GameRunChecks(scratch);
    GameSessionChecks(scratch);
    UpdaterChecks(scratch);
    DirectoryListChecks(scratch);
    KitFollowChecks(scratch);
    SaveBackupChecks(scratch);
    ReportChecks(scratch);
    return g_shellFailures;
}
