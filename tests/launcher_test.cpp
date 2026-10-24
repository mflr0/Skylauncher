// Checks the launcher's disc verification and settings file without showing any UI.
// usage: SSALauncherTest <game disc .iso> <scratch dir> [<damaged copy of the image>...]
#include "launcher/game_install.h"
#include "launcher/launcher.h"
#include "assets/launcher_assets.h"
#include "launcher/ui/launcher_ui.h"
#include "shared/figures/figure_collection.h"
#include "shared/figures/figure_icons.h"
#include "shared/vfs.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <aes.hpp>
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

    // Figure engine, independent of the disc: crypto building blocks, a new figure, then progress written the way the
    // game writes it (re-implemented here from the layout, so a slip in either copy shows up).
    std::string Hex(std::span<const uint8_t> bytes)
    {
        std::string out;
        for (uint8_t b : bytes)
            out += fmt::format("{:02x}", b);
        return out;
    }

    void SealAreaForTest(figures::FigureData& data, int areaBlock, uint32_t experience, uint16_t gold, uint8_t sequence)
    {
        // Plain area blocks (area-relative 0..9), checksums as documented in figure_file.h.
        uint8_t blocks[10][16] = {};
        blocks[0][0] = uint8_t(experience); blocks[0][1] = uint8_t(experience >> 8); blocks[0][2] = uint8_t(experience >> 16);
        blocks[0][3] = uint8_t(gold); blocks[0][4] = uint8_t(gold >> 8);
        blocks[0][9] = sequence;
        blocks[1][0] = 0x05; // two upgrades
        const char16_t name[] = u"Gilly";
        for (int i = 0; name[i] != 0; i++)
            blocks[2][i * 2] = uint8_t(name[i]);
        auto crc = [](std::vector<uint8_t> bytes) { return figures::Crc16(bytes); };
        std::vector<uint8_t> extra(0x110, 0);
        for (int i = 0; i < 4; i++)
            memcpy(&extra[i * 16], blocks[std::array{ 5, 6, 8, 9 }[i]], 16);
        uint16_t c3 = crc(extra);
        std::vector<uint8_t> type2;
        for (int k : { 1, 2, 4 })
            type2.insert(type2.end(), blocks[k], blocks[k] + 16);
        uint16_t c2 = crc(type2);
        blocks[0][0x0A] = uint8_t(c3); blocks[0][0x0B] = uint8_t(c3 >> 8);
        blocks[0][0x0C] = uint8_t(c2); blocks[0][0x0D] = uint8_t(c2 >> 8);
        std::vector<uint8_t> head(blocks[0], blocks[0] + 16);
        head[0x0E] = 0x05; head[0x0F] = 0x00;
        uint16_t c1 = crc(head);
        blocks[0][0x0E] = uint8_t(c1); blocks[0][0x0F] = uint8_t(c1 >> 8);
        // Encrypt like the game: AES-128-ECB, key = MD5(blocks 0-1, block number, copyright line).
        static const char Copyright[] = " Copyright (C) 2010 Activision. All Rights Reserved. ";
        for (int k = 0; k < 10; k++)
        {
            int block = areaBlock + k;
            if (block % 4 == 3)
                continue;
            uint8_t* out = &data[block * 16];
            memcpy(out, blocks[k], 16);
            if (std::all_of(out, out + 16, [](uint8_t b) { return b == 0; }))
                continue;
            std::vector<uint8_t> material(data.begin(), data.begin() + 32);
            material.push_back(uint8_t(block));
            material.insert(material.end(), Copyright, Copyright + sizeof(Copyright) - 1);
            std::array<uint8_t, 16> key = figures::Md5(material);
            AES_ctx context;
            AES_init_ctx(&context, key.data());
            AES_ECB_encrypt(&context, out);
        }
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
        EXPECT(GameInstallFolder("abc").filename() == "abc" && GameInstallFolder("abc").parent_path().filename() == "games");

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
        InstallProgress done = run(nullptr);
        EXPECT(done.state == InstallProgress::State::Done && done.steps.size() == 6);
        EXPECT(std::filesystem::path(done.runtime).filename().string().starts_with("SSARecomp"));
        EXPECT(std::filesystem::exists(scratch / "game" / "install.log"));
        printf("  fake kit: failure at step %zu, then built %s\n", failed.steps.size(), done.runtime.c_str());

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

    void FigureChecks(const std::filesystem::path& scratch)
    {
        auto bytes = [](std::string_view text) { return std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()); };
        EXPECT(Hex(figures::Md5(bytes(""))) == "d41d8cd98f00b204e9800998ecf8427e");
        EXPECT(Hex(figures::Md5(bytes("abc"))) == "900150983cd24fb0d6963f7d28e17f72");
        EXPECT(Hex(figures::Md5(bytes("12345678901234567890123456789012345678901234567890123456789012345678901234567890"))) ==
            "57edf4a22be3c955ac49da2e2107b67a");
        EXPECT(figures::Crc16(bytes("123456789")) == 0x29B1);
        EXPECT(figures::LevelFromExperience(0) == 1 && figures::LevelFromExperience(1000) == 2 && figures::LevelFromExperience(40000) == 10);

        const figures::ToyInfo& gill = *figures::FindToy(14);
        figures::FigureData data = figures::CreateFigure(gill.toyType, gill.variantId);
        figures::FigureHeader header = figures::ReadHeader(data);
        EXPECT(header.checksumValid && header.toyType == 14 && header.variantId == 0);
        EXPECT(data[4] == (data[0] ^ data[1] ^ data[2] ^ data[3]));
        EXPECT(Hex(std::span(&data[0x30], 6)) == "4b0b20107ccb"); // sector 0 key A
        EXPECT(figures::ReadStats(data).state == figures::FigureStats::State::Blank);
        EXPECT(!figures::SetNickname(data, "Nope")); // a blank toy has no progress to rename

        SealAreaForTest(data, 0x08, 2500, 1234, 7);
        figures::FigureStats stats = figures::ReadStats(data);
        EXPECT(stats.state == figures::FigureStats::State::Valid && stats.area == 0);
        EXPECT(stats.experience == 2500 && stats.level == 3 && stats.gold == 1234 && stats.nickname == "Gilly" && stats.upgrades == 5);
        EXPECT(figures::SetNickname(data, "Gill Gr\xC3\xBCnt"));
        stats = figures::ReadStats(data);
        EXPECT(stats.state == figures::FigureStats::State::Valid && stats.area == 1 && stats.sequence == 8);
        EXPECT(stats.nickname == "Gill Gr\xC3\xBCnt" && stats.experience == 2500 && stats.gold == 1234);
        EXPECT(!figures::SetNickname(data, "A name far too long"));
        data[0x08 * 16 + 5] ^= 0xFF; // damage the older copy: the current one still reads
        EXPECT(figures::ReadStats(data).state == figures::FigureStats::State::Valid);
        data[0x24 * 16 + 5] ^= 0xFF; // and now both
        EXPECT(figures::ReadStats(data).state == figures::FigureStats::State::Unreadable);
        figures::ResetProgress(data);
        EXPECT(figures::ReadStats(data).state == figures::FigureStats::State::Blank && figures::ReadHeader(data).toyType == 14);

        // The folder: create, import, back up, restore, delete, locks.
        std::filesystem::path folder = scratch / "figures";
        std::string made = figures::NewFigure(folder, gill);
        EXPECT(made == "014 Gill Grunt.sky");
        EXPECT(figures::NewFigure(folder, gill) == "014 Gill Grunt (2).sky");
        std::filesystem::path outside = scratch / "dump.bin";
        EXPECT(figures::SaveFigure(outside, figures::CreateFigure(416, 0)));
        figures::ImportResult imported = figures::ImportFigure(folder, outside);
        EXPECT(imported.file == "dump.bin" && imported.error.empty());
        WriteFile(scratch / "junk.sky", std::vector<uint8_t>(100, 1));
        EXPECT(figures::ImportFigure(folder, scratch / "junk.sky").file.empty());
        EXPECT(!figures::BackupFigure(folder, made).empty());
        EXPECT(figures::ListBackups(folder, made).size() == 1);
        EXPECT(figures::RestoreBackup(folder, made, figures::ListBackups(folder, made).front()));
        EXPECT(figures::DeleteFigure(folder, made) && !std::filesystem::exists(folder / made));
        EXPECT(!figures::IsLocked(folder, "dump.bin"));
        std::ofstream(folder / "dump.bin.lock") << 0;
        EXPECT(!figures::IsLocked(folder, "dump.bin")); // no such process
        std::ofstream(folder / "dump.bin.lock") << getpid();
        EXPECT(figures::IsLocked(folder, "dump.bin"));

        // Generated icons: opaque in the middle, transparent in the corners.
        std::vector<uint8_t> icon = figures::GenerateIcon(*figures::FindToy(404), 64);
        EXPECT(icon.size() == 64 * 64 * 4 && icon[(32 * 64 + 32) * 4 + 3] == 255 && icon[3] == 0);
        for (const figures::ToyInfo& toy : figures::Catalog())
            EXPECT(figures::PlayableIn(toy, figures::Game::SpyrosAdventure) == (toy.game == figures::Game::SpyrosAdventure));
        printf("Figure engine: crypto, header, progress copies, folder and icons checked\n");
    }
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

    FigureChecks(scratch);
    InstallChecks(scratch, iso.string());

    // FIPS 180-4 examples, covering one- and two-block padding.
    auto sha256 = [](std::string_view text) { return Sha256Hex({ reinterpret_cast<const uint8_t*>(text.data()), text.size() }); };
    EXPECT(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT(sha256(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    printf("Game table: %zu game(s); \"ssa\" = %s, %zu release(s)\n", g_games.size(), game.name, game.releases.size());
    EXPECT(FindGame("unknown") == nullptr);

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

    printf("Disc frontend texture decoder:\n");
    std::vector<uint8_t> startMenu = disc.ReadAll("ui/eng/startmenu.str");
    std::vector<uint8_t> logo;
    start = Clock::now();
    EXPECT(!startMenu.empty());
    EXPECT(LauncherAssets::DecodeFrontendTexture(startMenu, "skylanderslogo.dds", 1024, 512, logo));
    EXPECT(logo.size() == 1024ull * 512 * 4);
    size_t transparent = 0, opaque = 0;
    for (size_t i = 3; i < logo.size(); i += 4)
    {
        transparent += logo[i] == 0 ? 1 : 0;
        opaque += logo[i] > 200 ? 1 : 0;
    }
    printf("  skylanderslogo.dds 1024x512, %zu transparent and %zu opaque texels, decoded in %lld ms\n",
        transparent, opaque, milliseconds(start));
    EXPECT(transparent > 1000);
    EXPECT(opaque > 1000);
    EXPECT(!LauncherAssets::DecodeFrontendTexture(startMenu, "missing.dds", 1024, 512, logo));

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

    std::filesystem::remove_all(scratch);
    printf(g_failures == 0 ? "All launcher checks passed\n" : "%d launcher check(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
