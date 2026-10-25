// Checks the launcher's disc verification and settings file without showing any UI.
// usage: SSALauncherTest <game disc .iso> <scratch dir> [<damaged copy of the image>...]
#include "launcher/portal_status.h"
#include "launcher/game_install.h"
#include "launcher/launcher.h"
#include "assets/launcher_assets.h"
#include "shared/art/game_art.h"
#include "shared/collection/icons.h"
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/launcher_ui.h"
#include "launcher/ui/collection_model.h"
#include "launcher/ui/hold_confirm.h"
#include "shared/figures/figure_icons.h"
#include "shared/vfs.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <fmt/format.h>
#include <SDL3/SDL.h>
#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace
{
    int g_failures = 0;

#define EXPECT(condition) \
    do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); g_failures++; } } while (0)

    const char* StatusName(GameSourceStatus status)
    {
        switch (status)
        {
        case GameSourceStatus::Ok: return "Ok";
        case GameSourceStatus::NotFound: return "NotFound";
        case GameSourceStatus::Unreadable: return "Unreadable";
        case GameSourceStatus::Folder: return "Folder";
        case GameSourceStatus::NotXboxDisc: return "NotXboxDisc";
        case GameSourceStatus::NotXbox360Game: return "NotXbox360Game";
        case GameSourceStatus::Damaged: return "Damaged";
        case GameSourceStatus::WrongGame: return "WrongGame";
        case GameSourceStatus::WrongVersion: return "WrongVersion";
        }
        return "?";
    }

    GameSourceStatus Check(const std::filesystem::path& path)
    {
        GameSourceCheck check = CheckGameSource(path.string(), *FindGame("ssa"));
        printf("  %-14s %s\n", StatusName(check.status), path.filename().string().c_str());
        if (!check.message.empty())
            printf("    -> %s\n", check.message.c_str());
        return check.status;
    }

    void Put32LE(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24); }
    void Put32BE(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); }

    void WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& data)
    {
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), data.size());
    }

    // Minimal XDVDFS image (plain layout: volume descriptor at sector 32) whose root holds one file.
    void WriteDiscImage(const std::filesystem::path& path, const std::string& name, const std::vector<uint8_t>& file, uint32_t extraDeclaredSize = 0)
    {
        constexpr uint32_t Sector = 2048;
        std::vector<uint8_t> image(34 * Sector + file.size());
        memcpy(&image[32 * Sector], "MICROSOFT*XBOX*MEDIA", 20);
        Put32LE(&image[32 * Sector + 20], 33);
        Put32LE(&image[32 * Sector + 24], Sector);

        uint8_t* entry = &image[33 * Sector]; // no left/right subtrees
        Put32LE(entry + 4, 34);
        Put32LE(entry + 8, uint32_t(file.size()) + extraDeclaredSize);
        entry[12] = 0x80;
        entry[13] = uint8_t(name.size());
        memcpy(entry + 14, name.data(), name.size());

        memcpy(&image[34 * Sector], file.data(), file.size());
        WriteFile(path, image);
    }

    // XEX2 header carrying only the execution info.
    std::vector<uint8_t> FakeXex(uint32_t titleId, uint32_t mediaId, uint32_t version)
    {
        std::vector<uint8_t> xex(0x100);
        Put32BE(&xex[0x00], 0x58455832);
        Put32BE(&xex[0x14], 1);
        Put32BE(&xex[0x18], 0x00040006);
        Put32BE(&xex[0x1C], 0x40);
        Put32BE(&xex[0x40], mediaId);
        Put32BE(&xex[0x44], version);
        Put32BE(&xex[0x4C], titleId);
        return xex;
    }

    std::vector<uint8_t> RandomBytes(size_t size)
    {
        std::mt19937 random(1234);
        std::vector<uint8_t> data(size);
        for (uint8_t& byte : data)
            byte = uint8_t(random());
        return data;
    }

    std::string ResolveHeadless(std::vector<std::string> arguments)
    {
        arguments.insert(arguments.begin(), "SSARecomp");
        std::vector<char*> argv;
        for (std::string& argument : arguments)
            argv.push_back(argument.data());
        return ResolveGameSource(int(argv.size()), argv.data(), *FindGame("ssa"), false);
    }

    // SSABuild's events, then the fake kit (tests/fake_ssabuild.c) run through the launcher's installer: a failure, then a success.
    void InstallChecks(const std::filesystem::path& scratch, const std::string& iso)
    {
        printf("Install:\n");
        InstallProgress progress;
        progress.Apply("@step disc Reading the disc", 100);
        progress.Apply("@progress 3 10", 150);
        progress.Apply("@log reading default.xex", 160);
        progress.Apply("@step compile Compiling the game", 200);
        progress.Apply("warning: not an event", 210);
        progress.Apply("@error compile failed\r", 300);
        EXPECT(progress.steps.size() == 2 && progress.steps[0].id == "disc" && progress.steps[0].title == "Reading the disc");
        EXPECT(progress.steps[0].startedAt == 100 && progress.steps[0].endedAt == 200 && progress.steps[1].endedAt == 0);
        EXPECT(progress.done == 0 && progress.total == 0); // reset by the new step
        EXPECT(progress.error == "compile failed");
        EXPECT(progress.log.size() == 5);
        progress.Apply("@progress 7 9", 310);
        progress.Apply("@done /games/x/SSARecomp.exe", 400);
        EXPECT(progress.done == 7 && progress.total == 9 && progress.runtime == "/games/x/SSARecomp.exe" && progress.steps[1].endedAt == 400);

        LauncherConfig config;
        RecordInstalledGame(config, "abc", (scratch / "missing").string(), "1");
        EXPECT(FindInstalledGame(config, "abc").runtime.empty()); // the built game is gone
        EXPECT(FindInstalledGame(config, "abc").kitVersion == "1");
        EXPECT(GameInstallFolder("abc").filename() == "SSA" && GameInstallFolder("abc") == GameInstallFolder("def"));
        SetActiveGame("giants"); // one folder per game, side by side in the data folder
        EXPECT(GameInstallFolder("abc").filename() == "Giants");
        EXPECT(GameInstallFolder("abc") != GameInstallFolder("def").parent_path() / "SSA");
        SetActiveGame("ssa");
        EXPECT(GameInstallFolder("abc").filename() == "SSA");

        config["launcher"]["kit"] = SKYLAUNCHER_FAKE_KIT;
        std::optional<InstallKit> kit = FindInstallKit(config);
        EXPECT(kit && kit->version == "fake-1");
        auto run = [&](const char* fail) {
            if (fail != nullptr)
                SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_SSABUILD_FAIL", fail, true);
            else
                SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_SSABUILD_FAIL");
            GameInstaller installer;
            installer.Start(kit, iso, scratch / "game");
            while (installer.Running())
                SDL_Delay(20);
            return installer.Snapshot(1000);
        };
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_SSABUILD_DELAY", "0", true);
        InstallProgress failed = run("shaders");
        EXPECT(failed.state == InstallProgress::State::Failed && failed.error.find("didn't compile") != std::string::npos);
        EXPECT(failed.steps.size() == 4 && failed.runtime.empty());
        // The housekeeping after a successful build: the work folder goes, its logs stay, the log before is kept.
        std::filesystem::create_directories(scratch / "game" / "ssabuild-work" / "obj");
        { std::ofstream(scratch / "game" / "ssabuild-work" / "obj" / "big.o") << "object"; }
        std::filesystem::create_directories(scratch / "game" / "ssabuild-work" / "logs");
        { std::ofstream(scratch / "game" / "ssabuild-work" / "logs" / "link.log") << "link ok"; }
        InstallProgress done = run(nullptr);
        EXPECT(!std::filesystem::exists(scratch / "game" / "ssabuild-work") && std::filesystem::exists(scratch / "game" / "install.prev.log"));
        EXPECT(std::filesystem::exists(scratch / "game" / "install-logs" / "link.log"));
        EXPECT(done.state == InstallProgress::State::Done && done.steps.size() == 10);
        EXPECT(std::filesystem::path(done.runtime).filename().string().starts_with("SSARecomp"));
        EXPECT(std::filesystem::exists(scratch / "game" / "install.log"));
        // A game built by this launcher is marked; one without the mark is an older build (kit 0.3.0 or before).
        EXPECT(std::filesystem::exists(scratch / "game" / "layout.txt") && !IsOlderLayoutBuild(done.runtime, scratch / "game"));
        std::filesystem::remove(scratch / "game" / "layout.txt");
        EXPECT(IsOlderLayoutBuild(done.runtime, scratch / "game"));
        EXPECT(!IsOlderLayoutBuild(scratch / "elsewhere" / "SSARecomp", scratch / "game")); // the player's own runtime
        MarkGameLayout(scratch / "game");
        printf("  fake kit: failure at step %zu, then built %s\n", failed.steps.size(), done.runtime.c_str());

        // After a failed build nothing is cleaned.
        std::filesystem::create_directories(scratch / "game" / "ssabuild-work");
        run("shaders");
        EXPECT(std::filesystem::exists(scratch / "game" / "ssabuild-work"));
        std::filesystem::remove_all(scratch / "game" / "ssabuild-work");

        // Older kits and extracted compiler archives, in a scratch data folder.
        {
            namespace fs = std::filesystem;
            fs::path data = scratch / "data";
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SKYLAUNCHER_DATA", data.string().c_str(), true);
            for (const char* tag : { "v0.2.0", "v0.3.0", "v0.10.0", "notes", ".download" })
            {
                fs::create_directories(data / "kit" / tag);
                std::ofstream(data / "kit" / tag / "SSABuild") << tag;
            }
            fs::create_directories(data / "toolchains" / "llvm-mingw-x");
            std::ofstream(data / "toolchains" / "llvm-mingw-x" / "ssabuild-ready.txt") << "ok";
            std::ofstream(data / "toolchains" / "llvm-mingw-x.zip") << "zip";
            std::ofstream(data / "toolchains" / "other.zip") << "not extracted";
            CleanAfterInstall(data / "SSA", data / "kit" / "v0.3.0");
            EXPECT(!fs::exists(data / "kit" / "v0.2.0") && fs::exists(data / "kit" / "v0.3.0") && fs::exists(data / "kit" / "v0.10.0"));
            EXPECT(fs::exists(data / "kit" / "notes") && fs::exists(data / "kit" / ".download")); // not release tags: kept
            EXPECT(!fs::exists(data / "toolchains" / "llvm-mingw-x.zip") && fs::exists(data / "toolchains" / "other.zip"));
            // A kit that is not a downloaded one (next to the launcher) leaves them all alone.
            CleanAfterInstall(data / "SSA", scratch / "elsewhere" / "v9.0.0");
            EXPECT(fs::exists(data / "kit" / "v0.3.0") && fs::exists(data / "kit" / "v0.10.0"));
            SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "SKYLAUNCHER_DATA");
        }

        // Cancel stops the fake SSABuild, slowed down so it is still running.
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_SSABUILD_DELAY", "0.2", true);
        GameInstaller installer;
        installer.Start(kit, iso, scratch / "game");
        SDL_Delay(500);
        installer.Cancel();
        auto start = std::chrono::steady_clock::now();
        while (installer.Running())
            SDL_Delay(20);
        EXPECT(installer.Snapshot().state == InstallProgress::State::Cancelled);
        EXPECT(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
        EXPECT(!FindInstallKit({ { "launcher", { { "kit", (scratch / "nowhere").string() } } } }));
    }

    // The collection: SSARecomp's library (src/shared/collection) and the launcher's model over it, in a scratch folder
    // (SSA_COLLECTION, SSA_SAVE_DIR, SSA_MIGRATE_FROM): never the player's own.
    void CollectionChecks(const std::filesystem::path& scratch)
    {
        namespace fs = std::filesystem;
        fs::path folder = scratch / "collection", legacy = scratch / "legacy", save = scratch / "save";
        fs::create_directories(legacy);
        fs::create_directories(save);
        SDL_setenv_unsafe("SSA_COLLECTION", folder.string().c_str(), 1);
        SDL_setenv_unsafe("SSA_SAVE_DIR", save.string().c_str(), 1);
        SDL_setenv_unsafe("SSA_MIGRATE_FROM", legacy.string().c_str(), 1);
        SDL_setenv_unsafe("SSA_LAUNCHER_INI", "", 1);
        auto dump = [&](const fs::path& at, uint16_t type) {
            FigureData data = figures::CreateFigure(type, 0);
            WriteFile(at, std::vector<uint8_t>(data.begin(), data.end()));
            return at;
        };
#ifdef _WIN32
        long long otherPid = 0; // no parent process to borrow: the lock checks are skipped
#else
        long long otherPid = getppid(); // a live process that is not this one
#endif
        auto bytesOf = [](const fs::path& path) {
            std::ifstream in(path, std::ios::binary);
            return std::vector<char>(std::istreambuf_iterator<char>(in), {});
        };

        EXPECT(fs::path(collection::Folder()) == folder);
        const collection::Catalogue& catalogue = collection::Catalogue::Get();
        EXPECT(catalogue.Games().size() == 6 && catalogue.Accepts("ssa", 14) && catalogue.FindGame("ssa")->seats.size() == 4);

        // Migration: copied, never moved; a second pass copies nothing.
        dump(legacy / "014 Gill Grunt.sky", 14);
        dump(legacy / "016 Spyro.sky", 16);
        dump(legacy / "200 Anvil Rain.sky", 200);
        std::vector<char> before = bytesOf(legacy / "014 Gill Grunt.sky");
        collection::Migration migration = collection::Migrate(collection::LegacyFolder());
        EXPECT(migration.copied == 3 && migration.failed == 0);
        EXPECT(fs::exists(legacy / "014 Gill Grunt.sky") && bytesOf(legacy / "014 Gill Grunt.sky") == before);
        EXPECT(bytesOf(folder / "014 Gill Grunt.sky") == before);
        EXPECT(collection::Migrate(collection::LegacyFolder()).copied == 0);

        // A figure: stats, level, the nickname, backups, restore.
        std::string gill = collection::PathOf("014 Gill Grunt.sky");
        collection::Figure figure;
        EXPECT(collection::Read(gill, figure) && figure.toy != nullptr && figure.toy->name == "Gill Grunt" && figure.state == figures::AreaState::Blank);
        EXPECT(collection::UntouchedStarter(figure));
        figure.stats.experience = 2500;
        figure.stats.gold = 1234;
        figures::SetNicknameUtf8(figure.stats, "Gilou");
        std::string error;
        EXPECT(collection::WriteStats(gill, figure.stats, &error));
        EXPECT(collection::Read(gill, figure) && figure.state == figures::AreaState::Valid && figure.level == 3 && figure.stats.gold == 1234 &&
            figures::NicknameUtf8(figure.stats) == "Gilou");
        EXPECT(!collection::Backups(gill).empty());
        std::string backup = collection::Backup(gill);
        EXPECT(!backup.empty() && collection::Backups(gill).size() >= 2);
        figure.stats.gold = 9;
        EXPECT(collection::WriteStats(gill, figure.stats));
        EXPECT(collection::Restore(gill, backup) && collection::Read(gill, figure) && figure.stats.gold == 1234);
        EXPECT(collection::ResetToNew(gill) && collection::Read(gill, figure) && figure.state == figures::AreaState::Blank);

        // The folder's other jobs: label, favourite, duplicate, trash, import.
        EXPECT(collection::SetLabel(gill, "Gilou's") && collection::SetFavourite(gill, true));
        EXPECT(collection::Read(gill, figure) && figure.label == "Gilou's" && figure.favourite && figure.DisplayName() == "Gilou's");
        std::string copy = collection::Duplicate(gill);
        EXPECT(!copy.empty() && fs::exists(copy));
        EXPECT(collection::Delete(copy) && !fs::exists(copy) && collection::Trash().size() == 1);
        EXPECT(!collection::Undelete(collection::Trash().front().id).empty() && collection::Trash().empty());
        fs::path outside = dump(scratch / "dump.bin", 416);
        collection::ImportReport imported = collection::Import({ outside.string() });
        EXPECT(imported.added == 1 && imported.duplicates == 0);
        EXPECT(collection::Import({ outside.string() }).duplicates == 1);
        WriteFile(scratch / "junk.sky", std::vector<uint8_t>(100, 1));
        EXPECT(collection::Import({ (scratch / "junk.sky").string() }).invalid == 1);

        // Locks: the game's JSON. Another live process holds the figure: nothing writes it. A dead one does not.
        std::string spyro = collection::PathOf("016 Spyro.sky");
        collection::Figure spyroFigure;
        EXPECT(collection::Read(spyro, spyroFigure));
        spyroFigure.stats.experience = 500;
        EXPECT(collection::SetFavourite(spyro, true)); // metadata is not the dump
        if (otherPid > 0)
        {
            std::ofstream(spyro + ".lock") << fmt::format("{{ \"game\": \"ssa\", \"pid\": {}, \"since\": \"2026-10-24T10:15:00Z\" }}", otherPid);
            EXPECT(collection::GetLock(spyro).Busy() && collection::GetLock(spyro).game == "ssa");
            EXPECT(!collection::WriteStats(spyro, spyroFigure.stats, &error) && !error.empty());
            EXPECT(!collection::Delete(spyro));
        }
        std::ofstream(spyro + ".lock") << fmt::format("{{ \"game\": \"ssa\", \"pid\": {} }}", 99999999);
        EXPECT(collection::GetLock(spyro).stale && !collection::GetLock(spyro).Busy());
        EXPECT(collection::WriteStats(spyro, spyroFigure.stats));
        collection::BreakLock(spyro);

        // Seats: the session file the game reads (portal.txt), a figure to a seat it fits.
        const collection::Game& ssa = *catalogue.FindGame("ssa");
        std::string anvil = collection::PathOf("200 Anvil Rain.sky");
        std::string session = launcher_ui::SessionFile(ssa);
        EXPECT(session == (save / "portal.txt").string());
        EXPECT(launcher_ui::WriteSeats(session, { gill, "", anvil, "" }));
        std::vector<std::string> seats = launcher_ui::ReadSeats(ssa, session);
        EXPECT(seats.size() == 4 && seats[0] == gill && seats[1].empty() && seats[2] == anvil && seats[3].empty());
        // The game numbers slots by hardware, not seats: a figure goes to the first seat that takes it.
        { std::ofstream(session) << "5 " << spyro << "\n7 " << anvil << "\n"; }
        seats = launcher_ui::ReadSeats(ssa, session);
        EXPECT(seats[0] == spyro && seats[2] == anvil);
        EXPECT(launcher_ui::ShortGameName("ssa") == "Spyro's Adventure");

        launcher_ui::CollectionModel model;
        model.Load();
        EXPECT(model.figures.size() == 5 && model.SeatOf(spyro) == 0 && model.OnPortal().size() == 2);
        EXPECT(model.CanEditSeats() && model.SetSeat(1, gill) && model.SeatOf(gill) == 1);
        EXPECT(launcher_ui::ReadSeats(ssa, session)[1] == gill);
        EXPECT(model.SetSeat(1, "") && model.seats[1].empty());
        // Every kind of seat can be emptied, the others stay (issue #9): player, magic item, adventure pack.
        EXPECT(model.SetSeat(2, anvil) && model.seats[2] == anvil && model.seats[0] == spyro);
        std::vector<std::string> names(4);
        std::vector<collection::PortalEntry> overview = collection::PortalOverview(ssa, model.seats, names);
        EXPECT(overview.size() == 2 && overview[0].tag == "P1" && overview[1].tag == "Item" && overview[1].name == "200 Anvil Rain.sky");
        EXPECT(collection::PortalOverviewLine(overview, "none") == "P1 016 Spyro.sky \xC2\xB7 Item 200 Anvil Rain.sky");
        EXPECT(model.SetSeat(2, "") && model.seats[2].empty() && launcher_ui::ReadSeats(ssa, session)[2].empty());
        EXPECT(model.seats[0] == spyro && launcher_ui::ReadSeats(ssa, session)[0] == spyro);
        EXPECT(model.SetSeat(0, "") && model.OnPortal().empty() && collection::PortalOverviewLine(collection::PortalOverview(ssa, model.seats, names), "none") == "none");
        EXPECT(launcher_ui::ReadSeats(ssa, session) == std::vector<std::string>(4)); // the file names nothing
        EXPECT(model.SetSeat(0, spyro) && model.SetSeat(2, anvil)); // as before, for the checks below
        EXPECT(model.Poll(1000000, 0) == false); // nothing changed since Load (SetSeat's own write is not a change)
        // While a game holds a figure: the seats are read-only. When its dump changes the model reloads.
        if (otherPid > 0)
        {
            std::ofstream(spyro + ".lock") << fmt::format("{{ \"game\": \"ssa\", \"pid\": {} }}", otherPid);
            SDL_Delay(30);
            EXPECT(model.Poll(2000000, 0) && model.RunningGame() == "ssa" && !model.CanEditSeats());
            collection::BreakLock(spyro);
            SDL_Delay(30);
            EXPECT(model.Poll(3000000, 0) && model.CanEditSeats());
        }
        collection::Figure again;
        collection::Read(gill, again);
        again.stats.experience = 40000;
        collection::WriteStats(gill, again.stats);
        SDL_Delay(30);
        EXPECT(model.Poll(4000000, 0) && model.Find(gill)->level == 10);

        // The launcher's own earlier state moves over: favourites and "starts on the portal", by name.
        fs::remove(session);
        std::vector<std::string> moved = launcher_ui::MoveLauncherSettings(ssa, legacy.string(), { "016 Spyro.sky" }, { "200 Anvil Rain.sky", "014 Gill Grunt.sky", "new:5" });
        EXPECT(moved.size() == 4 && moved[0] == gill && moved[2] == anvil);
        collection::Read(spyro, again);
        EXPECT(again.favourite);

        // Unknown toys can be named (collection's catalogue.json), icons exist for every category.
        EXPECT(launcher_ui::NameUnknownToy(3502, "Mystery", collection::Category::Core, "ssa"));
        EXPECT(fs::exists(folder / "catalogue.json"));
        for (const collection::Toy& toy : catalogue.Toys())
        {
            std::vector<uint8_t> icon = figures::GenerateIcon(&toy, collection::VariantKind::Legendary, 32);
            EXPECT(icon.size() == 32 * 32 * 4 && icon[(16 * 32 + 16) * 4 + 3] > 0);
        }
        EXPECT(figures::GenerateIcon(nullptr, collection::VariantKind::Base, 32).size() == 32 * 32 * 4);
        // Deleting a figure (issue #13): it leaves the seat and the list, lands in trash/ with its metadata, the index is
        // rewritten in one go, and Undelete brings it all back. A collection.json that does not load refuses it, with a
        // reason, and moves nothing.
        std::string victim = collection::Create(4);
        EXPECT(!victim.empty() && collection::SetLabel(victim, "Doomed") && collection::SetFavourite(victim, true));
        model.Load();
        EXPECT(model.SetSeat(1, victim) && model.SeatOf(victim) == 1);
        size_t listed = model.figures.size(), trashed = collection::Trash().size();
        std::string why;
        EXPECT(model.Delete(victim, &why) && why.empty());
        EXPECT(!fs::exists(victim) && model.Find(victim) == nullptr && model.figures.size() == listed - 1);
        EXPECT(model.SeatOf(victim) < 0 && launcher_ui::ReadSeats(ssa, session)[1].empty());
        EXPECT(collection::Trash().size() == trashed + 1 && fs::exists(folder / "trash" / collection::Trash().front().id));
        std::vector<char> indexBytes = bytesOf(folder / "collection.json");
        std::string indexText(indexBytes.begin(), indexBytes.end());
        EXPECT(indexText.find("Doomed") != std::string::npos && indexText.find("\"figures\"") != std::string::npos);
        collection::Figure gone;
        EXPECT(!collection::Read(victim, gone));
        std::string back = collection::Undelete(collection::Trash().front().id);
        EXPECT(!back.empty() && collection::Read(back, gone) && gone.label == "Doomed" && gone.favourite);
        // A bad collection.json: refused once, with the reason, and the figure stays where it is.
        std::vector<char> goodIndex = bytesOf(folder / "collection.json");
        { std::ofstream(folder / "collection.json", std::ios::binary) << "{ \"format\": 1, \"figures\": {"; }
        trashed = collection::Trash().size();
        why.clear();
        EXPECT(!model.Delete(back, &why) && !why.empty() && fs::exists(back) && collection::Trash().size() == trashed);
        { std::ofstream(folder / "collection.json", std::ios::binary).write(goodIndex.data(), std::streamsize(goodIndex.size())); }
        EXPECT(model.Delete(back, &why) && !fs::exists(back));
        printf("Collection: library, migration, locks, seats, live refresh and icons checked\n");
    }
}

// The portal status: the VID:PID table and the status logic with fake enumerators.
static void PortalChecks()
{
    using namespace launcher_portal;
    EXPECT(ModelName(0x1430, 0x0150) == "Portal of Power");
    EXPECT(ModelName(0x1430, 0x1F17) == "Xbox 360 portal");
    EXPECT(ModelName(0x1430, 0x0ABC) == "Skylanders portal (1430:0ABC)");
    EXPECT(ModelName(0x045E, 0x0150).empty());

    Status plugged = Query([] { return Scan{ true, { { 0x1430, 0x0150 } } }; });
    EXPECT(plugged.connected && plugged.Line() == "Portal: Portal of Power");
    Status other = Query([] { return Scan{ true, { { 0x1430, 0x0ABC } } }; });
    EXPECT(other.Line() == "Portal: Skylanders portal (1430:0ABC)");
    Status unplugged = Query([] { return Scan{ true, {} }; });
    EXPECT(!unplugged.connected && unplugged.Line() == "Portal: not detected");
    Status foreign = Query([] { return Scan{ true, { { 0x045E, 0x0150 } } }; });
    EXPECT(!foreign.connected && foreign.Line() == "Portal: not detected");
    Status failed = Query([] { return Scan{}; });
    EXPECT(!failed.connected && failed.Line().empty());
    printf("Portal status: table and status line checked\n");
}

// Hold to confirm (HoldConfirm): the timing, a release or the pointer leaving cancels, one action per press, buttons
// told apart by their ID even when their labels are the same.
void HoldChecks()
{
    using launcher_ui::HoldConfirm;
    bool done = false;
    {
        // Timing: full after a second, acts once, not again while still held.
        HoldConfirm hold;
        EXPECT(hold.Update(7, true, true, 1000, done) == 0.0f && !done);
        EXPECT(hold.Update(7, true, true, 1500, done) == 0.5f && !done);
        EXPECT(hold.Update(7, true, true, 1999, done) < 1.0f && !done);
        EXPECT(hold.Update(7, true, true, 2000, done) == 1.0f && done);
        EXPECT(hold.Update(7, true, true, 2016, done) == 1.0f && !done);
        EXPECT(hold.Update(7, true, true, 5000, done) == 1.0f && !done);
        // Released, then pressed again: a new hold, which acts once more.
        EXPECT(hold.Update(7, false, false, 5100, done) == 0.0f && !done);
        EXPECT(hold.Update(7, true, true, 6000, done) == 0.0f && !done);
        EXPECT(hold.Update(7, true, true, 7000, done) == 1.0f && done);
    }
    {
        // Released early: nothing happens, and the next press starts from empty.
        HoldConfirm hold;
        hold.Update(7, true, true, 0, done);
        EXPECT(hold.Update(7, true, true, 600, done) == 0.6f && !done);
        EXPECT(hold.Update(7, false, false, 650, done) == 0.0f && !done);
        EXPECT(hold.Update(7, true, true, 700, done) == 0.0f && !done);
        EXPECT(hold.Update(7, true, true, 1600, done) == 0.9f && !done);
        EXPECT(hold.Update(7, true, true, 1700, done) == 1.0f && done);
    }
    {
        // The pointer leaves while pressing: cancelled, and coming back while still pressed does not resume it.
        HoldConfirm hold;
        hold.Update(7, true, true, 0, done);
        EXPECT(hold.Update(7, false, true, 500, done) == 0.0f && !done);
        EXPECT(hold.Update(7, true, true, 600, done) == 0.0f && !done);
        EXPECT(hold.Update(7, true, true, 3000, done) == 0.0f && !done);
        EXPECT(hold.Update(7, false, false, 3100, done) == 0.0f && !done);
        hold.Update(7, true, true, 3200, done);
        EXPECT(hold.Update(7, true, true, 4200, done) == 1.0f && done);
    }
    {
        // Identity: two "Delete for good" buttons (one per trash entry) are drawn every frame; only the held one fills,
        // and the other one, drawn before or after it, doesn't reset it. This is what never completed before.
        HoldConfirm hold;
        for (uint64_t now = 0; now < 1000; now += 16)
        {
            EXPECT(hold.Update(1, false, true, now, done) == 0.0f && !done);
            EXPECT(hold.Update(2, true, true, now, done) == float(now) / 1000.0f && !done);
            EXPECT(hold.Update(3, false, true, now, done) == 0.0f && !done);
        }
        hold.Update(1, false, true, 1000, done);
        EXPECT(hold.Update(2, true, true, 1000, done) == 1.0f && done);
        // Its entry is gone; the one now under the pointer is not active (ImGui wants a new click), and nothing else acts
        // until the input is released, even an activate key held on the next button.
        EXPECT(hold.Update(3, true, true, 3000, done) == 0.0f && !done);
        EXPECT(hold.Update(3, false, false, 3016, done) == 0.0f && !done);
        hold.Update(3, true, true, 3100, done);
        EXPECT(hold.Update(3, true, true, 4100, done) == 1.0f && done);
    }
    printf("Hold to confirm: timing, cancel and identity checked\n");
}

// Edit stats and names (#9 S-11, S-17): Save keeps what a game changed meanwhile, a name of spaces is no name.
void EditChecks()
{
    using launcher_ui::MergeEditedStats;
    figures::FigureStats base{};
    base.experience = 9000;
    base.gold = 2400;
    base.heroPoints = 3;
    base.playSeconds = 600;
    figures::SetNicknameUtf8(base, "Gilou");
    // The game played it while the editor was open: more time, more gold.
    figures::FigureStats now = base;
    now.playSeconds = 4200;
    now.gold = 2600;
    // The player changed the experience only: the game's gold and time stay.
    figures::FigureStats edited = base;
    edited.experience = 12000;
    figures::FigureStats saved = MergeEditedStats(now, base, edited, "Gilou", "Gilou");
    EXPECT(saved.experience == 12000 && saved.gold == 2600 && saved.playSeconds == 4200 && saved.heroPoints == 3);
    EXPECT(figures::NicknameUtf8(saved) == "Gilou");
    // The gold and the nickname changed here: they win over the game's.
    edited.gold = 100;
    saved = MergeEditedStats(now, base, edited, "Gilou", "Gill");
    EXPECT(saved.gold == 100 && saved.playSeconds == 4200 && figures::NicknameUtf8(saved) == "Gill");

    using launcher_ui::Trimmed;
    EXPECT(Trimmed("   ").empty() && Trimmed("").empty() && Trimmed("\t Gilou's  ") == "Gilou's" && Trimmed("A b") == "A b");
    printf("Edit stats and names: merge and trim checked\n");
}

int ShellChecks(const std::filesystem::path& scratch); // tests/shell_test.cpp

// The launcher window switching games in place (App::TestSwitchScript, in an offscreen window): both discs checked, a
// build of each made by the fake kit, the state of each kept across switches, PLAY returning the shown game's runtime.
static void SwitchChecks(const std::filesystem::path& scratch, const std::string& ssaIso, const std::string& giantsIso)
{
    auto set = [](const char* name, const std::string& value) { SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value.c_str(), true); };
    std::filesystem::create_directories(scratch / "shots");
    set("SDL_VIDEO_DRIVER", "offscreen");
    set("SDL_AUDIO_DRIVER", "dummy");
    set("SKYLANDERS_DATA", (scratch / "data").string());
    set("SKYLAUNCHER_DATA", (scratch / "launcher_data").string());
    set("SSA_COLLECTION", (scratch / "collection").string());
    set("SKYLAUNCHER_NO_UPDATE_CHECK", "1");
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_SSABUILD_FAIL");
    set("FAKE_SSABUILD_DELAY", "0.01"); // long enough that Giants is still building when SSA is shown again
    set("SSA_LAUNCHER_SHOTS", (scratch / "shots").string());
    set("SSA_LAUNCHER_TEST_SWITCH", "1");
    std::string ini = (scratch / "switch.ini").string();
    set("SSA_LAUNCHER_INI", ini);
    LauncherConfig config;
    config["launcher"]["kit"] = SKYLAUNCHER_FAKE_KIT;
    config["ssa"]["iso"] = ssaIso;
    config["giants"]["iso"] = giantsIso;
    launcher_ui::App app(*FindGame("ssa"), ini, config);
    std::string chosen = app.Run();
    EXPECT(app.TestFailures() == 0);
    EXPECT(&app.ChosenGame() == FindGame("giants") && chosen == giantsIso); // the last PLAY of the script
    EXPECT(config["launcher"]["game"] == "giants"); // remembered for the next start
    EXPECT(config["ssa"]["iso"] == ssaIso && config["giants"]["iso"] == giantsIso);
    SetActiveGame("ssa");
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: SSALauncherTest <game disc .iso> <scratch dir> [<damaged copy of the image>...]\n");
        return 2;
    }
    std::filesystem::path iso = argv[1];
    std::filesystem::path scratch = std::filesystem::path(argv[2]) / "launcher_test";
    std::filesystem::remove_all(scratch);
    std::filesystem::create_directories(scratch);
    const GameInfo& game = *FindGame("ssa");
    using Clock = std::chrono::steady_clock;
    auto milliseconds = [](Clock::time_point start)
    {
        return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    };

    CollectionChecks(scratch);
    HoldChecks();
    EditChecks();
    PortalChecks();
    InstallChecks(scratch, iso.string());
    g_failures += ShellChecks(scratch);

    // FIPS 180-4 examples, covering one- and two-block padding.
    auto sha256 = [](std::string_view text) { return Sha256Hex({ reinterpret_cast<const uint8_t*>(text.data()), text.size() }); };
    EXPECT(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT(sha256(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    printf("Game table: %zu game(s); \"ssa\" = %s, %zu release(s)\n", g_games.size(), game.name, game.releases.size());
    EXPECT(FindGame("unknown") == nullptr);
    const GameInfo* giants = FindGame("giants");
    EXPECT(g_games.size() == 2 && giants != nullptr && std::string(giants->runtime) == "SGRecomp" && giants->releases.size() == 1);
    EXPECT(giants != nullptr && giants->releases[0].titleId == 0x415608DA && std::string(giants->releases[0].defaultXexSha256).size() == 64);

    printf("Player's disc image:\n");
    auto start = Clock::now();
    EXPECT(Check(iso) == GameSourceStatus::Ok);
    EXPECT(CheckGameSource(iso.string(), game).xexSha256 == game.releases[0].defaultXexSha256);
    printf("  verified in %lld ms\n", milliseconds(start));

    DiscFileSystem disc;
    EXPECT(disc.Open(iso.string().c_str()));
    start = Clock::now();
    TitleMetadata metadata = ReadTitleMetadata(disc.ReadAll("default.xex"));
    const std::vector<uint8_t>& png = metadata.iconPng;
    bool isPng = png.size() > 24 && memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8) == 0;
    EXPECT(!metadata.name.empty());
    EXPECT(metadata.titleId == 0x415608AB);
    EXPECT(isPng);
    if (isPng)
    {
        auto be32 = [&](size_t offset) { return uint32_t(png[offset]) << 24 | png[offset + 1] << 16 | png[offset + 2] << 8 | png[offset + 3]; };
        printf("  XDBF title name \"%s\", icon PNG %ux%u (%zu bytes), read in %lld ms (kept in memory only)\n",
            metadata.name.c_str(), be32(16), be32(20), png.size(), milliseconds(start));
    }

    // The game's pictures (shared/art/game_art.h), into a scratch data folder: logo, background, title icon, portraits.
    auto artChecks = [&](const char* id, const std::string& image, int minimumPortraits) {
        DiscFileSystem gameDisc;
        EXPECT(gameDisc.Open(image.c_str()));
        TitleMetadata meta = ReadTitleMetadata(gameDisc.ReadAll("default.xex"));
        LauncherAssets::Provider provider;
        provider.Init(id);
        auto began = Clock::now();
        provider.ExtractFromDisc(gameDisc, meta);
        long long first = milliseconds(began);
        art::Extraction found = art::Extract(id, art::ReadFrom(gameDisc), {}, art::Portraits);
        printf("  %s: logo %s, background %s, title icon %s, %d portraits, read in %lld ms\n", id,
            provider.Get(LauncherAssets::Role::Logo).valid ? "yes" : "NO", provider.Get(LauncherAssets::Role::HeroBackground).valid ? "yes" : "NO",
            provider.Get(LauncherAssets::Role::TitleIcon).valid ? "yes" : "NO", found.portraits, first);
        EXPECT(provider.Get(LauncherAssets::Role::Logo).valid && provider.Get(LauncherAssets::Role::HeroBackground).valid);
        EXPECT(provider.Get(LauncherAssets::Role::TitleIcon).valid && LauncherAssets::Cached(id, LauncherAssets::Role::Logo).valid);
        EXPECT(found.portraits >= minimumPortraits);
    };
    printf("Disc pictures:\n");
    std::string dataBefore = getenv("SKYLANDERS_DATA") != nullptr ? getenv("SKYLANDERS_DATA") : "";
    SDL_setenv_unsafe("SKYLANDERS_DATA", (scratch / "art-data").string().c_str(), 1);
    artChecks("ssa", iso.string(), 70);
    if (const char* giantsIso = getenv("SKYLAUNCHER_TEST_GIANTS_ISO"); giantsIso != nullptr)
    {
        artChecks("giants", giantsIso, 123);
        // The rule for a toy: the shown game's picture, else the other game's (Jet-Vac has none on Spyro's Adventure).
        const collection::Toy* eruptor = collection::Catalogue::Get().Find(9);
        const collection::Toy* jetVac = collection::Catalogue::Get().Find(100);
        EXPECT(eruptor != nullptr && jetVac != nullptr);
        if (eruptor != nullptr && jetVac != nullptr)
        {
            EXPECT(collection::PortraitPath("ssa", *eruptor).find("toy_eruptor_hud") != std::string::npos);
            EXPECT(collection::PortraitPath("giants", *eruptor).ends_with("eruptor.png"));
            EXPECT(collection::PortraitPath("giants", *eruptor, 0x1206).ends_with("eruptor_lightcore.png"));
            EXPECT(collection::PortraitPath("ssa", *jetVac).ends_with("eagle.png"));
        }
    }
    if (dataBefore.empty())
        SDL_unsetenv_unsafe("SKYLANDERS_DATA");
    else
        SDL_setenv_unsafe("SKYLANDERS_DATA", dataBefore.c_str(), 1);

    printf("Wrong inputs:\n");
    EXPECT(Check(scratch / "missing.iso") == GameSourceStatus::NotFound);

    std::filesystem::create_directories(scratch / "extracted");
    WriteFile(scratch / "extracted" / "default.xex", FakeXex(0x415608AB, 0x53CE7B97, 2));
    EXPECT(Check(scratch / "extracted") == GameSourceStatus::Folder);

    WriteFile(scratch / "locked.iso", RandomBytes(4096));
    std::filesystem::permissions(scratch / "locked.iso", std::filesystem::perms::none);
    EXPECT(Check(scratch / "locked.iso") == GameSourceStatus::Unreadable);
    std::filesystem::permissions(scratch / "locked.iso", std::filesystem::perms::owner_all);

    WriteFile(scratch / "empty.iso", {});
    EXPECT(Check(scratch / "empty.iso") == GameSourceStatus::NotXboxDisc);
    WriteFile(scratch / "random.iso", RandomBytes(1 << 20));
    EXPECT(Check(scratch / "random.iso") == GameSourceStatus::NotXboxDisc);

    WriteDiscImage(scratch / "original_xbox.iso", "default.xbe", RandomBytes(4096));
    EXPECT(Check(scratch / "original_xbox.iso") == GameSourceStatus::NotXbox360Game);
    WriteDiscImage(scratch / "not_a_xex.iso", "default.xex", RandomBytes(4096));
    EXPECT(Check(scratch / "not_a_xex.iso") == GameSourceStatus::Damaged);
    WriteDiscImage(scratch / "cut_short.iso", "default.xex", FakeXex(0x415608AB, 0x53CE7B97, 2), 4096);
    EXPECT(Check(scratch / "cut_short.iso") == GameSourceStatus::Damaged);
    WriteDiscImage(scratch / "other_game.iso", "default.xex", FakeXex(0x4D5307E6, 0x11111111, 0));
    EXPECT(Check(scratch / "other_game.iso") == GameSourceStatus::WrongGame);
    WriteDiscImage(scratch / "other_region.iso", "default.xex", FakeXex(0x415608AB, 0x12345678, 0x00000100));
    EXPECT(Check(scratch / "other_region.iso") == GameSourceStatus::WrongVersion);
    WriteDiscImage(scratch / "modified.iso", "default.xex", FakeXex(0x415608AB, 0x53CE7B97, 2));
    EXPECT(Check(scratch / "modified.iso") == GameSourceStatus::Damaged);

    printf("Damaged copies of the player's image:\n");
    for (int i = 3; i < argc; i++)
    {
        GameSourceStatus status = Check(argv[i]);
        EXPECT(status != GameSourceStatus::Ok);
    }

    // The Giants disc (optional: SKYLAUNCHER_TEST_GIANTS_ISO): its own game accepts it, Spyro's Adventure says it is another game.
    if (const char* giantsIso = getenv("SKYLAUNCHER_TEST_GIANTS_ISO"); giantsIso != nullptr && giants != nullptr)
    {
        printf("Giants disc image:\n");
        GameSourceCheck own = CheckGameSource(giantsIso, *giants);
        EXPECT(own.status == GameSourceStatus::Ok && own.xexSha256 == giants->releases[0].defaultXexSha256);
        EXPECT(CheckGameSource(giantsIso, game).status == GameSourceStatus::WrongGame);
        EXPECT(CheckGameSource(iso.string(), *giants).status == GameSourceStatus::WrongGame);
        printf("Switching games in the window:\n");
        SwitchChecks(scratch, iso.string(), giantsIso);
    }
    printf("Command line without UI:\n");
    EXPECT(ResolveHeadless({ iso.string() }) == iso.string());
    EXPECT(ResolveHeadless({ "--select-iso", iso.string() }) == iso.string());
    EXPECT(ResolveHeadless({}).empty());
    EXPECT(ResolveHeadless({ (scratch / "random.iso").string() }).empty());

    printf("Launcher catalog and display helpers:\n");
    auto catalog = LauncherGameCatalog();
    EXPECT(catalog.size() == 7); // the six console games and SuperChargers Racing
    EXPECT(std::string(catalog.front().id) == "ssa");
    EXPECT(catalog.front().enabled);
    EXPECT(std::string(catalog.front().name) == "Spyro's Adventure");
    size_t enabledEntries = 0;
    for (const LauncherGameEntry& entry : catalog)
    {
        EXPECT(entry.id != nullptr && *entry.id != '\0');
        EXPECT(entry.name != nullptr && *entry.name != '\0');
        EXPECT(entry.accent != 0);
        enabledEntries += entry.enabled ? 1 : 0;
    }
    EXPECT(enabledEntries == g_games.size());

    LauncherConfig helperConfig;
    // The game reads "[ssa] language" / "voice_language" (codes); the labels name them, unknown codes stay as they are.
    EXPECT(LauncherLanguageLabel(helperConfig, game) == "English");
    EXPECT(LauncherVoiceLabel(helperConfig, game) == "English");
    helperConfig["ssa"]["language"] = "fr";
    EXPECT(LauncherLanguageLabel(helperConfig, game) == "Français");
    helperConfig["ssa"]["voice_language"] = "sv";
    EXPECT(LauncherVoiceLabel(helperConfig, game) == "Svenska");
    helperConfig["ssa"]["language"] = "pt-BR";
    EXPECT(LauncherLanguageLabel(helperConfig, game) == "pt-BR");
    helperConfig["ssa"]["language"].clear();
    helperConfig["ssa"]["voice_language"].clear();

    EXPECT(!LauncherFullscreen(helperConfig));
    helperConfig["launcher"]["fullscreen"] = "YES";
    EXPECT(LauncherFullscreen(helperConfig));
    helperConfig["launcher"]["fullscreen"] = "0";
    EXPECT(!LauncherFullscreen(helperConfig));
    helperConfig["launcher"]["fullscreen"].clear();
    helperConfig[""]["fullscreen"] = "on";
    EXPECT(LauncherFullscreen(helperConfig));

    EXPECT(LauncherFormatSize(0) == "0 bytes");
    EXPECT(LauncherFormatSize(1023) == "1023 bytes");
    EXPECT(LauncherFormatSize(1024) == "1 KB");
    EXPECT(LauncherFormatSize(5 * 1024) == "5 KB");
    EXPECT(LauncherFormatSize(5ull * 1024 * 1024) == "5 MB");
    EXPECT(LauncherFormatSize(2ull * 1024 * 1024 * 1024) == "2.0 GB");
    EXPECT(LauncherFormatSize(8738846720ull) == "8.1 GB");

    EXPECT(LauncherMiddleEllipsis("hello", 10) == "hello");
    EXPECT(LauncherMiddleEllipsis("hello", 5) == "hello");
    EXPECT(LauncherMiddleEllipsis("abcdefghij", 3) == "abc");
    EXPECT(LauncherMiddleEllipsis("abcdefghij", 7) == "ab...ij");
    EXPECT(LauncherMiddleEllipsis("d\xc3\xa9j\xc3\xa0 vu's.iso", 9) == "d\xc3\xa9j...iso");
    std::string longPath = "/Volumes/USB disk/Giants [Disc] #1 = a very long name.iso";
    std::string shortened = LauncherMiddleEllipsis(longPath, 20);
    EXPECT(shortened.size() < longPath.size());
    EXPECT(shortened.find("...") != std::string::npos);
    EXPECT(shortened.rfind(".iso") == shortened.size() - 4);

    EXPECT(LauncherDisplayName("Skylanders: Spyro's Adventure", game) == "Skylanders: Spyro's Adventure");
    EXPECT(LauncherDisplayName("", game) == game.name);

    printf("Settings file:\n");
    LauncherConfig config;
    config[""]["version"] = "1";
    config["ssa"]["iso"] = iso.string();
    config["giants"]["iso"] = "/Volumes/USB disk/Giants [Disc] #1 = d\xc3\xa9j\xc3\xa0 vu's.iso";
    std::filesystem::path settings = scratch / "launcher.ini";
    EXPECT(SaveLauncherConfig(settings.string(), config));
    EXPECT(LoadLauncherConfig(settings.string()) == config);
    std::ifstream saved(settings);
    printf("%s", std::string(std::istreambuf_iterator<char>(saved), {}).c_str());

    std::string handEdited = "# comment\r\n  [ ssa ]  \r\n\r\n iso = /a b/c.iso \r\n; other\r\nnot a pair\r\n[empty]\r\n";
    WriteFile(scratch / "edited.ini", { handEdited.begin(), handEdited.end() });
    LauncherConfig edited = LoadLauncherConfig((scratch / "edited.ini").string());
    EXPECT(edited.size() == 1 && edited["ssa"].size() == 1 && edited["ssa"]["iso"] == "/a b/c.iso");
    EXPECT(LoadLauncherConfig((scratch / "missing.ini").string()).empty());

    LauncherConfig video;
    video["video"]["aspect"] = "16:9";
    video["video"]["texture_packs"] = "0";
    video["video"]["texture_dump"] = "1";
    EXPECT(SaveLauncherConfig(settings.string(), video));
    LauncherConfig videoBack = LoadLauncherConfig(settings.string());
    EXPECT(videoBack["video"]["aspect"] == "16:9" && videoBack["video"]["texture_packs"] == "0" && videoBack["video"]["texture_dump"] == "1");

    std::filesystem::remove_all(scratch);
    printf(g_failures == 0 ? "All launcher checks passed\n" : "%d launcher check(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
