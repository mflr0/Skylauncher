// SkylauncherTest's checks for the settings split: each game's own video settings, the command line, the desktop shortcut
// files of every system, and what delete data and uninstall remove. Scratch folders only: nothing here touches the real
// data folder or the real Desktop. Called from launcher_test.cpp's main; returns the number of failures.
#include "launcher/game_install.h"
#include "launcher/game_settings.h"
#include "launcher/launcher.h"
#include "launcher/shortcut.h"
#include "shared/collection/save_backup.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    int g_settingsFailures = 0;

#define SETTINGS_EXPECT(condition) \
    do { if (!(condition)) { fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition); g_settingsFailures++; } } while (0)

    void Write(const fs::path& file, const std::string& data)
    {
        fs::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary | std::ios::trunc) << data;
    }

    std::string Read(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }

    // The first bytes of a PNG: its signature and an IHDR of this size (all the shortcut files look at).
    std::vector<uint8_t> PngOf(uint32_t size)
    {
        std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R' };
        for (uint32_t value : { size, size })
            for (int shift = 24; shift >= 0; shift -= 8)
                png.push_back(uint8_t(value >> shift));
        for (int i = 0; i < 40; i++)
            png.push_back(uint8_t(i));
        return png;
    }

    // The launcher reads some variables through SDL and the shared folder code through the C library: set both.
    void SetEnv(const char* name, const std::string& value)
    {
        if (value.empty())
        {
            SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), name);
            SDL_unsetenv_unsafe(name);
        }
        else
        {
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value.c_str(), true);
            SDL_setenv_unsafe(name, value.c_str(), 1);
        }
    }

    // ---- Per-game video settings ---------------------------------------------------------------------------------------

    void VideoChecks()
    {
        LauncherConfig config;
        config["video"]["fps_limit"] = "60";
        config["video"]["vsync"] = "0";
        config["video"]["custom"] = "kept"; // not a game key: never copied
        SeedGameVideo(config, "ssa");
        SeedGameVideo(config, "giants");
        SETTINGS_EXPECT(config["ssa.video"]["fps_limit"] == "60" && config["giants.video"]["vsync"] == "0");
        SETTINGS_EXPECT(config["ssa.video"].count("custom") == 0);

        // Giants gets its own limit and no vsync line; SSA stays as it was.
        config["giants.video"]["fps_limit"] = "30";
        config["giants.video"].erase("vsync");
        SeedGameVideo(config, "giants"); // never overwrites a game's copy
        SETTINGS_EXPECT(config["giants.video"]["fps_limit"] == "30");
        ApplyGameVideo(config, "giants");
        SETTINGS_EXPECT(config["video"]["fps_limit"] == "30" && config["video"].count("vsync") == 0 && config["video"]["custom"] == "kept");
        ApplyGameVideo(config, "ssa");
        SETTINGS_EXPECT(config["video"]["fps_limit"] == "60" && config["video"]["vsync"] == "0");

        // The runtime changes [video] while the game runs: kept as that game's, the other game's copy untouched.
        config["video"]["resolution_scale"] = "3";
        StoreGameVideo(config, "ssa");
        SETTINGS_EXPECT(config["ssa.video"]["resolution_scale"] == "3" && config["giants.video"].count("resolution_scale") == 0);
        SETTINGS_EXPECT(GameVideoSection("giants") == "giants.video" && GameVideoKeys().size() == 9);

        // Through the settings file, sections with a dot included.
        fs::path scratch = fs::temp_directory_path() / "skylauncher-video-test.ini";
        SETTINGS_EXPECT(SaveLauncherConfig(scratch.string(), config));
        LauncherConfig back = LoadLauncherConfig(scratch.string());
        SETTINGS_EXPECT(back["giants.video"]["fps_limit"] == "30" && back["ssa.video"]["resolution_scale"] == "3");
        fs::remove(scratch);
        printf("Settings: each game's video settings are its own, [video] follows the game that plays\n");
    }

    // ---- The command line ------------------------------------------------------------------------------------------------

    void ArgumentChecks()
    {
        const char* none[] = { "Skylauncher" };
        LauncherArguments empty = ParseLauncherArguments(1, none);
        SETTINGS_EXPECT(empty.game.empty() && !empty.play && !empty.help && empty.unknown.empty());
        const char* shortcut[] = { "Skylauncher", "--game", "giants", "--play" };
        LauncherArguments play = ParseLauncherArguments(4, shortcut);
        SETTINGS_EXPECT(play.game == "giants" && play.play && !play.help);
        const char* equals[] = { "Skylauncher", "--game=ssa", "--bogus", "-h", "--game" };
        LauncherArguments mixed = ParseLauncherArguments(5, equals);
        SETTINGS_EXPECT(mixed.game == "ssa" && mixed.help && !mixed.play);
        SETTINGS_EXPECT(mixed.unknown.size() == 2 && mixed.unknown[0] == "--bogus" && mixed.unknown[1] == "--game"); // no value: unknown
        SETTINGS_EXPECT(std::string(LauncherUsage()).find("--play") != std::string::npos);
        printf("Command line: --game, --game=, --play, --help; the rest listed\n");
    }

    // ---- The desktop shortcut --------------------------------------------------------------------------------------------

    void ShortcutChecks(const fs::path& scratch)
    {
        SETTINGS_EXPECT(ShortcutFileName("Skylanders: Giants") == "Skylanders Giants");
        SETTINGS_EXPECT(ShortcutFileName("Skylanders: Spyro's Adventure") == "Skylanders Spyro's Adventure");
        SETTINGS_EXPECT(ShortcutFileName("  A/B*C?  ") == "A B C" && ShortcutFileName(":::") == "Skylanders");
        SETTINGS_EXPECT(ShortcutArguments("giants") == "--game giants --play");
        SETTINGS_EXPECT(ShortcutFileName("Skylanders: Swap Force") == "Skylanders Swap Force" && ShortcutArguments("swapforce") == "--game swapforce --play");
        SETTINGS_EXPECT(GameVideoSection("swapforce") == "swapforce.video");
        SETTINGS_EXPECT(ShellQuote("/Applications/My Apps/Sky'launcher") == "'/Applications/My Apps/Sky'\\''launcher'");
        SETTINGS_EXPECT(DesktopExecQuote("/opt/sky launcher/run") == "\"/opt/sky launcher/run\"");
        SETTINGS_EXPECT(DesktopExecQuote("/a\"b`c$d%e") == "\"/a\\\"b\\`c\\$d%%e\"");
        SETTINGS_EXPECT(DesktopExecQuote("C:\\x") == "\"C:\\\\\\\\x\"");

        // The icon formats hold the PNG as it is.
        std::vector<uint8_t> png = PngOf(64);
        std::vector<uint8_t> icns = MacIcns(png);
        SETTINGS_EXPECT(icns.size() == 16 + png.size() && std::string(icns.begin(), icns.begin() + 4) == "icns" &&
            std::string(icns.begin() + 8, icns.begin() + 12) == "icp6");
        SETTINGS_EXPECT(icns[4] == 0 && icns[5] == 0 && icns[6] == 0 && icns[7] == icns.size() && icns[15] == icns.size() - 8); // big-endian sizes
        SETTINGS_EXPECT(std::equal(png.begin(), png.end(), icns.begin() + 16));
        std::vector<uint8_t> big = MacIcns(PngOf(256));
        SETTINGS_EXPECT(std::string(big.begin() + 8, big.begin() + 12) == "ic08");
        SETTINGS_EXPECT(MacIcns(PngOf(100)).empty() && MacIcns({ 1, 2, 3 }).empty());
        std::vector<uint8_t> ico = WindowsIco(png);
        SETTINGS_EXPECT(ico.size() == 22 + png.size() && ico[0] == 0 && ico[1] == 0 && ico[2] == 1 && ico[3] == 0 && ico[4] == 1 && ico[5] == 0);
        SETTINGS_EXPECT(ico[6] == 64 && ico[7] == 64 && ico[10] == 1 && ico[12] == 32 && ico[14] == png.size() && ico[18] == 22);
        SETTINGS_EXPECT(std::equal(png.begin(), png.end(), ico.begin() + 22));
        SETTINGS_EXPECT(WindowsIco(PngOf(256))[6] == 0 && WindowsIco({ 1, 2, 3 }).empty()); // 0 means 256

        // The text files.
        std::string script = MacLaunchScript("/Applications/Skylauncher.app/Contents/MacOS/Skylauncher", "giants");
        SETTINGS_EXPECT(script.starts_with("#!/bin/sh\n") &&
            script.find("exec '/Applications/Skylauncher.app/Contents/MacOS/Skylauncher' --game giants --play\n") != std::string::npos);
        std::string plist = MacInfoPlist("Skylanders: Giants & co", "giants", true);
        SETTINGS_EXPECT(plist.find("<string>Skylanders: Giants &amp; co</string>") != std::string::npos &&
            plist.find("<key>CFBundleIconFile</key>") != std::string::npos && plist.find("<string>launch</string>") != std::string::npos);
        SETTINGS_EXPECT(MacInfoPlist("X", "ssa", false).find("CFBundleIconFile") == std::string::npos);
        std::string entry = LinuxDesktopEntry("Skylanders: Giants", "/home/p/Skylauncher.AppImage", "giants", "/home/p/.local/share/Skylanders/Giants/shortcut-icon.png");
        SETTINGS_EXPECT(entry.starts_with("[Desktop Entry]\nType=Application\n") && entry.find("\nName=Skylanders: Giants\n") != std::string::npos &&
            entry.find("\nExec=\"/home/p/Skylauncher.AppImage\" --game giants --play\n") != std::string::npos &&
            entry.find("\nIcon=/home/p/.local/share/Skylanders/Giants/shortcut-icon.png\n") != std::string::npos &&
            entry.find("\nTerminal=false\n") != std::string::npos && entry.find("Categories=Game;") != std::string::npos);
        SETTINGS_EXPECT(LinuxDesktopEntry("X", "/x", "ssa", {}).find("Icon=") == std::string::npos);

        // The files on disk: a Desktop of the test's own, replaced when made again, the script really starting the launcher.
        SetEnv("SKYLAUNCHER_DESKTOP", (scratch / "desktop").string());
        SETTINGS_EXPECT(DesktopFolder() == scratch / "desktop");
        SetEnv("SKYLAUNCHER_EXE", "/somewhere/Skylauncher");
        SETTINGS_EXPECT(SelfExecutable() == fs::path("/somewhere/Skylauncher"));
        SetEnv("SKYLAUNCHER_EXE", "");

        ShortcutSpec spec;
        spec.gameId = "giants";
        spec.gameName = "Skylanders: Giants";
        spec.launcher = scratch / "fake launcher" / "Skylauncher"; // a folder name with a space
        spec.iconPng = png;
        spec.folder = scratch / "desktop";
        spec.iconFolder = scratch / "gamefolder";
        Write(spec.launcher, "#!/bin/sh\necho \"$@\" > \"" + (scratch / "args.txt").string() + "\"\n");
        fs::permissions(spec.launcher, fs::perms::owner_all);

        ShortcutResult app = WriteMacApp(spec);
        SETTINGS_EXPECT(app.ok && app.file == scratch / "desktop" / "Skylanders Giants.app");
        SETTINGS_EXPECT(Read(app.file / "Contents" / "Info.plist") == MacInfoPlist("Skylanders: Giants", "giants", true));
        SETTINGS_EXPECT(Read(app.file / "Contents" / "MacOS" / "launch") == MacLaunchScript(spec.launcher, "giants"));
        SETTINGS_EXPECT(Read(app.file / "Contents" / "Resources" / "icon.icns") == std::string(icns.begin(), icns.end()));
#ifndef _WIN32
        auto perms = fs::status(app.file / "Contents" / "MacOS" / "launch").permissions();
        SETTINGS_EXPECT((perms & fs::perms::owner_exec) != fs::perms::none);
        std::string command = "'" + (app.file / "Contents" / "MacOS" / "launch").string() + "'";
        SETTINGS_EXPECT(std::system(command.c_str()) == 0);
        SETTINGS_EXPECT(Read(scratch / "args.txt") == "--game giants --play\n"); // the shortcut started the launcher with its options
#endif
        Write(app.file / "stale", "from an older shortcut");
        SETTINGS_EXPECT(WriteMacApp(spec).ok && !fs::exists(app.file / "stale")); // made again: replaced, not merged
        ShortcutSpec noIcon = spec;
        noIcon.iconPng.clear();
        SETTINGS_EXPECT(WriteMacApp(noIcon).ok && !fs::exists(app.file / "Contents" / "Resources" / "icon.icns") &&
            Read(app.file / "Contents" / "Info.plist").find("CFBundleIconFile") == std::string::npos);

        ShortcutResult desktop = WriteLinuxDesktop(spec);
        SETTINGS_EXPECT(desktop.ok && desktop.file == scratch / "desktop" / "skylanders-giants.desktop");
        SETTINGS_EXPECT(Read(desktop.file) == LinuxDesktopEntry("Skylanders: Giants", spec.launcher, "giants", scratch / "gamefolder" / "shortcut-icon.png"));
        SETTINGS_EXPECT(Read(scratch / "gamefolder" / "shortcut-icon.png") == std::string(png.begin(), png.end()));
#ifndef _WIN32
        SETTINGS_EXPECT((fs::status(desktop.file).permissions() & fs::perms::owner_exec) != fs::perms::none);
#endif

        // Where it can't write, it says so.
        ShortcutSpec none = spec;
        none.folder.clear();
        SETTINGS_EXPECT(!CreateDesktopShortcut(none).ok && !CreateDesktopShortcut(none).error.empty());
        none = spec;
        none.launcher.clear();
        SETTINGS_EXPECT(!CreateDesktopShortcut(none).ok);
        SetEnv("SKYLAUNCHER_DESKTOP", "");
        printf("Shortcut: macOS .app (script run), Linux .desktop, Windows .ico, names and quoting checked\n");
    }

    // ---- Delete data, uninstall -------------------------------------------------------------------------------------------

    // A built game and its data, as docs/install.md lays them out, in a folder of the test's own.
    GameFolders Populate(const fs::path& root)
    {
        fs::remove_all(root);
        GameFolders folders;
        folders.game = root / "Giants";
        folders.saves = folders.game / "saves";
        Write(folders.game / "SGRecomp.app" / "Contents" / "MacOS" / "SGRecomp", std::string(1000, 'x'));
        Write(folders.game / "install.log", std::string(10, 'l'));
        Write(folders.game / "install-logs" / "compile.txt", std::string(20, 'c'));
        Write(folders.saves / "a" / "GLOBAL.DAT", std::string(100, 's'));
        Write(folders.game / "saves-backup" / "20260101-000000" / "a" / "GLOBAL.DAT", std::string(100, 'b'));
        Write(folders.game / "portal.txt", std::string(5, 'p'));
        Write(folders.game / "pipelines.bin", std::string(300, 'c'));
        Write(folders.game / "launcher-cache" / "title_icon.png", std::string(50, 'i'));
        Write(folders.game / "portraits" / "p.png", std::string(40, 'q'));
        Write(folders.game / "SSARecomp.log", std::string(7, 'g'));
        Write(folders.game / "textures" / "t.dds", std::string(60, 't'));
        Write(folders.game / "figures" / "f.bin", std::string(8, 'f'));
        Write(folders.game / "gamedata" / "default.xex", std::string(4000, 'x')); // the copied game data
        return folders;
    }

    void DataChecks(const fs::path& scratch)
    {
        // The seat of the data folder these paths come from: the helpers answer for the folders they are given.
        SetEnv("SSA_SAVE_DIR", "");
        GameFolders folders = Populate(scratch / "data");

        GameDataSizes sizes = MeasureGameData(folders);
        SETTINGS_EXPECT(sizes.saves == 100 + 100 + 5);
        SETTINGS_EXPECT(sizes.caches == 300 + 50 + 40 + 7);
        SETTINGS_EXPECT(sizes.installed == 1000 + 10 + 20); // the build and its logs; the textures and figures are not its

        // Caches: only the caches go.
        DataResult cleared = ClearGameCaches(folders);
        SETTINGS_EXPECT(cleared.ok && cleared.freed == 300 + 50 + 40 + 7);
        SETTINGS_EXPECT(!fs::exists(folders.game / "pipelines.bin") && !fs::exists(folders.game / "launcher-cache") &&
            !fs::exists(folders.game / "portraits") && !fs::exists(folders.game / "SSARecomp.log"));
        SETTINGS_EXPECT(fs::exists(folders.saves / "a" / "GLOBAL.DAT") && fs::exists(folders.game / "SGRecomp.app") &&
            fs::exists(folders.game / "install.log") && fs::exists(folders.game / "textures" / "t.dds"));
        SETTINGS_EXPECT(MeasureGameData(folders).caches == 0 && ClearGameCaches(folders).ok); // twice: nothing left, no error

        // A running game: nothing is deleted.
        folders = Populate(scratch / "data");
        savebackup::SetGameRunning(true, folders.saves);
        SETTINGS_EXPECT(GameIsRunning(folders));
        DataResult refused = DeleteGameData(folders);
        SETTINGS_EXPECT(!refused.ok && refused.freed == 0 && refused.error.find("running") != std::string::npos);
        SETTINGS_EXPECT(!ClearGameCaches(folders).ok);
        LauncherConfig config;
        SETTINGS_EXPECT(!UninstallGame(folders, config).ok);
        SETTINGS_EXPECT(fs::exists(folders.saves / "a" / "GLOBAL.DAT") && fs::exists(folders.game / "SGRecomp.app") && fs::exists(folders.game / "pipelines.bin"));
        savebackup::SetGameRunning(false, folders.saves);
        SETTINGS_EXPECT(!GameIsRunning(folders));

        // Delete data: the saves, the backups, the seats and the caches; the game, the textures and the figures stay.
        DataResult deleted = DeleteGameData(folders);
        SETTINGS_EXPECT(deleted.ok && deleted.freed == 205 + 397);
        SETTINGS_EXPECT(!fs::exists(folders.saves) && !fs::exists(folders.game / "saves-backup") && !fs::exists(folders.game / "portal.txt") &&
            !fs::exists(folders.game / "pipelines.bin") && !fs::exists(folders.game / "portraits"));
        SETTINGS_EXPECT(fs::exists(folders.game / "SGRecomp.app" / "Contents" / "MacOS" / "SGRecomp") && fs::exists(folders.game / "install.log") &&
            fs::exists(folders.game / "textures" / "t.dds") && fs::exists(folders.game / "figures" / "f.bin"));

        // Uninstall: the build goes with its logs, what the player made stays, the settings forget the build.
        folders = Populate(scratch / "data");
        config.clear();
        config["install.aaaa"]["runtime"] = (folders.game / "SGRecomp.app").string();
        config["install.aaaa"]["kit"] = "fake-1";
        config["install.bbbb"]["runtime"] = (scratch / "elsewhere" / "SSARecomp").string(); // another game's, not in this folder
        config["giants"]["iso"] = "/discs/giants.iso";
        config["giants.video"]["fps_limit"] = "30";
        DataResult uninstalled = UninstallGame(folders, config);
        SETTINGS_EXPECT(uninstalled.ok && uninstalled.freed >= 1000 + 10 + 20);
        SETTINGS_EXPECT(!fs::exists(folders.game / "SGRecomp.app") && !fs::exists(folders.game / "install.log") &&
            !fs::exists(folders.game / "install-logs") && !fs::exists(folders.game / "pipelines.bin") && !fs::exists(folders.game / "SSARecomp.log"));
        SETTINGS_EXPECT(fs::exists(folders.saves / "a" / "GLOBAL.DAT") && fs::exists(folders.game / "saves-backup") && fs::exists(folders.game / "portal.txt") &&
            fs::exists(folders.game / "textures" / "t.dds") && fs::exists(folders.game / "figures" / "f.bin") &&
            fs::exists(folders.game / "launcher-cache" / "title_icon.png") && fs::exists(folders.game / "portraits" / "p.png"));
        SETTINGS_EXPECT(fs::exists(folders.game / "gamedata" / "default.xex")); // the copy stays: it is the disc's files, removed from the Disc tab
        SETTINGS_EXPECT(config.count("install.aaaa") == 0 && config.count("install.bbbb") == 1 && config["giants"]["iso"] == "/discs/giants.iso" &&
            config["giants.video"]["fps_limit"] == "30");

        // A game with no folder, or none to name: refused, and nothing outside is touched.
        GameFolders nothing;
        SETTINGS_EXPECT(!DeleteGameData(nothing).ok && !UninstallGame(nothing, config).ok && MeasureGameData(nothing).saves == 0);
        SETTINGS_EXPECT(fs::exists(scratch / "data" / "Giants" / "textures" / "t.dds"));

        // FoldersOf names the game's folder in the data folder, and SSA_SAVE_DIR moves the saves.
        SetEnv("SKYLANDERS_DATA", (scratch / "root").string());
        SetEnv("SKYLAUNCHER_DATA", "");
        SETTINGS_EXPECT(FoldersOf("giants").game == scratch / "root" / "Giants" && FoldersOf("giants").saves == scratch / "root" / "Giants" / "saves");
        SETTINGS_EXPECT(FoldersOf("ssa").game == scratch / "root" / "SSA");
        SetEnv("SSA_SAVE_DIR", (scratch / "elsewhere-saves").string());
        SETTINGS_EXPECT(FoldersOf("ssa").saves == scratch / "elsewhere-saves");
        SetEnv("SSA_SAVE_DIR", "");
        printf("Delete data and uninstall: sizes, caches only, refused while the game runs, what stays, on a scratch folder\n");
    }
}

int SettingsChecks(const fs::path& scratch)
{
    fs::create_directories(scratch);
    VideoChecks();
    ArgumentChecks();
    ShortcutChecks(scratch / "shortcut");
    DataChecks(scratch / "datachecks");
    return g_settingsFailures;
}
