// A folder whose name has accents (a Windows user name does): the data root, the collection and the install folder, each
// written, read, listed and deleted. It runs in a process of its own (the collection folder is fixed for a process), with the
// system's data folder moved into scratch: APPDATA on Windows, SKYLANDERS_DATA elsewhere. The word is only a word.
#include "launcher/game_install.h"
#include "launcher/launcher.h"
#include "launcher/ui/launcher_app.h"
#include "launcher/updater.h"
#include "shared/collection/collection.h"
#include "shared/collection/datadir.h"
#include "shared/dir_list.h"
#include "shared/env.h"
#include "shared/figures/figure_tag.h"
#include "test_env.h"
#include <SDL3/SDL.h>
#include <miniz.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    int g_failures = 0;

#define ACCENT_EXPECT(condition) \
    do { if (!(condition)) { printf("FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition); g_failures++; } } while (0)

    std::string Slurp(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }

    bool Spit(const fs::path& file, const std::string& bytes)
    {
        std::error_code error;
        fs::create_directories(file.parent_path(), error);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << bytes;
        return out.good();
    }

    bool Lists(const fs::path& folder, const fs::path& name)
    {
        std::error_code error;
        for (const auto& entry : ListDirectory(folder, error))
            if (entry.path().filename() == name)
                return true;
        return false;
    }

    // Every spelling of a variable out of the way, so that only the system's folder decides.
    void Clear(const char* name)
    {
        std::string first, second;
        skyenv::OlderNames(name, first, second);
        for (const std::string& spelling : { std::string(name), first, second })
            if (!spelling.empty())
                TestUnsetEnv(spelling.c_str());
    }

    void DataRootChecks(const fs::path& root)
    {
        const fs::path data = datadir::Root();
#ifdef _WIN32
        ACCENT_EXPECT(data == root / "Roaming" / "Skylanders");
#else
        ACCENT_EXPECT(data == root / "data");
#endif
        ACCENT_EXPECT(Spit(data / "probe.txt", "root bytes"));
        ACCENT_EXPECT(Slurp(data / "probe.txt") == "root bytes" && Lists(data, "probe.txt"));
        // The launcher's own settings file, in the data root: written through the UTF-8 text path, read back.
        const std::string ini = LauncherConfigPath();
        ACCENT_EXPECT(datadir::FromText(ini).parent_path() == data);
        LauncherConfig config;
        config["accent"]["key"] = "value";
        ACCENT_EXPECT(SaveLauncherConfig(ini, config) && fs::exists(datadir::FromText(ini)));
        ACCENT_EXPECT(LoadLauncherConfig(ini)["accent"]["key"] == "value");
        std::error_code error;
        ACCENT_EXPECT(fs::remove(data / "probe.txt", error) && !fs::exists(data / "probe.txt"));
        printf("Accented data root: written, read, listed and deleted\n");
    }

    void CollectionChecks()
    {
        const fs::path folder = datadir::Root() / "Collection";
        ACCENT_EXPECT(collection::Folder() == datadir::Text(folder));
        FigureData dump = figures::CreateFigure(14, 0);
        const std::string name = "014 Gill Grunt.sky";
        ACCENT_EXPECT(Spit(folder / name, std::string(dump.begin(), dump.end())));
        const std::string path = collection::PathOf(name);
        collection::Figure figure;
        ACCENT_EXPECT(collection::Read(path, figure) && figure.toy != nullptr && figure.toy->name == "Gill Grunt");
        ACCENT_EXPECT(collection::List().size() == 1 && Lists(folder, name));
        figure.stats.gold = 1234;
        std::string error;
        ACCENT_EXPECT(collection::WriteStats(path, figure.stats, &error));
        ACCENT_EXPECT(collection::Read(path, figure) && figure.stats.gold == 1234);
        ACCENT_EXPECT(collection::SetFavourite(path, true) && fs::exists(folder / "collection.json"));
        ACCENT_EXPECT(!collection::Backup(path).empty() && !collection::Backups(path).empty());
        // An import from another accented folder, then the figure out of the way.
        const fs::path elsewhere = folder.parent_path() / "import";
        FigureData spyro = figures::CreateFigure(16, 0);
        ACCENT_EXPECT(Spit(elsewhere / "016 Spyro.sky", std::string(spyro.begin(), spyro.end())));
        collection::ImportReport report = collection::Import({ datadir::Text(elsewhere / "016 Spyro.sky") });
        ACCENT_EXPECT(report.added == 1 && fs::exists(folder / "016 Spyro.sky"));
        ACCENT_EXPECT(collection::Delete(path) && !fs::exists(folder / name) && collection::List().size() == 1);
        printf("Accented collection: written, read, listed, imported, saved and deleted\n");
    }

    // The pictures the collection shows are files of the game's data folder, read by the window.
    void PortraitChecks()
    {
        const fs::path portrait = datadir::Game("ssa") / "portraits" / "toys" / "014.png";
        ACCENT_EXPECT(Spit(portrait, "picture bytes"));
        std::vector<uint8_t> bytes;
        ACCENT_EXPECT(launcher_ui::ReadWholeFile(datadir::Text(portrait), bytes) && std::string(bytes.begin(), bytes.end()) == "picture bytes");
        printf("Accented portraits folder: a picture read\n");
    }

    // "Report a problem", saved: the save dialog hands the callback a UTF-8 path.
    void ReportChecks()
    {
        const fs::path report = datadir::Root() / "Desktop" / "report.txt";
        std::error_code error;
        fs::create_directories(report.parent_path(), error);
        const std::string chosen = datadir::Text(report);
        const char* files[] = { chosen.c_str(), nullptr };
        launcher_ui::SaveReport(new std::string("report text"), files, 0);
        ACCENT_EXPECT(Slurp(report) == "report text");
        ACCENT_EXPECT(fs::remove(report, error) && !fs::exists(report));
        printf("Accented folder for the report: saved\n");
    }

    void InstallFolderChecks(const fs::path& scratch)
    {
        const fs::path install = GameInstallFolder({});
        ACCENT_EXPECT(Spit(install / "install.log", "log bytes") && Slurp(install / "install.log") == "log bytes" && Lists(install, "install.log"));
        // The kit zip: made where the name is plain, put in the kits folder, unpacked there.
        const fs::path plain = scratch / "plain-zip" / "kit.zip";
        std::error_code error;
        fs::create_directories(plain.parent_path(), error);
        fs::remove(plain, error);
        const std::string files[][2] = { { "kit/SkyBuild", "kit bytes" }, { "kit/SkyBuild.exe", "kit bytes" }, { "kit/kit.txt", "build=b1\n" } };
        for (const auto& file : files)
            ACCENT_EXPECT(mz_zip_add_mem_to_archive_file_in_place(plain.string().c_str(), file[0].c_str(), file[1].data(), file[1].size(), nullptr, 0, 6));
        const fs::path kits = DownloadedKitsFolder();
        fs::create_directories(kits / ".download", error);
        fs::copy_file(plain, kits / ".download" / "kit.zip", fs::copy_options::overwrite_existing, error);
        ACCENT_EXPECT(!error && fs::exists(kits / ".download" / "kit.zip"));
        // The same unpacking with the accents on one side only, and with none: what fails tells which path it is.
        const fs::path accented = kits / ".download" / "kit.zip";
        std::string why;
        ACCENT_EXPECT(ExtractZip(plain, scratch / "plain-out", why));
        ACCENT_EXPECT(ExtractZip(plain, kits / "from-plain", why));
        ACCENT_EXPECT(ExtractZip(accented, scratch / "plain-out2", why));
        why.clear();
        ACCENT_EXPECT(InstallKitZip(accented, kits / "v0.0.1", why));
        if (!why.empty())
            printf("accent_test: InstallKitZip said: %s\n", why.c_str());
        ACCENT_EXPECT(Slurp(kits / "v0.0.1" / "kit.txt") == "build=b1\n" && Lists(kits, "v0.0.1"));
        ACCENT_EXPECT(fs::remove_all(kits / "v0.0.1", error) > 0 && !fs::exists(kits / "v0.0.1"));
        // Both sides of the unpacking and of a download, with a name the ANSI code page has a letter for and one it has not.
        for (const fs::path& word : { fs::path(u8"donn\u00e9es-\u00e9"), fs::path(u8"\u30c7\u30fc\u30bf") })
        {
            const fs::path folder = scratch / "accents" / word / "zip";
            fs::create_directories(folder, error);
            fs::copy_file(plain, folder / "kit.zip", fs::copy_options::overwrite_existing, error);
            why.clear();
            ACCENT_EXPECT(ExtractZip(folder / "kit.zip", folder / "out", why));
            ACCENT_EXPECT(Slurp(folder / "out" / "kit" / "kit.txt") == "build=b1\n");
            // The files keep the date they have in the zip: the same kit unpacked twice is the same kit.
            ACCENT_EXPECT(ExtractZip(folder / "kit.zip", folder / "again", why));
            ACCENT_EXPECT(fs::last_write_time(folder / "out" / "kit" / "kit.txt", error) == fs::last_write_time(folder / "again" / "kit" / "kit.txt", error));
            // curl writes where it is told (-o), in that folder: from a plain address, a local file.
            HttpResult got = HttpDownload(TestFileUrl(plain), folder / "downloaded.zip");
            ACCENT_EXPECT(got.ok && Slurp(folder / "downloaded.zip") == Slurp(plain));
            if (!got.ok)
                printf("accent_test: the download said: %s\n", got.error.c_str());
        }
        ACCENT_EXPECT(fs::remove(install / "install.log", error) && !fs::exists(install / "install.log"));
        printf("Accented install folder and kit: written, unpacked, listed and deleted\n");
    }
}

// Run in the process the test binary starts for it (--accents <scratch>); nothing outside <scratch> is named.
int AccentChecks(const fs::path& scratch)
{
    const fs::path root = scratch / "accents" / fs::path(u8"données-é");
    fs::remove_all(scratch / "accents");
    fs::create_directories(root);
    for (const char* name : { "SKYLANDERS_DATA", "SKY_COLLECTION", "SKY_FIGURES", "SKY_LAUNCHER_DATA", "SKY_LAUNCHER_INI", "SKY_SAVE_DIR", "SKY_MIGRATE_FROM" })
        Clear(name);
    TestSetEnv("SKY_LAUNCHER_NO_UPDATE_CHECK", "1");
#ifdef _WIN32
    // The system's own variables are wide: a player's folder arrives as it is, never through the ANSI code page.
    _wputenv_s(L"APPDATA", (root / "Roaming").c_str());
    _wputenv_s(L"LOCALAPPDATA", (root / "Local").c_str());
#else
    const std::u8string data = (root / "data").u8string();
    setenv("SKYLANDERS_DATA", std::string(data.begin(), data.end()).c_str(), 1);
#endif
    DataRootChecks(root);
    CollectionChecks();
    InstallFolderChecks(scratch);
    PortraitChecks();
    ReportChecks();
    printf("%d failures in the accented folder checks\n", g_failures);
    return g_failures;
}

// The parent's side: the same binary, in a process of its own.
int AccentChecksInChild(const char* executable, const fs::path& scratch)
{
    const std::string folder = scratch.string();
    const char* arguments[] = { executable, "--accents", folder.c_str(), nullptr };
    SDL_Process* process = SDL_CreateProcess(arguments, false);
    if (process == nullptr)
    {
        printf("FAIL accent_test: cannot start %s\n", executable);
        return 1;
    }
    int code = -1;
    SDL_WaitProcess(process, true, &code);
    SDL_DestroyProcess(process);
    if (code != 0)
        printf("FAIL accent_test: the accented folder checks ended with %d\n", code);
    return code == 0 ? 0 : 1;
}
