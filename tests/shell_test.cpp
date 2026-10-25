// SkylauncherTest's checks for the launcher shell: the install checklist, game runs and logs, the updater, settings.
// Called from launcher_test.cpp's main; returns the number of failures.
#include "launcher/game_install.h"
#include "launcher/game_run.h"
#include "launcher/install_checklist.h"
#include "launcher/launcher.h"
#include "launcher/updater.h"
#include "shared/collection/save_backup.h"
#include "shared/mini_json.h"
#include <miniz.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <filesystem>
#include <map>
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
        SHELL_EXPECT(InstallFailureHeadline(list, failed).find("couldn't be downloaded") != std::string::npos);
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
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SKYLAUNCHER_LOG_DIR", logs.string().c_str(), true);
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
        SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "SKYLAUNCHER_LOG_DIR");
#endif
        printf("Game run: capture of stdout and stderr, exit code, logs text and portal probe checked\n");
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

    std::string FileUrl(const std::filesystem::path& file) { return "file://" + file.string(); }

    // A fake release in `folder`: latest.json, SHA256SUMS and the assets given as name -> content. `sumsFor` lists the hashes.
    void MakeRelease(const std::filesystem::path& folder, const std::string& tag, const std::map<std::string, std::string>& assets,
        const std::map<std::string, std::string>& hashes, bool withSums = true)
    {
        std::filesystem::create_directories(folder);
        std::string json = "{\"tag_name\":\"" + tag + "\",\"name\":\"Skylauncher " + tag + "\",\"body\":\"What\\u2019s new\\n- a thing\",\"html_url\":\"https://example.test/r\",\"prerelease\":false,\"assets\":[";
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
        MakeRelease(updates / "r1", "v99.0.0", { { "Skylauncher.exe", "exe bytes" }, { "ssa-kit-test.zip", "zip" } }, {});
        SHELL_EXPECT(ParseRelease(ReadBytes(updates / "r1" / "latest.json"), release) && release.tag == "v99.0.0");
        SHELL_EXPECT(release.notes.find("What\xE2\x80\x99s new") == 0 && release.assets.size() == 3 && FindAsset(release, "Skylauncher.exe")->size == 9);
        std::string sums = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  a b.txt\n"
                           "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855 *empty.bin\r\nshort  x\n";
        SHELL_EXPECT(FindChecksum(sums, "a b.txt").starts_with("ba7816bf") && FindChecksum(sums, "empty.bin").starts_with("e3b0c442"));
        SHELL_EXPECT(FindChecksum(sums, "x").empty() && FindChecksum(sums, "nothing").empty());
        SHELL_EXPECT(LauncherAssetName().starts_with("Skylauncher") && KitAssetName().starts_with("ssa-kit-"));

        // Downloads: good, damaged, and a release without SHA256SUMS.
        std::string downloadError;
        auto file = DownloadVerified(release, "Skylauncher.exe", updates / "got", {}, nullptr, downloadError);
        SHELL_EXPECT(!file.empty() && ReadBytes(file) == "exe bytes" && !std::filesystem::exists(updates / "got" / "SHA256SUMS"));
        MakeRelease(updates / "r2", "v99.0.0", { { "Skylauncher.exe", "exe bytes" } }, { { "Skylauncher.exe", std::string(64, 'a') } });
        ParseRelease(ReadBytes(updates / "r2" / "latest.json"), release);
        file = DownloadVerified(release, "Skylauncher.exe", updates / "got2", {}, nullptr, downloadError);
        SHELL_EXPECT(file.empty() && downloadError.find("SHA-256") != std::string::npos && !std::filesystem::exists(updates / "got2" / "Skylauncher.exe"));
        MakeRelease(updates / "r3", "v99.0.0", { { "Skylauncher.exe", "exe bytes" } }, {}, false);
        ParseRelease(ReadBytes(updates / "r3" / "latest.json"), release);
        file = DownloadVerified(release, "Skylauncher.exe", updates / "got3", {}, nullptr, downloadError);
        SHELL_EXPECT(file.empty() && downloadError.find("SHA256SUMS") != std::string::npos);
        file = DownloadVerified(release, "Other.exe", updates / "got3", {}, nullptr, downloadError);
        SHELL_EXPECT(file.empty() && downloadError.find("Other.exe") != std::string::npos);
        HttpResult missing = HttpDownload(FileUrl(updates / "nowhere.bin"), updates / "got4" / "x");
        SHELL_EXPECT(!missing.ok && !missing.error.empty() && !std::filesystem::exists(updates / "got4" / "x.part"));
        SHELL_EXPECT(!HttpDownload("http://example.test/x", updates / "got4" / "y").ok); // never plain http

        // Swapping: the new one in place, the old one kept as .old, undone when the new one is missing.
        std::filesystem::path app = updates / "swap" / "Skylauncher.exe";
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
        SHELL_EXPECT(!ExtractZip(zip, updates / "unz", error) && error.find("outside") != std::string::npos && !std::filesystem::exists(updates / "evil.txt"));
        SHELL_EXPECT(!ExtractZip(updates / "r1" / "latest.json", updates / "unz2", error));
        MakeZip(zip, { { "SSABuild", "first install" }, { "SSABuild.exe", "first install" } });
        SHELL_EXPECT(InstallKitZip(zip, updates / "fresh" / "ssa-kit", error) && ReadBytes(updates / "fresh" / "ssa-kit" / "SSABuild") == "first install");

        // The manager: a check against the fake latest.json, then the kit update from it.
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SKYLAUNCHER_UPDATE_URL", FileUrl(updates / "r1" / "latest.json").c_str(), true);
        UpdateManager manager;
        UpdateManager::Settings settings;
        settings.kitInstalled = "1.0.0";
        manager.Check(settings);
        WaitFor(manager);
        UpdateManager::State state = manager.Snapshot();
        SHELL_EXPECT(state.checked && state.launcherNewer && state.launcher.tag == "v99.0.0" && !state.kitNewer && state.phase == UpdateManager::Phase::Idle);
        MakeRelease(updates / "r4", "v0.0.1", { { "Skylauncher.exe", "x" } }, {});
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SKYLAUNCHER_UPDATE_URL", FileUrl(updates / "r4" / "latest.json").c_str(), true);
        manager.Check(settings);
        WaitFor(manager);
        SHELL_EXPECT(manager.Snapshot().checked && !manager.Snapshot().launcherNewer);
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SKYLAUNCHER_UPDATE_URL", FileUrl(updates / "none.json").c_str(), true);
        manager.Check(settings);
        WaitFor(manager);
        SHELL_EXPECT(!manager.Snapshot().checked && !manager.Snapshot().checkError.empty());

        // Kit update: a release with the kit asset, a newer tag than the installed kit.
        std::string kitZip = MakeZip(updates / "kitasset" / KitAssetName(), { { "SSABuild", "kit v2" }, { "SSABuild.exe", "kit v2" }, { "kit.txt", "version=2.0.0\nbuild=b2\n" } });
        MakeRelease(updates / "r5", "v2.0.0", { { KitAssetName(), kitZip } }, {});
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SKYLAUNCHER_UPDATE_URL", FileUrl(updates / "r5" / "latest.json").c_str(), true);
        settings.kitRepo = "someone/kit";
        settings.kitFolder = updates / "kitdest" / "ssa-kit";
        WriteBytes(settings.kitFolder / "SSABuild", "kit v1");
        manager.Check(settings);
        WaitFor(manager);
        SHELL_EXPECT(manager.Snapshot().kitNewer && manager.Snapshot().kit.tag == "v2.0.0");
        manager.UpdateKit();
        WaitFor(manager);
        state = manager.Snapshot();
        SHELL_EXPECT(state.phase == UpdateManager::Phase::Done && !state.kitNewer && ReadBytes(settings.kitFolder / "SSABuild") == "kit v2");
        SHELL_EXPECT(ReadBytes(settings.kitFolder / "kit.txt").find("build=b2") != std::string::npos);
        settings.kitInstalled = "v2.0.0";
        manager.Check(settings);
        WaitFor(manager);
        SHELL_EXPECT(!manager.Snapshot().kitNewer);
        // The kit download INSTALL starts with: the release of this launcher's tag, else the latest, verified, then found.
        auto setEnv = [](const char* name, const std::string& value) { SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value.c_str(), true); };
        SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "SKYLAUNCHER_KIT");
        setEnv("SKYLAUNCHER_DATA", (updates / "data").string());
        std::string launcherTag = "v" + LauncherVersion();
        std::string dlZip = MakeZip(updates / "dl" / KitAssetName(), { { "ssa-kit/", "" }, { "ssa-kit/SSABuild", "dl kit" }, { "ssa-kit/SSABuild.exe", "dl kit" }, { "ssa-kit/kit.txt", "build=b9\n" } });
        MakeRelease(updates / "dl", launcherTag, { { KitAssetName(), dlZip } }, {});
        std::filesystem::create_directories(updates / "dl-other");
        std::filesystem::copy_file(updates / "dl" / "latest.json", updates / "dl-other" / (launcherTag + ".json"));
        MakeRelease(updates / "dl-other", "v77.0.0", { { KitAssetName(), "wrong zip" } }, {}); // the latest: must not be used
        setEnv("SKYLAUNCHER_UPDATE_URL", FileUrl(updates / "dl-other" / "latest.json"));
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
        setEnv("SKYLAUNCHER_UPDATE_URL", FileUrl(updates / "dl-latest" / "latest.json"));
        kitDir = DownloadKit("x/y", launcherTag, updates / "data2" / "kit", {}, nullptr, kitError);
        SHELL_EXPECT(kitDir == updates / "data2" / "kit" / "v77.0.0" && ReadBytes(kitDir / "SSABuild") == "latest kit");
        // A bad hash and a SHA256SUMS without the line are refused, and nothing is left behind.
        MakeRelease(updates / "dl-bad", "v78.0.0", { { KitAssetName(), latestZip } }, { { KitAssetName(), std::string(64, 'b') } });
        setEnv("SKYLAUNCHER_UPDATE_URL", FileUrl(updates / "dl-bad" / "latest.json"));
        kitDir = DownloadKit("x/y", launcherTag, updates / "data3" / "kit", {}, nullptr, kitError);
        SHELL_EXPECT(kitDir.empty() && kitError.find("SHA-256") != std::string::npos && !std::filesystem::exists(updates / "data3" / "kit" / "v78.0.0"));
        MakeRelease(updates / "dl-line", "v79.0.0", { { KitAssetName(), latestZip }, { "other.bin", "x" } }, {});
        WriteBytes(updates / "dl-line" / "SHA256SUMS", Sha("x") + "  other.bin\n");
        setEnv("SKYLAUNCHER_UPDATE_URL", FileUrl(updates / "dl-line" / "latest.json"));
        kitDir = DownloadKit("x/y", launcherTag, updates / "data3" / "kit", {}, nullptr, kitError);
        SHELL_EXPECT(kitDir.empty() && kitError.find("doesn't list") != std::string::npos);
        MakeRelease(updates / "dl-noasset", "v80.0.0", { { "Skylauncher.exe", "x" } }, {});
        setEnv("SKYLAUNCHER_UPDATE_URL", FileUrl(updates / "dl-noasset" / "latest.json"));
        SHELL_EXPECT(DownloadKit("x/y", launcherTag, updates / "data3" / "kit", {}, nullptr, kitError).empty() && kitError.find(KitAssetName()) != std::string::npos);
        // On a thread, as the card polls it.
        setEnv("SKYLAUNCHER_UPDATE_URL", FileUrl(updates / "dl-latest" / "latest.json"));
        KitDownloader downloader;
        downloader.Start("x/y", launcherTag, updates / "data4" / "kit");
        for (int i = 0; i < 500 && downloader.Running(); i++)
            SDL_Delay(20);
        KitDownloader::Snapshot_ result = downloader.Snapshot();
        SHELL_EXPECT(result.state == KitDownloader::State::Done && result.tag == "v77.0.0" && std::filesystem::exists(result.folder / "SSABuild"));
        SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "SKYLAUNCHER_DATA");
        SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "SKYLAUNCHER_UPDATE_URL");
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

int ShellChecks(const std::filesystem::path& scratch)
{
    ChecklistChecks(scratch);
    GameRunChecks(scratch);
    UpdaterChecks(scratch);
    SaveBackupChecks(scratch);
    return g_shellFailures;
}
