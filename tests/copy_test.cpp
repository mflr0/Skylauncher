// "Copy the game data" (docs/launcher_ui.md "Game data"): the copy of a real disc's small files (happy path, not enough space,
// interrupted then resumed, damaged and the fall back to the disc image, removal), then the window on stand-in discs (the
// real default.xex alone): the wrong-disc button, the copy through its states, playing with no disc image (App::TestCopyScript).
// The real discs are read only; every copy goes to a scratch folder.
#include "launcher/game_copy.h"
#include "launcher/env.h"
#include "launcher/game_install.h"
#include "launcher/launcher.h"
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/launcher_ui.h"
#include "shared/collection/datadir.h"
#include "shared/vfs.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace
{
    int g_failures = 0;

#define EXPECT(condition) \
    do { if (!(condition)) { fprintf(stderr, "FAIL copy_test line %d: %s\n", __LINE__, #condition); g_failures++; } } while (0)

    namespace fs = std::filesystem;

    // In SDL's environment (the window reads it there) and in the process's (datadir.h reads it there).
    void SetEnv(const char* name, const std::string& value)
    {
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value.c_str(), true);
        SDL_setenv_unsafe(name, value.c_str(), 1);
    }

    void UnsetEnv(const char* name)
    {
        SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), name);
        SDL_unsetenv_unsafe(name);
    }

    // Nothing may reach the player's own data folders: stop a check whose folders are not under the scratch folder.
    bool UnderScratch(const fs::path& scratch, const fs::path& path)
    {
        std::error_code error;
        fs::path base = fs::weakly_canonical(scratch, error), inner = fs::weakly_canonical(path, error);
        bool inside = !error && std::mismatch(base.begin(), base.end(), inner.begin(), inner.end()).first == base.end();
        if (!inside)
            fprintf(stderr, "FAIL copy_test: %s is not under the scratch folder: set SKYLANDERS_DATA before the checks\n", path.string().c_str());
        return inside;
    }

    void Put32LE(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24); }

    // A minimal XDVDFS image (plain layout) whose root holds one file: the real default.xex makes it pass the disc check.
    void WriteStandInDisc(const fs::path& path, const std::vector<uint8_t>& xex)
    {
        constexpr uint32_t Sector = 2048;
        std::vector<uint8_t> image(34 * Sector + xex.size());
        memcpy(&image[32 * Sector], "MICROSOFT*XBOX*MEDIA", 20);
        Put32LE(&image[32 * Sector + 20], 33);
        Put32LE(&image[32 * Sector + 24], Sector);
        uint8_t* entry = &image[33 * Sector];
        Put32LE(entry + 4, 34);
        Put32LE(entry + 8, uint32_t(xex.size()));
        entry[12] = 0x80;
        entry[13] = 11;
        memcpy(entry + 14, "default.xex", 11);
        memcpy(&image[34 * Sector], xex.data(), xex.size());
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(image.data()), std::streamsize(image.size()));
    }

    // default.xex and the `extra` smallest other files of a real disc: a copy that is quick and still a game for the disc check.
    gamedata::Source SubsetOf(DiscFileSystem& disc, size_t extra, const std::string& xexSha256, std::map<std::string, int>& reads)
    {
        std::vector<std::pair<std::string, const DiscFileSystem::Entry*>> others;
        gamedata::Source source;
        source.xexSha256 = xexSha256;
        for (const auto& [path, entry] : disc.Files())
        {
            if (path == "default.xex")
                source.files.push_back({ path, entry->size });
            else
                others.emplace_back(path, entry);
        }
        std::sort(others.begin(), others.end(), [](const auto& a, const auto& b) { return a.second->size < b.second->size; });
        for (size_t i = 0; i < others.size() && i < extra; i++)
            source.files.push_back({ others[i].first, others[i].second->size });
        source.read = [&disc, &reads](const gamedata::SourceFile& file, uint64_t position, void* buffer, size_t size) -> size_t {
            if (position == 0)
                reads[file.path]++;
            const DiscFileSystem::Entry* entry = disc.Find(file.path);
            return entry == nullptr ? 0 : disc.Read(*entry, position, buffer, uint32_t(size));
        };
        return source;
    }

    bool SameFiles(DiscFileSystem& disc, DiscFileSystem& copy, const gamedata::Source& source)
    {
        for (const gamedata::SourceFile& file : source.files)
            if (disc.ReadAll(file.path) != copy.ReadAll(file.path))
                return false;
        return true;
    }

    void EngineChecks(const fs::path& scratch, const std::string& ssaIso)
    {
        using gamedata::State;
        using Outcome = gamedata::CopyResult::Outcome;
        const GameInfo& ssa = *FindGame("ssa");
        const GameInfo* giants = FindGame("giants");
        fs::create_directories(scratch);
        SetEnv("SKYLANDERS_DATA", (scratch / "data").string());
        const fs::path folder = gamedata::Folder("ssa");
        if (!UnderScratch(scratch, folder))
        {
            g_failures++;
            return;
        }

        DiscFileSystem disc;
        EXPECT(disc.Open(ssaIso.c_str()));
        std::map<std::string, int> reads;
        gamedata::Source source = SubsetOf(disc, 6, ssa.releases[0].defaultXexSha256, reads);
        EXPECT(source.files.size() >= 3);

        // Nothing copied: the disc image is the source.
        GameSourcePick pick = PickGameSource(ssa, ssaIso);
        EXPECT(!pick.copied && pick.path == ssaIso && pick.copy.state == State::None);

        // Happy path.
        EXPECT(gamedata::Copy(source, folder).outcome == Outcome::Done);
        gamedata::Status status = gamedata::Inspect(folder);
        EXPECT(status.state == State::Complete && status.files == source.files.size() && status.xexSha256 == ssa.releases[0].defaultXexSha256);
        GameSourceCheck check = CheckGameSource(datadir::Text(folder), ssa);
        EXPECT(check.status == GameSourceStatus::Ok && check.xexSha256 == ssa.releases[0].defaultXexSha256);
        DiscFileSystem copy;
        EXPECT(copy.Open(folder.string().c_str()) && SameFiles(disc, copy, source));
        pick = PickGameSource(ssa, ssaIso);
        EXPECT(pick.copied && pick.path == datadir::Text(folder));
        pick = PickGameSource(ssa, ""); // no disc image at all: the copy is the source
        EXPECT(pick.copied);
        if (giants != nullptr)
        {
            EXPECT(!PickGameSource(*giants, "x.iso").copied); // a game's copy is its own
            GameSourceCheck other = CheckGameSource(datadir::Text(folder), *giants);
            EXPECT(other.status == GameSourceStatus::WrongGame && other.otherGame == "ssa");
        }
        EXPECT(CheckGameSource(datadir::Text(scratch / "data"), ssa).status == GameSourceStatus::Folder); // any other folder is not a game

        // Fallback: a missing file, a file of another size: Damaged, and the disc image is what is used.
        std::string victim;
        for (const gamedata::SourceFile& file : source.files)
            if (file.path != "default.xex" && file.size > 4)
                victim = file.path;
        EXPECT(!victim.empty());
        fs::resize_file(folder / victim, 3);
        status = gamedata::Inspect(folder);
        EXPECT(status.state == State::Damaged && status.problem.find(victim) != std::string::npos);
        pick = PickGameSource(ssa, ssaIso);
        EXPECT(!pick.copied && pick.path == ssaIso && pick.copy.state == State::Damaged);
        fs::remove(folder / victim);
        EXPECT(gamedata::Inspect(folder).state == State::Damaged);
        // Repaired by copying again, reading only what is missing.
        reads.clear();
        EXPECT(gamedata::Copy(source, folder).outcome == Outcome::Done);
        EXPECT(reads.size() == 1 && reads.count(victim) == 1);
        EXPECT(gamedata::Inspect(folder).state == State::Complete);

        // Removal.
        std::string error;
        EXPECT(gamedata::Remove(folder, error) && !fs::exists(folder));
        pick = PickGameSource(ssa, ssaIso);
        EXPECT(!pick.copied && pick.copy.state == State::None);

        // Not enough room: said, and nothing written.
        gamedata::CopyOptions tight;
        tight.freeBytes = 1000;
        gamedata::CopyResult small = gamedata::Copy(source, folder, tight);
        EXPECT(small.outcome == Outcome::NotEnoughSpace && small.available == 1000 && small.needed > 1000);
        EXPECT(CopyFailureText(small).rfind("Not enough space", 0) == 0);
        EXPECT(gamedata::Inspect(folder).state == State::None && !fs::exists(folder / "default.xex"));

        // Interrupted after two files, then resumed: the first two are not read again.
        std::atomic<bool> cancel{ false };
        std::map<std::string, int> started;
        gamedata::Source interrupted = source;
        interrupted.read = [&](const gamedata::SourceFile& file, uint64_t position, void* buffer, size_t size) -> size_t {
            if (position == 0 && started[file.path]++ == 0 && started.size() == 3)
                cancel = true;
            return source.read(file, position, buffer, size);
        };
        gamedata::CopyOptions stoppable;
        stoppable.cancel = &cancel;
        EXPECT(gamedata::Copy(interrupted, folder, stoppable).outcome == Outcome::Cancelled);
        status = gamedata::Inspect(folder);
        EXPECT(status.state == State::Partial && status.present > 0 && status.missing > 0);
        pick = PickGameSource(ssa, ssaIso);
        EXPECT(!pick.copied && pick.path == ssaIso && pick.copy.state == State::Partial); // until it is finished, the disc image
        reads.clear();
        cancel = false;
        EXPECT(gamedata::Copy(source, folder, stoppable).outcome == Outcome::Done);
        EXPECT(reads.size() == source.files.size() - 2); // the two finished files were not read
        EXPECT(gamedata::Inspect(folder).state == State::Complete);
        EXPECT(gamedata::Remove(folder, error));
        printf("Copy engine on the disc's small files: happy path, space, interrupted and resumed, damaged, removed\n");
    }

    // The window on stand-in discs: the wrong-disc button, the copy's states, playing with no disc image.
    void WindowChecks(const fs::path& scratch, const std::string& ssaIso, const std::string& giantsIso)
    {
        fs::create_directories(scratch);
        DiscFileSystem disc;
        EXPECT(disc.Open(ssaIso.c_str()));
        std::vector<uint8_t> xex = disc.ReadAll("default.xex");
        fs::path standIn = scratch / "stand-in-ssa.iso";
        WriteStandInDisc(standIn, xex);
        GameSourceCheck standInCheck = CheckGameSource(standIn.string(), *FindGame("ssa"));
        if (standInCheck.status != GameSourceStatus::Ok)
            fprintf(stderr, "stand-in disc: %s (xex %zu bytes)\n", standInCheck.message.c_str(), xex.size());
        EXPECT(standInCheck.status == GameSourceStatus::Ok);

        fs::create_directories(scratch / "shots");
        SetEnv("SDL_VIDEO_DRIVER", "offscreen");
        SetEnv("SDL_AUDIO_DRIVER", "dummy");
        SetEnv("SKYLANDERS_DATA", (scratch / "window-data").string());
        SetEnv("SKY_LAUNCHER_DATA", (scratch / "window-launcher-data").string());
        SetEnv("SKY_COLLECTION", (scratch / "window-collection").string());
        SetEnv("SKY_LAUNCHER_NO_UPDATE_CHECK", "1");
        SetEnv("FAKE_SSABUILD_DELAY", "0.01");
        UnsetEnv("FAKE_SSABUILD_FAIL");
        UnsetEnv("FAKE_SSABUILD_HOLD");
        // The other window checks leave their switches set (the first script that matches runs): only this one may be.
        for (const char* other : { "SWITCH", "SIDEBAR", "INSTALL", "UPDATE", "NAV", "COLLECTION", "SEATX", "BUTTONS", "DROP" })
            UnsetEnv((std::string("SKY_LAUNCHER_TEST_") + other).c_str());
        if (!UnderScratch(scratch, gamedata::Folder("ssa")) || !UnderScratch(scratch, datadir::Root()) || !UnderScratch(scratch, LauncherDataFolder()))
        {
            g_failures++;
            return;
        }
        SetEnv("SKY_LAUNCHER_SHOTS", (scratch / "shots").string());
        SetEnv("SKY_LAUNCHER_TEST_COPY", "1");
        if (!giantsIso.empty())
            SetEnv("SKY_LAUNCHER_TEST_COPY_GIANTS_ISO", giantsIso);
        std::string ini = (scratch / "copy.ini").string();
        SetEnv("SKY_LAUNCHER_INI", ini);
        LauncherConfig config;
        config["launcher"]["kit"] = SKYLAUNCHER_FAKE_KIT;
        config["giants"]["iso"] = standIn.string(); // the wrong disc for Giants
        {
            launcher_ui::App app(*FindGame("ssa"), ini, config);
            app.Run();
            EXPECT(app.TestFailures() == 0);
        }
        EXPECT(config["ssa"]["iso"] == standIn.string() && config["giants"]["iso"] == giantsIso); // SSA got the disc Giants was given
        UnsetEnv("SKY_LAUNCHER_TEST_COPY");
        UnsetEnv("SKY_LAUNCHER_TEST_COPY_GIANTS_ISO");
        SetActiveGame("ssa");
        printf("Copy and wrong-disc button in the window: %s\n", g_failures == 0 ? "ok" : "FAILED");
    }
}

// SKY_LAUNCHER_TEST_COPY_REAL=1 (with SKYLANDERS_DATA on a scratch folder): copies the whole real discs the way the launcher does, checks
// them, and leaves them there for a game run with no disc image (docs/launcher_ui.md "Game data").
static void RealCopyChecks(const std::string& ssaIso, const std::string& giantsIso)
{
    fs::path data = datadir::Root();
    printf("Real copies into %s\n", data.string().c_str());
    for (auto [id, image] : { std::pair<const char*, std::string>{ "ssa", ssaIso }, std::pair<const char*, std::string>{ "giants", giantsIso } })
    {
        if (image.empty())
            continue;
        const GameInfo& game = *FindGame(id);
        GameSourceCheck disc = CheckGameSource(image, game);
        EXPECT(disc.status == GameSourceStatus::Ok);
        GameCopyJob job;
        auto began = SDL_GetTicks();
        job.Start(game, image, disc.xexSha256);
        while (!job.Done())
        {
            SDL_Delay(2000);
            printf("  %s: %.0f %%\n", id, job.Fraction() * 100);
            fflush(stdout);
        }
        gamedata::CopyResult result = job.Collect();
        EXPECT(result.outcome == gamedata::CopyResult::Outcome::Done);
        gamedata::Status status = gamedata::Inspect(gamedata::Folder(id));
        GameSourceCheck copy = CheckGameSource(datadir::Text(gamedata::Folder(id)), game);
        EXPECT(status.state == gamedata::State::Complete && copy.status == GameSourceStatus::Ok && copy.xexSha256 == disc.xexSha256);
        printf("  %s: %zu files, %.2f GB copied and verified in %.0f s\n", id, status.files, double(status.total) / 1e9, double(SDL_GetTicks() - began) / 1000.0);
    }
}

int CopyChecks(const fs::path& scratch, const std::string& ssaIso, const std::string& giantsIso)
{
    if (LauncherEnv("SKY_LAUNCHER_TEST_COPY_REAL") != nullptr)
    {
        g_failures = 0;
        RealCopyChecks(ssaIso, giantsIso);
        return g_failures;
    }
    g_failures = 0;
    fs::path root = scratch / "copy";
    fs::remove_all(root);
    fs::create_directories(root);
    EngineChecks(root / "engine", ssaIso);
    WindowChecks(root / "window", ssaIso, giantsIso);
    return g_failures;
}
