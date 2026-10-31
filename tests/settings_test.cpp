// ArkeyanLauncherTest's checks for the settings split: each game's own video settings, the command line, the desktop shortcut
// files of every system, and what delete data and uninstall remove. Scratch folders only: nothing here touches the real
// data folder or the real Desktop. Called from launcher_test.cpp's main; returns the number of failures.
#include "test_env.h"
#include "launcher/env.h"
#include "launcher/game_install.h"
#include "launcher/game_keys.h"
#include "launcher/i18n.h"
#include "launcher/game_settings.h"
#include "launcher/i18n.h"
#include "launcher/launcher.h"
#include "launcher/shortcut.h"
#include "launcher/ui/launcher_app.h"
#include "shared/collection/datadir.h"
#include "shared/collection/save_backup.h"
#include <SDL3/SDL.h>
#include <stb_image_write.h>
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
            TestUnsetEnv(name);
        }
        else
        {
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value.c_str(), true);
            TestSetEnv(name, value.c_str());
        }
    }

    // ---- Frame rates of an older file ----------------------------------------------------------------------------------------

    void FrameRateMigration()
    {
        LauncherConfig config;
        config["video"]["fps_limit"] = "0";
        config["ssa.video"]["fps_limit"] = "144";
        config["giants.video"]["fps_limit"] = "30";
        config["swapforce.video"]["fps_limit"] = "60";
        config["other"]["fps_limit"] = "240"; // not a video section
        MigrateFrameRates(config);
        SETTINGS_EXPECT(config["video"]["fps_limit"] == "60" && config["ssa.video"]["fps_limit"] == "60");
        SETTINGS_EXPECT(config["giants.video"]["fps_limit"] == "30" && config["swapforce.video"]["fps_limit"] == "60");
        SETTINGS_EXPECT(config["other"]["fps_limit"] == "240" && config["launcher"]["frame_rates"] == "1");
        // A choice made afterwards stays, through a save and a load.
        config["ssa.video"]["fps_limit"] = "120";
        config["video"]["fps_limit"] = "0";
        fs::path file = fs::temp_directory_path() / "skylauncher-frame-rates.ini";
        SETTINGS_EXPECT(SaveLauncherConfig(datadir::Text(file), config));
        LauncherConfig again = LoadLauncherConfig(datadir::Text(file));
        MigrateFrameRates(again);
        SETTINGS_EXPECT(again["ssa.video"]["fps_limit"] == "120" && again["video"]["fps_limit"] == "0");
        fs::remove(file);
        // A file with no marker and no frame rate gets the marker only.
        LauncherConfig fresh;
        MigrateFrameRates(fresh);
        SETTINGS_EXPECT(fresh["launcher"]["frame_rates"] == "1" && fresh.count("video") == 0);
    }

    // ---- Giants' cap ---------------------------------------------------------------------------------------------------------

    void GiantsCap()
    {
        SETTINGS_EXPECT(MaxFrameRate("giants") == 60 && MaxFrameRate("ssa") == 0 && MaxFrameRate("swapforce") == 0);
        SETTINGS_EXPECT(launcher_ui::FrameRateChoices("giants").size() == 3 && std::string(launcher_ui::FrameRateChoices("giants").back().value) == "60");
        SETTINGS_EXPECT(launcher_ui::FrameRateChoices("ssa").size() == std::size(launcher_ui::FpsChoices) && launcher_ui::FrameRateChoices("swapforce").size() == std::size(launcher_ui::FpsChoices));
        // A file from the version before the cap (frame rates already migrated): Giants' higher values become 60, once; the others stay.
        for (const char* old : { "0", "120", "144", "240" })
        {
            LauncherConfig config;
            config["launcher"]["frame_rates"] = "1";
            config["giants.video"]["fps_limit"] = old;
            config["ssa.video"]["fps_limit"] = old;
            config["swapforce.video"]["fps_limit"] = old;
            MigrateFrameRates(config);
            SETTINGS_EXPECT(config["giants.video"]["fps_limit"] == "60" && config["ssa.video"]["fps_limit"] == old && config["swapforce.video"]["fps_limit"] == old);
            config["giants.video"]["fps_limit"] = "30";
            MigrateFrameRates(config);
            SETTINGS_EXPECT(config["giants.video"]["fps_limit"] == "30" && config["launcher"]["giants_cap"] == "1");
        }
        LauncherConfig kept; // a Giants copy already at 30 or 60, or none, is left as it is
        kept["giants.video"]["fps_limit"] = "30";
        MigrateFrameRates(kept);
        SETTINGS_EXPECT(kept["giants.video"]["fps_limit"] == "30");
        LauncherConfig none;
        MigrateFrameRates(none);
        SETTINGS_EXPECT(none.count("giants.video") == 0);
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

        // Trap Team has its own controls: LT is its tag team, RB an attack like B, and LB (which fires nothing) is not among them.
        bool tagTeam = false, rightBumper = false, leftBumper = false;
        for (const GameKey& key : GameKeys("trapteam"))
        {
            tagTeam = tagTeam || (std::string_view(key.action) == "lt" && std::string_view(key.label) == "Switch Skylander / Villain");
            rightBumper = rightBumper || (std::string_view(key.action) == "rb" && std::string_view(key.label) == "Attack 2");
            leftBumper = leftBumper || std::string_view(key.action) == "lb";
        }
        SETTINGS_EXPECT(tagTeam && rightBumper && !leftBumper);
        SeedGameVideo(config, "trapteam");
        SETTINGS_EXPECT(config["trapteam.video"]["fps_limit"] == "30" && config["trapteam.video"]["vsync"] == "0");
        config["trapteam.video"]["fps_limit"] = "60";
        SeedGameVideo(config, "trapteam");
        SETTINGS_EXPECT(config["trapteam.video"]["fps_limit"] == "60");
        LauncherConfig freshTrapTeam;
        SeedGameVideo(freshTrapTeam, "trapteam");
        SETTINGS_EXPECT(freshTrapTeam["trapteam.video"]["fps_limit"] == "30");

        // Giants gets its own limit and no vsync line; SSA stays as it was.
        config["giants.video"]["fps_limit"] = "30";
        config["giants.video"].erase("vsync");
        SeedGameVideo(config, "giants"); // never overwrites a game's copy
        SETTINGS_EXPECT(config["giants.video"]["fps_limit"] == "30");
        ApplyGameVideo(config, "giants");
        SETTINGS_EXPECT(config["video"]["fps_limit"] == "30" && config["video"].count("vsync") == 0 && config["video"]["custom"] == "kept");
        ApplyGameVideo(config, "ssa");
        SETTINGS_EXPECT(config["video"]["fps_limit"] == "60" && config["video"]["vsync"] == "0");
        // A game with no frame rate yet gets its default: 60 for Giants and Swap Force, 30 for SSA.
        config["swapforce.video"]["vsync"] = "1";
        ApplyGameVideo(config, "swapforce");
        SETTINGS_EXPECT(config["video"]["fps_limit"] == "60" && config["video"]["vsync"] == "1");
        LauncherConfig none;
        for (auto [id, rate] : { std::pair<const char*, const char*>{ "ssa", "30" }, { "giants", "60" }, { "swapforce", "60" } })
        {
            none["video"]["fps_limit"] = "120"; // another game's leftover
            ApplyGameVideo(none, id);
            SETTINGS_EXPECT(none["video"]["fps_limit"] == rate && std::string(DefaultFrameRate(id)) == rate);
        }
        // A saved value is left alone, whatever the default: SSA's 60, Giants' 30, Swap Force's "display" and 144.
        for (auto [id, saved] : { std::pair<const char*, const char*>{ "ssa", "60" }, { "giants", "30" }, { "swapforce", "display" }, { "ssa", "144" } })
        {
            LauncherConfig kept;
            kept[GameVideoSection(id)]["fps_limit"] = saved;
            ApplyGameVideo(kept, id);
            SETTINGS_EXPECT(kept["video"]["fps_limit"] == saved && kept[GameVideoSection(id)]["fps_limit"] == saved);
        }

        // The runtime changes [video] while the game runs: kept as that game's, the other game's copy untouched.
        config["video"]["resolution_scale"] = "3";
        StoreGameVideo(config, "ssa");
        SETTINGS_EXPECT(config["ssa.video"]["resolution_scale"] == "3" && config["giants.video"].count("resolution_scale") == 0);
        SETTINGS_EXPECT(GameVideoSection("giants") == "giants.video" && GameVideoKeys().size() == 12);

        // Original resolution (Swap Force): kept in [swapforce.video], put in [video] when it plays, absent for the other games.
        config["swapforce.video"]["original_resolution"] = "1";
        ApplyGameVideo(config, "swapforce");
        SETTINGS_EXPECT(config["video"]["original_resolution"] == "1");
        config["video"]["original_resolution"] = "0";
        StoreGameVideo(config, "swapforce");
        SETTINGS_EXPECT(config["swapforce.video"]["original_resolution"] == "0");
        ApplyGameVideo(config, "ssa");
        SETTINGS_EXPECT(config["video"].count("original_resolution") == 0 && config["ssa.video"].count("original_resolution") == 0);

        // Edge smoothing (Swap Force, Trap Team): the same way, a key of the game's copy.
        config["trapteam.video"]["edge_smoothing"] = "off";
        ApplyGameVideo(config, "trapteam");
        SETTINGS_EXPECT(config["video"]["edge_smoothing"] == "off");
        config["video"]["edge_smoothing"] = "light";
        StoreGameVideo(config, "trapteam");
        SETTINGS_EXPECT(config["trapteam.video"]["edge_smoothing"] == "light");
        ApplyGameVideo(config, "ssa");
        SETTINGS_EXPECT(config["video"].count("edge_smoothing") == 0 && config["ssa.video"].count("edge_smoothing") == 0);

        // Through the settings file, sections with a dot included.
        fs::path scratch = fs::temp_directory_path() / "skylauncher-video-test.ini";
        SETTINGS_EXPECT(SaveLauncherConfig(scratch.string(), config));
        LauncherConfig back = LoadLauncherConfig(scratch.string());
        SETTINGS_EXPECT(back["giants.video"]["fps_limit"] == "30" && back["ssa.video"]["resolution_scale"] == "3");
        fs::remove(scratch);
        // A first start: the data folder does not exist yet (a fresh Windows PC), and its path has accents.
        fs::path fresh = fs::temp_directory_path() / "skylauncher-fresh-test" / u8"Données élève";
        fs::remove_all(fresh.parent_path());
        std::string freshIni = datadir::Text(fresh / "launcher.ini");
        SETTINGS_EXPECT(SaveLauncherConfig(freshIni, config));
        SETTINGS_EXPECT(LoadLauncherConfig(freshIni)["giants.video"]["fps_limit"] == "30");
        SETTINGS_EXPECT(!fs::exists(fresh / "launcher.ini.tmp"));
        fs::remove_all(fresh.parent_path());

        // The whole way of the two options a player turns on in a game's page (Video): the page writes the game's copy,
        // PLAY puts it into [video] on disk, which is what the game reads, and the game's end keeps it.
        {
            LauncherConfig page;
            page["video"]["fps_limit"] = "30"; // what an older launcher.ini held: the game's copy is what counts
            SeedGameVideo(page, "ssa");
            page["ssa.video"]["skip_intro_movies"] = "1";
            page["ssa.video"]["fps_limit"] = "60";
            SeedGameVideo(page, "ssa"); // the window seeds again on its next start
            SETTINGS_EXPECT(page["ssa.video"]["skip_intro_movies"] == "1" && page["ssa.video"]["fps_limit"] == "60");
            ApplyGameVideo(page, "ssa");
            fs::path played = fs::temp_directory_path() / "skylauncher-play-test.ini";
            SETTINGS_EXPECT(SaveLauncherConfig(played.string(), page));
            LauncherConfig game = LoadLauncherConfig(played.string()); // the runtime parses the same file
            SETTINGS_EXPECT(game["video"]["skip_intro_movies"] == "1" && game["video"]["fps_limit"] == "60");
            StoreGameVideo(game, "ssa");
            SETTINGS_EXPECT(game["ssa.video"]["skip_intro_movies"] == "1" && game["ssa.video"]["fps_limit"] == "60");
            fs::remove(played);

            // A section that was only looked at (empty) does not stop the seeding from the player's older [video].
            LauncherConfig looked;
            looked["video"]["fps_limit"] = "60";
            looked["ssa.video"];
            SeedGameVideo(looked, "ssa");
            SETTINGS_EXPECT(looked["ssa.video"]["fps_limit"] == "60");
        }
        printf("Settings: each game's video settings are its own, [video] follows the game that plays\n");
    }

    // ---- Per-game keyboard keys ------------------------------------------------------------------------------------------

    bool HasRow(std::string_view game, std::string_view action, std::string_view label)
    {
        for (const GameKey& key : GameKeys(game))
            if (action == key.action && label == key.label)
                return true;
        return false;
    }

    void KeyChecks()
    {
        // Each game lists the actions it uses, named as it names them; a button it ignores has no row.
        SETTINGS_EXPECT(HasRow("ssa", "a", "1st Power / Accept") && HasRow("ssa", "x", "2nd Power") && HasRow("ssa", "y", "Use / Talk"));
        SETTINGS_EXPECT(HasRow("ssa", "b", "Special Power / Back") && HasRow("ssa", "start", "Pause") && HasRow("ssa", "right_up", "Shake / Insert / Swing"));
        SETTINGS_EXPECT(HasRow("giants", "a", "Attack 1 / Accept") && HasRow("giants", "x", "Attack 2") && HasRow("giants", "y", "Action"));
        SETTINGS_EXPECT(HasRow("giants", "b", "Attack 3 / Back") && !HasRow("giants", "right_up", "Shake / Insert / Swing"));
        SETTINGS_EXPECT(HasRow("swapforce", "a", "Jump") && HasRow("swapforce", "x", "Attack 1") && HasRow("swapforce", "back", "Skylanders Stats"));
        SETTINGS_EXPECT(HasRow("swapforce", "b", "Attack 2") && HasRow("swapforce", "y", "Attack 3") && HasRow("swapforce", "lt", "Interact"));
        SETTINGS_EXPECT(GameKeys("giants").size() == 10 && GameKeys("ssa").size() == 14 && GameKeys("swapforce").size() == 14 && GameKeys("nothing").empty());
        for (const char* game : { "ssa", "giants", "swapforce" })
            for (const GameKey& key : GameKeys(game))
            {
                bool known = false;
                for (const char* action : InputActions())
                    known = known || std::string_view(action) == key.action;
                SETTINGS_EXPECT(known && key.pad != nullptr && *key.pad != 0);
            }

        // A key name is shown in the player's language; the stored value stays SDL's English name.
        i18n::SetLanguage("fr");
        SETTINGS_EXPECT(KeyDisplayName("Space") == "Espace" && KeyDisplayName("Backspace") == "Retour arrière" && KeyDisplayName("W") == "W");
        SETTINGS_EXPECT(std::string(i18n::Tr("L stick")) == "stick gauche" && std::string(i18n::Tr("R stick up")) == "stick droit haut");
        i18n::SetLanguage("en");
        SETTINGS_EXPECT(KeyDisplayName("Space") == "Space");

        // Keys are places (scancodes): a captured key stores its place's name, which reads back as the same place, and is
        // shown with the letter the layout prints there (the test's default layout is US: W shows as W).
        for (SDL_Scancode place : { SDL_SCANCODE_W, SDL_SCANCODE_Q, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_RETURN, SDL_SCANCODE_1 })
            SETTINGS_EXPECT(SDL_GetScancodeFromName(CapturedKeyName(place).c_str()) == place);
        SETTINGS_EXPECT(CapturedKeyName(SDL_SCANCODE_W) == "W" && KeyDisplayName("W") == "W" && KeyDisplayName("Return") == "Return");
        for (const char* game : { "ssa", "giants", "swapforce" }) // the defaults are places too
            for (const GameKey& key : GameKeys(game))
                SETTINGS_EXPECT(SDL_GetScancodeFromName(GameKeyName(LauncherConfig{}, game, key).c_str()) == key.fallback);

        // The pad's button names follow the pad in use (a fixed glyph style wins), by place on a Switch.
        auto row = [](std::string_view action) -> const GameKey*
        {
            for (const GameKey& key : GameKeys("ssa"))
                if (action == key.action)
                    return &key;
            return nullptr;
        };
        const GameKey* a = row("a"), *ssaBack = row("back"), *ssaStart = row("start");
        SETTINGS_EXPECT(a != nullptr && ssaBack != nullptr && ssaStart != nullptr);
        const GameKey& ssaA = *a;
        SETTINGS_EXPECT(PadButtonName(ssaA, PadFamily::Xbox360) == "A" && PadButtonName(ssaA, PadFamily::PlayStation) == "Cross");
        SETTINGS_EXPECT(PadButtonName(ssaA, PadFamily::Nintendo) == "B" && PadButtonName(ssaA, PadFamily::XboxOne) == "A");
        SETTINGS_EXPECT(PadButtonName(*ssaBack, PadFamily::XboxOne) == "View" && PadButtonName(*ssaStart, PadFamily::XboxOne) == "Menu");
        SETTINGS_EXPECT(PadButtonName(*ssaBack, PadFamily::PlayStation) == "Touchpad" && PadButtonName(*ssaStart, PadFamily::PlayStation) == "Options");
        SETTINGS_EXPECT(PadButtonName(*ssaBack, PadFamily::PlayStation3) == "Select" && PadButtonName(*ssaStart, PadFamily::Nintendo) == "+");
        SETTINGS_EXPECT(PadButtonName(*row("left_up"), PadFamily::PlayStation) == "L stick");
        SETTINGS_EXPECT(PadFamilyOf(SDL_GAMEPAD_TYPE_PS5) == PadFamily::PlayStation && PadFamilyOf(SDL_GAMEPAD_TYPE_PS3) == PadFamily::PlayStation3);
        SETTINGS_EXPECT(PadFamilyOf(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO) == PadFamily::Nintendo && PadFamilyOf(SDL_GAMEPAD_TYPE_XBOXONE) == PadFamily::XboxOne);
        SETTINGS_EXPECT(PadFamilyOf(SDL_GAMEPAD_TYPE_STANDARD) == PadFamily::Xbox360);
        SETTINGS_EXPECT(PadFamilyFor("auto", PadFamily::Nintendo) == PadFamily::Nintendo && PadFamilyFor("playstation", PadFamily::Nintendo) == PadFamily::PlayStation);
        SETTINGS_EXPECT(PadFamilyFor("xbox", PadFamily::PlayStation) == PadFamily::Xbox360 && PadFamilyFor("xbox", PadFamily::XboxOne) == PadFamily::XboxOne);
        i18n::SetLanguage("fr");
        SETTINGS_EXPECT(PadButtonName(ssaA, PadFamily::PlayStation) == "Croix" && PadButtonName(*ssaBack, PadFamily::PlayStation) == "Pavé tactile");
        SETTINGS_EXPECT(PadButtonName(ssaA, PadFamily::Xbox360) == "A");
        i18n::SetLanguage("en");

        // Seeded once from the player's [input] keys; the other keys of [input] are not copied; the device stays.
        LauncherConfig config;
        config["input"] = { { "device", "keyboard" }, { "key_x", "F" }, { "key_dpad_up", "T" }, { "glyphs", "keyboard" } };
        SeedGameInput(config, "ssa");
        SeedGameInput(config, "giants");
        SETTINGS_EXPECT(config["ssa.input"]["key_x"] == "F" && config["giants.input"]["key_dpad_up"] == "T");
        SETTINGS_EXPECT(config["ssa.input"].count("device") == 0 && config["ssa.input"].count("glyphs") == 0);
        SETTINGS_EXPECT(GameKeyName(config, "ssa", GameKeys("ssa")[5]) == "F"); // ssa's X row
        config["input"]["key_x"] = "G"; // later changes of [input] (a game was played) never seed a game again
        SeedGameInput(config, "ssa");
        SETTINGS_EXPECT(config["ssa.input"]["key_x"] == "F");

        // A game that had nothing to seed is still marked, so [input] (another game's keys by then) does not seed it later.
        LauncherConfig fresh;
        SeedGameInput(fresh, "swapforce");
        fresh["input"]["key_a"] = "Z";
        SeedGameInput(fresh, "swapforce");
        SETTINGS_EXPECT(fresh["swapforce.input"].count("key_a") == 0 && fresh["swapforce.input"]["seeded"] == "1");

        // PLAY: the game's keys go to [input]; its defaults fill what it has not set; its unused actions follow the copy or
        // the runtime's default; the device and the glyph style stay; the other game's copy is not touched.
        config["ssa.input"]["key_a"] = "J";
        config["giants.input"]["key_a"] = "K";
        ApplyGameInput(config, "ssa");
        SETTINGS_EXPECT(config["input"]["key_a"] == "J" && config["input"]["key_x"] == "F" && config["input"]["key_y"] == "V");
        SETTINGS_EXPECT(config["input"]["device"] == "keyboard" && config["input"]["glyphs"] == "keyboard");
        SETTINGS_EXPECT(config["input"]["key_dpad_up"] == "T" && config["input"].count("key_lb") == 0); // an unused action keeps what it was seeded with
        ApplyGameInput(config, "giants");
        SETTINGS_EXPECT(config["input"]["key_a"] == "K" && config["input"]["key_x"] == "F" && config["input"]["key_dpad_up"] == "T");
        SETTINGS_EXPECT(config["ssa.input"]["key_a"] == "J" && config["giants.input"]["key_a"] == "K");

        // Two rows of one game on the same key are a conflict, in that game only.
        config["giants.input"]["key_y"] = "K";
        const std::span<const GameKey> giants = GameKeys("giants");
        int conflicts = 0;
        for (const GameKey& key : giants)
            conflicts += GameKeyConflict(config, "giants", key) ? 1 : 0;
        SETTINGS_EXPECT(conflicts == 2);
        for (const GameKey& key : GameKeys("ssa"))
            SETTINGS_EXPECT(!GameKeyConflict(config, "ssa", key) || std::string_view(key.action) != "y");

        // Reset: the game's copy loses its keys (the marker stays), and PLAY then puts the defaults in [input].
        ResetGameKeys(config, "giants");
        SETTINGS_EXPECT(config["giants.input"].count("key_a") == 0 && config["giants.input"].count("key_dpad_up") == 0 && config["giants.input"]["seeded"] == "1");
        SETTINGS_EXPECT(GameKeyName(config, "giants", giants[4]) == "Space");
        ApplyGameInput(config, "giants");
        SETTINGS_EXPECT(config["input"]["key_a"] == "Space" && config["input"]["key_y"] == "V" && config["input"].count("key_dpad_up") == 0);

        // Through the settings file, sections with a dot included.
        fs::path scratch = fs::temp_directory_path() / "skylauncher-keys-test.ini";
        SETTINGS_EXPECT(SaveLauncherConfig(scratch.string(), config));
        LauncherConfig back = LoadLauncherConfig(scratch.string());
        SETTINGS_EXPECT(back["ssa.input"]["key_a"] == "J" && back["giants.input"]["seeded"] == "1");
        fs::remove(scratch);
        printf("Settings: each game's keys are its own (its own actions and names), seeded once, put in [input] at PLAY, reset, conflicts\n");
    }

    // ---- The command line ------------------------------------------------------------------------------------------------

    // The Controller tab: what each pad button does (pad_<button> in [<game>.input]) and the rumble.
    void PadChecks()
    {
        LauncherConfig config;
        SETTINGS_EXPECT(PadButtons().size() == size_t(PadButtonTotal) && std::string_view(PadButtons()[5]) == "rb");
        // Nothing stored: every button does its own job; the rumble is normal.
        for (int button = 0; button < PadButtonTotal; button++)
            SETTINGS_EXPECT(PadTarget(config, "giants", button) == button);
        SETTINGS_EXPECT(RumbleLevel(config, "giants", 0) == "normal" && RumbleLevel(config, "giants", 1) == "normal");
        // RB does B's job: one line in the game's own section, and the other games and sections are not touched.
        SetPadTarget(config, "giants", 5, 1);
        SETTINGS_EXPECT(config["giants.input"]["pad_rb"] == "b" && PadTarget(config, "giants", 5) == 1);
        SETTINGS_EXPECT(PadTarget(config, "ssa", 5) == 5 && config.count("ssa.input") == 0 && config.count("input") == 0);
        SetPadTarget(config, "giants", 12, -1); // the D-pad's up turned off
        SETTINGS_EXPECT(config["giants.input"]["pad_dpad_up"] == "none" && PadTarget(config, "giants", 12) == -1);
        config["giants.input"]["pad_a"] = "nonsense"; // a value the runtime would refuse: the button keeps its own job
        SETTINGS_EXPECT(PadTarget(config, "giants", 0) == 0);
        // A button given back its own job loses its line.
        SetPadTarget(config, "giants", 5, 5);
        SETTINGS_EXPECT(config["giants.input"].count("pad_rb") == 0 && PadTarget(config, "giants", 5) == 5);
        // Reset: every pad_ line of the game goes, its keys and its rumble stay.
        config["giants.input"]["key_a"] = "G";
        config["giants.input"]["rumble_p1"] = "strong";
        config["giants.input"]["rumble_p2"] = "mighty";
        SetPadTarget(config, "giants", 1, 0);
        ResetPadMap(config, "giants");
        SETTINGS_EXPECT(PadTarget(config, "giants", 1) == 1 && PadTarget(config, "giants", 12) == 12);
        SETTINGS_EXPECT(config["giants.input"]["key_a"] == "G" && RumbleLevel(config, "giants", 0) == "strong");
        SETTINGS_EXPECT(RumbleLevel(config, "giants", 1) == "normal"); // an unknown level reads as normal, as the runtime does
        SETTINGS_EXPECT(std::string_view(RumbleLevels()[0]) == "off" && RumbleLevels().size() == 4);
        // Names: as the pad in use prints them, and the game's word for the button's job.
        SETTINGS_EXPECT(PadButtonLabel(5, PadFamily::Xbox360) == "RB" && PadButtonLabel(5, PadFamily::PlayStation) == "R1");
        SETTINGS_EXPECT(PadButtonLabel(10, PadFamily::PlayStation) == "L3" && PadButtonLabel(0, PadFamily::Nintendo) == "B");
        SETTINGS_EXPECT(PadButtonLabel(12, PadFamily::XboxOne) == "D-pad up" && PadButtonLabel(99, PadFamily::XboxOne).empty());
        SETTINGS_EXPECT(PadActionName("swapforce", 5) == "Attack 2" && PadActionName("swapforce", 4) == "Attack 3");
        SETTINGS_EXPECT(PadActionName("giants", 5).empty() && PadActionName("giants", 12).empty() && PadActionName("giants", 0) == "Attack 1 / Accept");
        printf("Pad buttons: remap, reset, rumble and names\n");
    }

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

    // ---- The names of the environment variables ---------------------------------------------------------------------------

    void EnvironmentChecks()
    {
        const char* all[] = { "SKY_LAUNCHER_ZZ", "SKYLAUNCHER_ZZ", "SSA_LAUNCHER_ZZ", "SKY_ZZ", "SSA_ZZ", "SKY_YY", "SSA_YY", "SKYLAUNCHER_XX", "SKY_LAUNCHER_XX", "SSA_LAUNCHER_XX",
            "SKY_LAUNCHER_WW", "SKYLAUNCHER_WW", "SSA_LAUNCHER_WW" };
        auto clear = [&] { for (const char* name : all) SetEnv(name, ""); };
        clear();
        // The launcher reads the new name, then SKYLAUNCHER_, then SSA_LAUNCHER_; a game's variable SKY_ then SSA_.
        SETTINGS_EXPECT(LauncherEnv("SKY_LAUNCHER_ZZ") == nullptr);
        SetEnv("SSA_LAUNCHER_ZZ", "c");
        SETTINGS_EXPECT(std::string(LauncherEnv("SKY_LAUNCHER_ZZ")) == "c");
        SetEnv("SKYLAUNCHER_ZZ", "b");
        SETTINGS_EXPECT(std::string(LauncherEnv("SKY_LAUNCHER_ZZ")) == "b");
        SetEnv("SKY_LAUNCHER_ZZ", "a");
        SETTINGS_EXPECT(std::string(LauncherEnv("SKY_LAUNCHER_ZZ")) == "a");
        SetEnv("SSA_ZZ", "game");
        SETTINGS_EXPECT(std::string(skyenv::Get("SKY_ZZ")) == "game");

        // A game is started with both names of everything set: the older kits read SSA_, the newer ones SKY_. The launcher's own
        // environment is left as it is.
        clear();
        SetEnv("SKY_YY", "new");
        SetEnv("SSA_ZZ", "old");
        SetEnv("SKYLAUNCHER_XX", "launcher");
        SetEnv("SSA_LAUNCHER_WW", "oldest");
        SetEnv("SKY_LAUNCHER_ZZ", "kept");
        SetEnv("SSA_LAUNCHER_ZZ", "other"); // both set: neither is replaced
        SDL_Environment* game = CreateGameEnvironment();
        SETTINGS_EXPECT(game != nullptr);
        auto var = [&](const char* name) { const char* value = SDL_GetEnvironmentVariable(game, name); return value != nullptr ? std::string(value) : std::string("<unset>"); };
        auto own = [](const char* name) { const char* value = SDL_getenv(name); return value != nullptr ? std::string(value) : std::string("<unset>"); };
        SETTINGS_EXPECT(var("SSA_YY") == "new" && var("SKY_YY") == "new");
        SETTINGS_EXPECT(var("SKY_ZZ") == "old" && var("SSA_ZZ") == "old");
        SETTINGS_EXPECT(var("SKY_LAUNCHER_XX") == "launcher" && var("SKYLAUNCHER_XX") == "launcher" && var("SSA_LAUNCHER_XX") == "launcher");
        SETTINGS_EXPECT(var("SKY_LAUNCHER_WW") == "oldest" && var("SKYLAUNCHER_WW") == "oldest" && var("SSA_LAUNCHER_WW") == "oldest");
        SETTINGS_EXPECT(var("SKY_LAUNCHER_ZZ") == "kept" && var("SSA_LAUNCHER_ZZ") == "other");
        SETTINGS_EXPECT(var("HOME") == own("HOME") && var("PATH") == own("PATH")); // everything else goes along
        SETTINGS_EXPECT(own("SSA_YY") == "<unset>" && own("SKY_ZZ") == "<unset>" && own("SKY_LAUNCHER_XX") == "<unset>" && own("SSA_LAUNCHER_XX") == "<unset>"); // the launcher's own is unchanged
        SDL_DestroyEnvironment(game);
        // Set and unset under every name.
        SetGameVariable("SKY_LAUNCHER_WW", "forced");
        SETTINGS_EXPECT(own("SKY_LAUNCHER_WW") == "forced" && own("SKYLAUNCHER_WW") == "forced" && own("SSA_LAUNCHER_WW") == "forced");
        UnsetGameVariable("SKY_LAUNCHER_WW");
        SETTINGS_EXPECT(own("SKY_LAUNCHER_WW") == "<unset>" && own("SKYLAUNCHER_WW") == "<unset>" && own("SSA_LAUNCHER_WW") == "<unset>");
        clear();
        printf("Environment: new names first, older names still read, both names given to a game\n");
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
        {
            // A real 512 px PNG (the game's icon): the .ico entry holds it halved to 256 px.
            std::vector<uint8_t> pixels(512 * 512 * 4, 200), real;
            auto append = [](void* context, void* data, int size) {
                auto* bytes = static_cast<std::vector<uint8_t>*>(context);
                bytes->insert(bytes->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
            };
            SETTINGS_EXPECT(stbi_write_png_to_func(append, &real, 512, 512, 4, pixels.data(), 512 * 4) != 0);
            std::vector<uint8_t> bigIco = WindowsIco(real);
            SETTINGS_EXPECT(bigIco.size() > 22 + 24 && bigIco[6] == 0 && bigIco[22 + 16] == 0 && bigIco[22 + 17] == 0 && bigIco[22 + 18] == 1 && bigIco[22 + 19] == 0); // IHDR width 256
        }
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
        SetEnv("SKY_LAUNCHER_DESKTOP", (scratch / "desktop").string());
        SETTINGS_EXPECT(DesktopFolder() == scratch / "desktop");
        SetEnv("SKY_LAUNCHER_EXE", "/somewhere/Skylauncher");
        SETTINGS_EXPECT(SelfExecutable() == fs::path("/somewhere/Skylauncher"));
        SetEnv("SKY_LAUNCHER_EXE", "");

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
        // Shortcuts made by a launcher called Skylauncher (0.4.x) start the new one and carry its name; the rest is left as it is.
        {
            const fs::path desktop = scratch / "older-desktop";
            const std::string oldExec = "/Applications/Skylauncher.app/Contents/MacOS/Skylauncher";
            const std::string oldScript = "#!/bin/sh\n# Opens Skylauncher on this game and starts it. Made by Skylauncher (Game settings).\nexec '" + oldExec + "' --game giants --play\n";
            Write(desktop / "Skylanders Giants.app" / "Contents" / "MacOS" / "launch", oldScript);
            Write(desktop / "Skylanders Giants.app" / "Contents" / "Resources" / "icon.icns", "icon");
            const std::string oldEntry = "[Desktop Entry]\nType=Application\nVersion=1.0\nName=Skylanders: Spyro's Adventure\nComment=Play Skylanders: Spyro's Adventure with Skylauncher\n"
                                         "Exec=\"/home/p/Skylauncher-x86_64.AppImage\" --game ssa --play\nIcon=/home/p/shortcut-icon.png\nTerminal=false\nCategories=Game;\n";
            Write(desktop / "skylanders-spyro's-adventure.desktop", oldEntry);
            // Not ours (no --game), already current, and another game's name: untouched.
            Write(desktop / "skylanders-swap-force.desktop", "[Desktop Entry]\nName=Skylanders: Swap Force\nComment=with Skylauncher\nExec=/bin/true\n");
            const std::vector<std::pair<std::string, std::string>> games = { { "ssa", "Skylanders: Spyro's Adventure" }, { "giants", "Skylanders: Giants" },
                { "swapforce", "Skylanders: Swap Force" } };
            const fs::path newer = "/Applications/Arkeyan Launcher.app/Contents/MacOS/ArkeyanLauncher";
            SETTINGS_EXPECT(RefreshShortcuts(desktop, games, newer) == 2);
            SETTINGS_EXPECT(Read(desktop / "Skylanders Giants.app" / "Contents" / "MacOS" / "launch") == MacLaunchScript(newer, "giants"));
            SETTINGS_EXPECT(Read(desktop / "Skylanders Giants.app" / "Contents" / "MacOS" / "launch").find("Skylauncher") == std::string::npos);
            SETTINGS_EXPECT(Read(desktop / "Skylanders Giants.app" / "Contents" / "Resources" / "icon.icns") == "icon");
            const std::string rewritten = Read(desktop / "skylanders-spyro's-adventure.desktop");
            SETTINGS_EXPECT(rewritten == LinuxDesktopEntry("Skylanders: Spyro's Adventure", newer, "ssa", "/home/p/shortcut-icon.png") && rewritten.find("Skylauncher") == std::string::npos);
            SETTINGS_EXPECT(Read(desktop / "skylanders-swap-force.desktop").find("Exec=/bin/true") != std::string::npos);
            SETTINGS_EXPECT(RefreshShortcuts(desktop, games, newer) == 0); // once is enough
            SETTINGS_EXPECT(RefreshShortcuts(desktop / "nowhere", games, newer) == 0 && RefreshShortcuts(desktop, games, {}) == 0);
        }
        SetEnv("SKY_LAUNCHER_DESKTOP", "");
        printf("Shortcut: macOS .app (script run), Linux .desktop, Windows .ico, names and quoting, shortcuts of older launchers rewritten\n");
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
        SetEnv("SKY_SAVE_DIR", "");
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

        // FoldersOf names the game's folder in the data folder, and SKY_SAVE_DIR moves the saves.
        SetEnv("SKYLANDERS_DATA", (scratch / "root").string());
        SetEnv("SKY_LAUNCHER_DATA", "");
        SETTINGS_EXPECT(FoldersOf("giants").game == scratch / "root" / "Giants" && FoldersOf("giants").saves == scratch / "root" / "Giants" / "saves");
        SETTINGS_EXPECT(FoldersOf("ssa").game == scratch / "root" / "SSA");
        SetEnv("SKY_SAVE_DIR", (scratch / "elsewhere-saves").string());
        SETTINGS_EXPECT(FoldersOf("ssa").saves == scratch / "elsewhere-saves");
        SetEnv("SKY_SAVE_DIR", "");
        printf("Delete data and uninstall: sizes, caches only, refused while the game runs, what stays, on a scratch folder\n");
    }
}

namespace
{
    // A game's language lists follow its disc: stored at the check, filtered, the saved choice off the disc shows the default.
    void DiscLanguageChecks()
    {
        using namespace launcher_ui;
        using V = std::vector<std::string>;
        auto values = [](const std::vector<Choice>& choices) {
            V out;
            for (const Choice& choice : choices)
                out.push_back(choice.value);
            return out;
        };

        SETTINGS_EXPECT(OffersLanguage({}, "fr") && OffersLanguage({ "en" }, "") && OffersLanguage({ "en", "fr" }, "fr") && !OffersLanguage({ "en" }, "fr"));

        LauncherConfig config;
        art::DiscLanguages europe{ { "es", "it" }, { "es", "it" } };
        StoreDiscLanguages(config, "swapforce", europe);
        SETTINGS_EXPECT(config["swapforce"]["disc_text_languages"] == "es,it");
        art::DiscLanguages back = StoredDiscLanguages(config, "swapforce");
        SETTINGS_EXPECT(back.text == europe.text && back.voice == europe.voice);
        // It survives the settings file.
        fs::path file = fs::temp_directory_path() / "disc-languages-check.ini";
        SETTINGS_EXPECT(SaveLauncherConfig(file.string(), config));
        art::DiscLanguages loaded = StoredDiscLanguages(LoadLauncherConfig(file.string()), "swapforce");
        SETTINGS_EXPECT(loaded.text == europe.text && loaded.voice == europe.voice);
        fs::remove(file);

        // Swap Force Europe: Spanish and Italian only; the choice that was saved is not there, so the first (the default) is shown.
        std::vector<Choice> text = GameLanguageChoices(config, "swapforce", false);
        SETTINGS_EXPECT(values(text) == V({ "es", "it" }));
        SETTINGS_EXPECT(ChoiceIndex(text, "fr", 0) == 0 && ChoiceIndex(text, "it", 0) == 1);

        // Spyro's Adventure with voices on the disc but one text language less: each list on its own.
        StoreDiscLanguages(config, "ssa", { { "en", "fr", "de" }, { "en", "es" } });
        SETTINGS_EXPECT(values(GameLanguageChoices(config, "ssa", false)) == V({ "en", "fr", "de" }));
        SETTINGS_EXPECT(values(GameLanguageChoices(config, "ssa", true)) == V({ "en", "es" }));

        // Giants keeps "Same as text" whatever the disc says.
        StoreDiscLanguages(config, "giants", { { "en", "fr" }, { "en", "fr" } });
        SETTINGS_EXPECT(values(GameLanguageChoices(config, "giants", true)) == V({ "", "en", "fr" }));

        // Trap Team: the languages of the disc, text and voices, in the launcher's order, whatever order they were stored in.
        StoreDiscLanguages(config, "trapteam", { { "fr", "nl" }, { "fr", "nl" } });
        SETTINGS_EXPECT(config["trapteam"]["disc_text_languages"] == "fr,nl");
        SETTINGS_EXPECT(values(GameLanguageChoices(config, "trapteam", false)) == V({ "fr", "nl" }));
        SETTINGS_EXPECT(values(GameLanguageChoices(config, "trapteam", true)) == V({ "fr", "nl" }));
        StoreDiscLanguages(config, "trapteam", { { "de", "en" }, { "en" } });
        SETTINGS_EXPECT(values(GameLanguageChoices(config, "trapteam", false)) == V({ "en", "de" }));
        SETTINGS_EXPECT(values(GameLanguageChoices(config, "trapteam", true)) == V({ "en" }));

        // Languages beyond Giants' twelve are named, with a word of the launcher's where the font cannot draw the language's own,
        // in English and in French; a code nobody knows is ignored (and a list of such codes alone is as if nothing were stored).
        StoreDiscLanguages(config, "swapforce", { { "ru", "pl", "ja", "xx", "en" }, { "en", "ru" } });
        SETTINGS_EXPECT(values(GameLanguageChoices(config, "swapforce", false)) == V({ "en", "pl", "ru", "ja" }));
        SETTINGS_EXPECT(values(GameLanguageChoices(config, "swapforce", true)) == V({ "en", "ru" }));
        for (const char* code : { "es-419", "pt", "pl", "ru", "cs", "hu", "tr", "el", "ja", "ko", "zh", "sv" })
        {
            const Choice* named = nullptr;
            for (const Choice& choice : AllLanguageChoices)
                if (std::string(choice.value) == code)
                    named = &choice;
            SETTINGS_EXPECT(named != nullptr);
            i18n::SetLanguage("fr");
            const std::string french = named != nullptr ? i18n::Tr(named->label) : "";
            i18n::SetLanguage("en");
            const std::string english = named != nullptr ? i18n::Tr(named->label) : "";
            SETTINGS_EXPECT(!english.empty() && !french.empty());
            if (std::string(code) == "ru" || std::string(code) == "el" || std::string(code) == "ja" ||
                std::string(code) == "ko" || std::string(code) == "zh")
                SETTINGS_EXPECT(french != english); // a word of the launcher's: translated
            else
                SETTINGS_EXPECT(french == english); // the language's own name
        }
        StoreDiscLanguages(config, "trapteam", { { "xx", "yy" }, { "zz" } });
        SETTINGS_EXPECT(values(GameLanguageChoices(config, "trapteam", false)) == V({ "en" }) &&
                        values(GameLanguageChoices(config, "trapteam", true)) == V({ "en" }));
        StoreDiscLanguages(config, "ssa", { { "xx" }, { "xx" } });
        SETTINGS_EXPECT(GameLanguageChoices(config, "ssa", false).size() == 10);

        // Nothing stored (a game checked before this version): today's full lists; Swap Force's was English and French, Trap Team's
        // is English.
        LauncherConfig old;
        SETTINGS_EXPECT(GameLanguageChoices(old, "ssa", false).size() == 10 && GameLanguageChoices(old, "giants", false).size() == 12);
        SETTINGS_EXPECT(GameLanguageChoices(old, "giants", true).size() == 13);
        SETTINGS_EXPECT(values(GameLanguageChoices(old, "swapforce", true)) == V({ "en", "fr" }));
        SETTINGS_EXPECT(values(GameLanguageChoices(old, "trapteam", false)) == V({ "en" }));
        // Forgetting the disc clears them.
        StoreDiscLanguages(config, "swapforce", {});
        SETTINGS_EXPECT(config["swapforce"].count("disc_text_languages") == 0 && StoredDiscLanguages(config, "swapforce").text.empty());
        printf("Disc languages: stored, filtered, the default when the choice is not on the disc, the full list when unknown\n");
    }

    // The launcher's own language: never the shown game's; an old "game" is the system's and goes at the next save.
    void LauncherLanguageChecks()
    {
        using namespace launcher_ui;
        const std::vector<std::string> system = { "ja", "fr", "en" };
        fs::path file = fs::temp_directory_path() / "launcher-language-check.ini";
        Write(file, "[launcher]\nlanguage=game\ngame=ssa\n[ssa]\nlanguage=fr\n[giants]\nlanguage=de\n");
        LauncherConfig config = LoadLauncherConfig(file.string());
        // An old "game" is the system's language (the first one the launcher has) and is not kept.
        SETTINGS_EXPECT(config["launcher"].count("language") == 0);
        SETTINGS_EXPECT(i18n::Resolve(SettingGet(config, "launcher", "language"), system) == "fr");
        SETTINGS_EXPECT(SaveLauncherConfig(file.string(), config));
        SETTINGS_EXPECT(LoadLauncherConfig(file.string())["launcher"].count("language") == 0); // gone at the next save
        // Showing another game, with its own text language, leaves the launcher's language alone.
        config["launcher"]["language"] = "en";
        config["launcher"]["game"] = "giants";
        SETTINGS_EXPECT(i18n::Resolve(SettingGet(config, "launcher", "language"), system) == "en");
        config["launcher"]["game"] = "ssa";
        SETTINGS_EXPECT(i18n::Resolve(SettingGet(config, "launcher", "language"), system) == "en");
        // A chosen language is what the next file keeps.
        SETTINGS_EXPECT(SaveLauncherConfig(file.string(), config));
        SETTINGS_EXPECT(LoadLauncherConfig(file.string())["launcher"]["language"] == "en");
        fs::remove(file);
        printf("Launcher language: never the shown game's, an old game is the system's and goes at the next save\n");
    }
}

int SettingsChecks(const fs::path& scratch)
{
    fs::create_directories(scratch);
    VideoChecks();
    FrameRateMigration();
    GiantsCap();
    KeyChecks();
    PadChecks();
    ArgumentChecks();
    EnvironmentChecks();
    ShortcutChecks(scratch / "shortcut");
    DataChecks(scratch / "datachecks");
    DiscLanguageChecks();
    LauncherLanguageChecks();
    return g_settingsFailures;
}
