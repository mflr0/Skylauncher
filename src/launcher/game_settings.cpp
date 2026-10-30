#include "launcher/game_settings.h"
#include "launcher/i18n.h"
#include "launcher/game_install.h"
#include "shared/dir_list.h"
#include "shared/collection/datadir.h"
#include "shared/collection/save_backup.h"
#include <algorithm>
#include <array>

namespace fs = std::filesystem;

namespace
{
    constexpr std::array<const char*, 11> VideoKeys = { "fullscreen", "resolution_scale", "fps_limit", "skip_intro_movies", "vsync",
        "anisotropy", "aspect", "texture_packs", "texture_dump", "original_resolution", "show_fps" };

    // Top-level names in the game's folder (docs/install.md has the table).
    // The achievements and the play time (collection/progress.h) go with the saves: kept when the game is removed, deleted with them.
    constexpr std::array<const char*, 5> SaveNames = { "saves", "saves-backup", "portal.txt", "achievements.txt", "playtime.txt" };
    constexpr std::array<const char*, 3> CacheNames = { "pipelines.bin", "launcher-cache", "portraits" };
    // Never removed by uninstalling: the player's data, and what comes from the disc and is cheap to keep.
    // "gamedata" is the copied game data (game_copy.h): like the disc image it came from, it is only removed from the Disc tab.
    constexpr std::array<const char*, 3> KeptNames = { "textures", "figures", "gamedata" };

    bool Named(const std::string& name, const auto& names)
    {
        return std::find(names.begin(), names.end(), name) != names.end();
    }

    bool IsGameLog(const fs::path& entry)
    {
        std::string name = entry.filename().string();
        return entry.extension() == ".log" && name.rfind("install", 0) != 0;
    }

    uint64_t BytesOf(const fs::path& path)
    {
        std::error_code error;
        if (fs::is_regular_file(path, error))
            return fs::file_size(path, error);
        uint64_t total = 0;
        if (!fs::is_directory(path, error))
            return 0;
        for (fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error))
        {
            std::error_code fileError;
            if (it->is_regular_file(fileError))
                total += it->file_size(fileError);
        }
        return total;
    }

    // A path that may be deleted: not empty, not a drive's root.
    bool Deletable(const fs::path& path)
    {
        return !path.empty() && path.has_filename() && path != path.root_path();
    }

    void Remove(const fs::path& path, DataResult& result)
    {
        std::error_code error;
        if (!Deletable(path) || !fs::exists(fs::symlink_status(path, error)))
            return;
        uint64_t bytes = BytesOf(path);
        fs::remove_all(path, error);
        if (error)
        {
            if (result.ok)
                result.error = Tf("Couldn't delete: {}", error.message());
            result.ok = false;
        }
        else
            result.freed += bytes;
    }

    // The save-side paths: the saves, their backups, the seats of the next session.
    std::vector<fs::path> SavePaths(const GameFolders& folders)
    {
        std::vector<fs::path> paths;
        if (!folders.saves.empty())
        {
            paths.push_back(folders.saves);
            paths.push_back(savebackup::BackupRoot(folders.saves));
        }
        paths.push_back(folders.game / "portal.txt");
        paths.push_back(folders.game / "achievements.txt");
        paths.push_back(folders.game / "playtime.txt");
        return paths;
    }

    std::vector<fs::path> CachePaths(const GameFolders& folders)
    {
        std::vector<fs::path> paths;
        std::error_code error;
        if (!fs::is_directory(folders.game, error))
            return paths;
        for (const fs::directory_entry& entry : ListDirectory(folders.game, error))
        {
            std::string name = entry.path().filename().string();
            if (Named(name, CacheNames) || (entry.is_regular_file(error) && IsGameLog(entry.path())))
                paths.push_back(entry.path());
        }
        return paths;
    }

    DataResult Refused(const GameFolders& folders)
    {
        DataResult result;
        if (!Deletable(folders.game))
        {
            result.ok = false;
            result.error = Tr("This game has no folder.");
        }
        else if (GameIsRunning(folders))
        {
            result.ok = false;
            result.error = Tr("The game is running: quit it first.");
        }
        return result;
    }
}

std::span<const char* const> GameVideoKeys() { return VideoKeys; }

std::string GameVideoSection(std::string_view gameId) { return std::string(gameId) + ".video"; }

void SeedGameVideo(LauncherConfig& config, std::string_view gameId)
{
    std::string section = GameVideoSection(gameId);
    if (auto own = config.find(section); own != config.end() && !own->second.empty())
        return; // an empty section is one something only looked at (operator[]): it has nothing to keep
    auto shared = config.find("video");
    if (shared == config.end())
        return;
    for (const char* key : VideoKeys)
        if (auto value = shared->second.find(key); value != shared->second.end())
            config[section][key] = value->second;
}

namespace
{
    // `to` gets exactly the keys `from` has among the game's; the ones it lacks are dropped so the runtime's defaults apply.
    void CopyVideo(LauncherConfig& config, const std::string& from, const std::string& to)
    {
        std::map<std::string, std::string> source;
        if (auto section = config.find(from); section != config.end())
            source = section->second;
        for (const char* key : VideoKeys)
        {
            if (auto value = source.find(key); value != source.end())
                config[to][key] = value->second;
            else if (auto target = config.find(to); target != config.end())
                target->second.erase(key);
        }
    }
}

void ApplyGameVideo(LauncherConfig& config, std::string_view gameId)
{
    CopyVideo(config, GameVideoSection(gameId), "video");
    // The page shows "Display refresh rate" for a game that has no frame rate yet: the game gets it too, since its own default is 30.
    config["video"].emplace("fps_limit", "display");
}

void StoreGameVideo(LauncherConfig& config, std::string_view gameId) { CopyVideo(config, "video", GameVideoSection(gameId)); }

void MigrateFrameRates(LauncherConfig& config)
{
    std::string& done = config["launcher"]["frame_rates"];
    if (done == "1")
        return;
    done = "1";
    for (auto& [name, section] : config)
    {
        bool video = name == "video" || (name.size() > 6 && name.compare(name.size() - 6, 6, ".video") == 0);
        if (auto limit = section.find("fps_limit"); video && limit != section.end() &&
            (limit->second == "0" || limit->second == "120" || limit->second == "144" || limit->second == "240"))
            limit->second = "60";
    }
}

GameFolders FoldersOf(std::string_view gameId)
{
    GameFolders folders;
    folders.game = LauncherDataFolder() / datadir::GameFolderName(gameId);
    folders.saves = savebackup::Root(gameId);
    return folders;
}

bool GameIsRunning(const GameFolders& folders)
{
    return !folders.saves.empty() && savebackup::GameRunning(folders.saves);
}

GameDataSizes MeasureGameData(const GameFolders& folders)
{
    GameDataSizes sizes;
    for (const fs::path& path : SavePaths(folders))
        sizes.saves += BytesOf(path);
    for (const fs::path& path : CachePaths(folders))
        sizes.caches += BytesOf(path);
    std::error_code error;
    if (fs::is_directory(folders.game, error))
        for (const fs::directory_entry& entry : ListDirectory(folders.game, error))
        {
            std::string name = entry.path().filename().string();
            if (!Named(name, SaveNames) && !Named(name, CacheNames) && !Named(name, KeptNames) && !IsGameLog(entry.path()))
                sizes.installed += BytesOf(entry.path());
        }
    return sizes;
}

DataResult ClearGameCaches(const GameFolders& folders)
{
    DataResult result = Refused(folders);
    if (!result.ok)
        return result;
    for (const fs::path& path : CachePaths(folders))
        Remove(path, result);
    return result;
}

DataResult DeleteGameData(const GameFolders& folders)
{
    DataResult result = Refused(folders);
    if (!result.ok)
        return result;
    for (const fs::path& path : SavePaths(folders))
        Remove(path, result);
    for (const fs::path& path : CachePaths(folders))
        Remove(path, result);
    return result;
}

DataResult UninstallGame(const GameFolders& folders, LauncherConfig& config)
{
    DataResult result = Refused(folders);
    if (!result.ok)
        return result;
    std::error_code error;
    if (fs::is_directory(folders.game, error))
        for (const fs::directory_entry& entry : ListDirectory(folders.game, error))
        {
            std::string name = entry.path().filename().string();
            if (Named(name, SaveNames) || Named(name, KeptNames) || name == "portraits" || name == "launcher-cache")
                continue;
            Remove(entry.path(), result);
        }
    // What the settings file knows of a build in this folder: the game is no longer installed.
    for (auto section = config.begin(); section != config.end();)
    {
        auto runtime = section->second.find("runtime");
        bool build = section->first.rfind("install.", 0) == 0 && runtime != section->second.end();
        if (build)
        {
            fs::path relative = fs::path(runtime->second).lexically_relative(folders.game);
            build = !relative.empty() && *relative.begin() != "..";
        }
        section = build ? config.erase(section) : std::next(section);
    }
    return result;
}
