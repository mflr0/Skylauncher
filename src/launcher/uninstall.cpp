#include "launcher/uninstall.h"
#include "launcher/env.h"
#include "launcher/game_install.h"
#include "launcher/game_settings.h"
#include "launcher/shortcut.h"
#include "launcher/updater.h"
#include "shared/collection/collection.h"
#include "shared/collection/datadir.h"
#include "shared/dir_list.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace uninstall
{
    namespace
    {
        // ---- Path rules (pure) ---------------------------------------------------------------------------------------

        bool HasParentStep(const fs::path& path)
        {
            for (const fs::path& part : path) // as written: "a/../b" is refused, not tidied
                if (part == "..")
                    return true;
            return false;
        }

        fs::path Resolved(const fs::path& path)
        {
            std::error_code error;
            fs::path resolved = fs::weakly_canonical(path, error);
            return error ? path.lexically_normal() : resolved;
        }

        // `child` is strictly inside `parent` (the folders themselves are not "inside"), links resolved on both.
        bool Inside(const fs::path& child, const fs::path& parent)
        {
            if (child.empty() || parent.empty())
                return false;
            fs::path relative = Resolved(child).lexically_relative(Resolved(parent));
            return !relative.empty() && relative != "." && *relative.begin() != "..";
        }

        // `path` is `other` or holds it.
        bool IsOrHolds(const fs::path& path, const fs::path& other)
        {
            if (path.empty() || other.empty())
                return false;
            fs::path relative = Resolved(other).lexically_relative(Resolved(path));
            return !relative.empty() && *relative.begin() != "..";
        }

        // Never a target, nor a folder removed whole: empty, relative, with "..", the filesystem root, the home folder or
        // anything that holds it.
        bool SafePath(const fs::path& path, const fs::path& home)
        {
            if (path.empty() || !path.is_absolute() || HasParentStep(path) || !path.has_filename())
                return false;
            if (path == path.root_path() || path.lexically_normal().parent_path() == path.root_path())
                return false; // "/", "C:\" and a folder straight under it ("/Users", "/tmp")
            return home.empty() || !IsOrHolds(path, home);
        }

        bool IsLink(const fs::path& path)
        {
            std::error_code error;
            return fs::is_symlink(fs::symlink_status(path, error));
        }

        bool Exists(const fs::path& path)
        {
            std::error_code error;
            return fs::exists(fs::symlink_status(path, error));
        }

        std::string Lower(std::string text)
        {
            for (char& c : text)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        // ---- What is the player's disc ----------------------------------------------------------------------------------

        class Guard
        {
        public:
            explicit Guard(const Environment& env) : m_env(env) {}

            bool IsDisc(const fs::path& path) const
            {
                std::string extension = Lower(path.extension().string());
                if (extension == ".iso" || extension == ".xiso")
                    return true;
                std::error_code error;
                for (const fs::path& image : m_env.discImages)
                    if (!image.empty() && (path.lexically_normal() == image.lexically_normal() || fs::equivalent(path, image, error)))
                        return true;
                return false;
            }

        private:
            const Environment& m_env;
        };

        // ---- Sizes ------------------------------------------------------------------------------------------------------

        uint64_t BytesOf(const fs::path& path, const Guard& guard)
        {
            std::error_code error;
            fs::file_status status = fs::symlink_status(path, error);
            if (fs::is_symlink(status))
                return 0;
            if (fs::is_regular_file(status))
                return guard.IsDisc(path) ? 0 : fs::file_size(path, error);
            if (!fs::is_directory(status))
                return 0;
            uint64_t total = 0;
            for (const fs::directory_entry& entry : ListDirectory(path, error))
                total += BytesOf(entry.path(), guard);
            return total;
        }

        // ---- The figure folder: what is the collection's, and nothing else --------------------------------------------

        bool IsCollectionName(const std::string& name, bool directory)
        {
            if (directory)
                return name == "backups" || name == "trash";
            std::string lower = Lower(name);
            auto ends = [&](const char* suffix) { return lower.size() >= std::strlen(suffix) && lower.compare(lower.size() - std::strlen(suffix), std::strlen(suffix), suffix) == 0; };
            return lower == "collection.json" || lower == "collection.json.tmp" || ends(".lock") || ends(".sky") || ends(".bin") || ends(".dmp");
        }

        std::vector<fs::path> CollectionEntries(const fs::path& folder)
        {
            std::vector<fs::path> entries;
            std::error_code error;
            for (const fs::directory_entry& entry : ListDirectory(folder, error))
            {
                fs::file_status status = fs::symlink_status(entry.path(), error);
                if (fs::is_symlink(status))
                    continue; // never followed, never removed from here
                if (IsCollectionName(entry.path().filename().string(), fs::is_directory(status)))
                    entries.push_back(entry.path());
            }
            return entries;
        }

        // ---- Which group a thing in a game's folder belongs to ---------------------------------------------------------

        Group GroupOfGameEntry(const std::string& name, bool regularFile)
        {
            if (name == "saves" || name == "saves-backup" || name == "portal.txt" || name == "achievements.txt" || name == "playtime.txt")
                return Group::Saves;
            if (name == "figures" || name == "textures")
                return Group::Figures;
            if (name == "gamedata")
                return Group::GameData;
            if (name == "pipelines.bin" || name == "launcher-cache" || name == "portraits")
                return Group::Caches;
            if (regularFile && Lower(fs::path(name).extension().string()) == ".log" && name.rfind("install", 0) != 0)
                return Group::Caches;
            return Group::Games;
        }

        // ---- Shortcuts ---------------------------------------------------------------------------------------------------

        // A file the launcher wrote: it holds "--game", in plain text or as UTF-16 (a .lnk keeps its arguments that way).
        bool HoldsGameArgument(const fs::path& file)
        {
            std::ifstream in(file, std::ios::binary);
            std::string bytes(65536, '\0');
            in.read(bytes.data(), std::streamsize(bytes.size()));
            bytes.resize(size_t(in.gcount()));
            static const std::string wide("-\0-\0g\0a\0m\0e\0", 12);
            return bytes.find("--game") != std::string::npos || bytes.find(wide) != std::string::npos;
        }

        bool IsOurShortcut(const fs::path& path)
        {
            std::error_code error;
            fs::file_status status = fs::symlink_status(path, error);
            if (fs::is_symlink(status))
                return false;
            if (fs::is_directory(status)) // a macOS .app: a script in Contents/MacOS
            {
                for (const fs::directory_entry& entry : ListDirectory(path / "Contents" / "MacOS", error))
                    if (!IsLink(entry.path()) && fs::is_regular_file(entry.path(), error) && HoldsGameArgument(entry.path()))
                        return true;
                return false;
            }
            return fs::is_regular_file(status) && HoldsGameArgument(path);
        }

        // ---- Removing -----------------------------------------------------------------------------------------------------

        struct Remover
        {
            const Guard& guard;
            uint64_t freed = 0;
            std::vector<Left>& left;
            Group group;

            // True when `path` is gone.
            bool Tree(const fs::path& path)
            {
                std::error_code error;
                fs::file_status status = fs::symlink_status(path, error);
                if (!fs::exists(status))
                    return true;
                if (fs::is_symlink(status)) // the link, never what it points at
                    return File(path, 0);
                if (fs::is_directory(status))
                {
                    bool clear = true;
                    for (const fs::directory_entry& entry : ListDirectory(path, error))
                        clear = Tree(entry.path()) && clear;
                    if (!clear)
                        return false;
                    return File(path, 0);
                }
                if (guard.IsDisc(path))
                {
                    left.push_back({ Why::DiscImage, group, path, {} });
                    return false;
                }
                return File(path, fs::is_regular_file(status) ? fs::file_size(path, error) : 0);
            }

            bool File(const fs::path& path, uint64_t bytes)
            {
                std::error_code error;
                fs::remove(path, error);
                if (error)
                {
                    left.push_back({ Why::NotRemoved, group, path, error.message() });
                    return false;
                }
                freed += bytes;
                return true;
            }
        };

        // Why a target is refused, or nothing. `declared`: a path the environment names itself (the figure folder, the
        // settings file) that may lie outside the data folders, as a test's or a portable setup's does.
        std::optional<Why> Refuse(const fs::path& path, const Environment& env, bool declared = false)
        {
            if (!SafePath(path, env.home))
                return Why::Unsafe;
            if (IsLink(path))
            {
                // A link inside a game's own folder goes as a link (never followed); any other link is refused.
                for (const GameEntry& game : env.games)
                    if (path.parent_path().lexically_normal() == game.folder.lexically_normal() && !IsLink(game.folder) &&
                        (Inside(game.folder, env.root) || Inside(game.folder, env.launcherData)))
                        return std::nullopt;
                return Why::Link;
            }
            if (declared && (path == env.collection || path == env.settingsFile))
                return std::nullopt;
            // The pre-0.3.1 folders are roots too; a link standing for one is refused above.
            for (const fs::path& folder : env.oldFolders)
                if (!IsLink(folder) && Inside(path, folder))
                    return std::nullopt;
            if (Inside(path, env.root) || Inside(path, env.launcherData))
                return std::nullopt;
            return Why::Outside;
        }

        bool RootsSafe(const Environment& env)
        {
            return SafePath(env.root, env.home) && SafePath(env.launcherData, env.home) && !IsLink(env.root) && !IsLink(env.launcherData);
        }
    }

    Os CurrentOs()
    {
#if defined(_WIN32)
        return Os::Windows;
#elif defined(__APPLE__)
        return Os::MacOS;
#else
        return Os::Linux;
#endif
    }

    fs::path DefaultRoot(Os os, const SystemDirs& dirs)
    {
        switch (os)
        {
        case Os::Windows: return dirs.appData.empty() ? fs::path() : dirs.appData / "Skylanders";
        case Os::MacOS: return dirs.home.empty() ? fs::path() : dirs.home / "Library" / "Application Support" / "Skylanders";
        case Os::Linux:
            if (dirs.xdgData.is_absolute())
                return dirs.xdgData / "Skylanders";
            return dirs.home.empty() ? fs::path() : dirs.home / ".local" / "share" / "Skylanders";
        }
        return {};
    }

    std::string ShortcutName(Os os, const std::string& gameName)
    {
        std::string name = ShortcutFileName(gameName);
        switch (os)
        {
        case Os::MacOS: return name + ".app";
        case Os::Windows: return name + ".lnk";
        case Os::Linux:
            for (char& c : name)
                c = c == ' ' ? '-' : char(std::tolower(static_cast<unsigned char>(c)));
            return name + ".desktop";
        }
        return name;
    }

    uint64_t Plan::Bytes(Group group) const
    {
        uint64_t total = 0;
        for (const Item& item : items)
            if (item.group == group)
                total += item.bytes;
        return total;
    }

    uint64_t Plan::Total(bool withUserData) const
    {
        uint64_t total = 0;
        for (const Item& item : items)
            if (withUserData || !NeedsTick(item.group))
                total += item.bytes;
        return total;
    }

    bool Plan::Has(Group group) const
    {
        return std::any_of(items.begin(), items.end(), [&](const Item& item) { return item.group == group; });
    }

    // ---- The plan ------------------------------------------------------------------------------------------------------

    Plan MakePlan(const Environment& env)
    {
        Plan plan;
        plan.figuresInUse = env.figuresInUse;
        for (const GameEntry& game : env.games)
            if (game.running)
                plan.running.push_back(game.name);
        if (!RootsSafe(env))
        {
            plan.refused = true;
            plan.left.push_back({ Why::Unsafe, Group::Games, env.root, {} });
            return plan;
        }
        const Guard guard(env);
        auto add = [&](Group group, const std::string& game, const fs::path& path, bool declared = false, bool figureFolder = false) {
            if (!Exists(path))
                return;
            if (auto why = Refuse(path, env, declared))
            {
                plan.left.push_back({ *why, group, path, {} });
                return;
            }
            for (const Item& item : plan.items)
                if (item.path.lexically_normal() == path.lexically_normal())
                    return;
            Item item{ group, game, path, 0, figureFolder };
            if (figureFolder)
                for (const fs::path& entry : CollectionEntries(path))
                    item.bytes += BytesOf(entry, guard);
            else
                item.bytes = BytesOf(path, guard);
            plan.items.push_back(std::move(item));
        };

        // The games: what is in each one's folder, sorted into the list's groups.
        for (const GameEntry& game : env.games)
        {
            std::error_code error;
            if (!Exists(game.folder))
                continue;
            if (auto why = Refuse(game.folder, env))
            {
                plan.left.push_back({ *why, Group::Games, game.folder, {} });
                continue;
            }
            for (const fs::directory_entry& entry : ListDirectory(game.folder, error))
            {
                std::string name = entry.path().filename().string();
                add(GroupOfGameEntry(name, fs::is_regular_file(fs::symlink_status(entry.path(), error))), game.id, entry.path());
            }
            // Saves kept elsewhere than <folder>/saves (SKY_SAVE_DIR): the folder and its backups, if they are ours.
            if (!game.saves.empty())
            {
                add(Group::Saves, game.id, game.saves);
                fs::path backup = game.saves;
                backup += "-backup";
                add(Group::Saves, game.id, backup);
            }
        }

        // Tools, in the data folders (the launcher's, and the root when they differ).
        for (const fs::path& base : { env.launcherData, env.root })
        {
            add(Group::Tools, {}, base / "kit");
            add(Group::Tools, {}, base / "toolchains");
            add(Group::Caches, {}, base / "logs");
        }
        add(Group::Figures, {}, env.collection, true, true);
        add(Group::Settings, {}, env.settingsFile, true);

        // The folders of the layout before 0.3.1: their entries go; xbox-signing is kept.
        for (const fs::path& folder : env.oldFolders)
        {
            if (!Exists(folder))
                continue;
            if (!SafePath(folder, env.home) || IsLink(folder))
            {
                plan.left.push_back({ IsLink(folder) ? Why::Link : Why::Unsafe, Group::OldFolders, folder, {} });
                continue;
            }
            std::error_code error;
            for (const fs::directory_entry& entry : ListDirectory(folder, error))
            {
                if (entry.path().filename() == "xbox-signing")
                    plan.left.push_back({ Why::Kept, Group::OldFolders, entry.path(), {} });
                else
                    add(Group::OldFolders, {}, entry.path());
            }
        }

        for (const GameEntry& game : env.games)
        {
            if (env.desktop.empty())
                continue;
            fs::path shortcut = env.desktop / ShortcutName(env.os, game.name);
            if (Exists(shortcut) && IsOurShortcut(shortcut) && SafePath(shortcut, env.home) && !IsLink(shortcut))
            {
                Item item{ Group::Shortcuts, game.id, shortcut, BytesOf(shortcut, guard), false };
                plan.items.push_back(std::move(item));
            }
        }

        // Where the settings say a game was built, and it is not in its folder: the player moved it. Told, not touched.
        for (const fs::path& built : env.builtGames)
        {
            if (built.empty())
                continue;
            bool ours = std::any_of(env.games.begin(), env.games.end(), [&](const GameEntry& game) { return Inside(built, game.folder); });
            if (ours)
                continue;
            plan.left.push_back({ Exists(built) ? Why::Elsewhere : Why::Missing, Group::Games, built, {} });
        }

        // The launcher itself, last.
        LauncherRemoval removal = PlanLauncherRemoval(env);
        if (removal.ok)
        {
            Item item{ Group::Launcher, {}, removal.paths.front(), 0, false };
            for (const fs::path& path : removal.paths)
                item.bytes += BytesOf(path, guard);
            plan.items.push_back(std::move(item));
        }
        else
            plan.left.push_back({ removal.why, Group::Launcher, env.launcherSelf, removal.problem });
        return plan;
    }

    // ---- Running it ----------------------------------------------------------------------------------------------------

    Result RunUninstall(const Plan& plan, const Environment& env, bool withUserData)
    {
        Result result;
        if (plan.refused || !RootsSafe(env))
        {
            result.left.push_back({ Why::Unsafe, Group::Games, env.root, {} });
            return result;
        }
        // A game that plays, or a figure on a running portal: nothing is removed.
        bool running = plan.figuresInUse || env.figuresInUse;
        for (const GameEntry& game : env.games)
            running = running || game.running;
        if (running)
        {
            result.left.push_back({ Why::InUse, Group::Games, {}, {} });
            return result;
        }
        const Guard guard(env);
        for (const Item& item : plan.items)
        {
            if (item.group == Group::Launcher || (NeedsTick(item.group) && !withUserData))
                continue;
            if (item.group == Group::Shortcuts)
            {
                if (!Exists(item.path))
                    continue;
                if (!SafePath(item.path, env.home) || IsLink(item.path) || !IsOurShortcut(item.path))
                {
                    result.left.push_back({ Why::Unsafe, item.group, item.path, {} });
                    continue;
                }
            }
            else if (auto why = Refuse(item.path, env, item.group == Group::Figures || item.group == Group::Settings))
            {
                result.left.push_back({ *why, item.group, item.path, {} });
                continue;
            }
            if (!Exists(item.path))
                continue; // gone already
            Remover remover{ guard, 0, result.left, item.group };
            bool gone = true;
            if (item.figureFolder)
            {
                for (const fs::path& entry : CollectionEntries(item.path))
                    gone = remover.Tree(entry) && gone;
                std::error_code error;
                fs::remove(item.path, error); // only an empty folder goes
                for (const fs::directory_entry& rest : ListDirectory(item.path, error))
                {
                    (void)rest;
                    result.left.push_back({ Why::Kept, item.group, item.path, {} }); // some other file of the player's
                    break;
                }
            }
            else
                gone = remover.Tree(item.path);
            result.freed += remover.freed;
            result.removed += gone ? 1 : 0;
        }
        // The folders that are empty now (the games', the data folders', the pre-0.3.1 ones); a folder with something in it
        // stays.
        std::error_code error;
        for (const GameEntry& game : env.games)
            if (!Refuse(game.folder, env) && Exists(game.folder))
                fs::remove(game.folder, error);
        for (const fs::path& base : { env.launcherData, env.root })
            if (SafePath(base, env.home) && Exists(base))
                fs::remove(base, error);
        for (const fs::path& folder : env.oldFolders)
            if (SafePath(folder, env.home) && !IsLink(folder) && Exists(folder))
                fs::remove(folder, error);
        return result;
    }

    // ---- The launcher itself ----------------------------------------------------------------------------------------------

    LauncherRemoval PlanLauncherRemoval(const Environment& env)
    {
        LauncherRemoval removal;
        const fs::path& self = env.launcherSelf;
        auto refuse = [&](Why why, const char* problem) {
            removal.ok = false;
            removal.why = why;
            removal.problem = problem;
            removal.paths.clear();
            return removal;
        };
        if (self.empty())
            return refuse(Why::Unfit, "the launcher's own path is not known");
        if (!SafePath(self, env.home))
            return refuse(Why::Unsafe, "the path is the filesystem root, the home folder, a folder that holds it, or not absolute");
        if (IsLink(self))
            return refuse(Why::Link, "the path is a link");
        if (Inside(self, env.root) || Inside(self, env.launcherData) || Inside(self, env.collection))
            return refuse(Why::Outside, "the path is inside the data folder");
        if (Guard(env).IsDisc(self))
            return refuse(Why::DiscImage, "the path is a disc image");
        std::error_code error;
        const std::string extension = Lower(self.extension().string());
        switch (env.os)
        {
        case Os::MacOS:
            if (extension != ".app" || !fs::is_directory(self, error) || !fs::is_directory(self / "Contents" / "MacOS", error))
                return refuse(Why::Unfit, "not an application bundle");
            removal.trash = env.trash.empty() ? env.home / ".Trash" : env.trash;
            break;
        case Os::Windows:
            if (extension != ".exe" || !fs::is_regular_file(self, error))
                return refuse(Why::Unfit, "not an .exe file");
            break;
        case Os::Linux:
        {
            if (!fs::is_regular_file(self, error))
                return refuse(Why::Unfit, "not a file");
            // An AppImage lives where its owner put it, but never in a system folder. Links are followed: a folder reached
            // through /var that really is /private/var is not one, a link into /usr is.
            const fs::path resolved = Resolved(self);
            if (std::distance(resolved.begin(), resolved.end()) < 3)
                return refuse(Why::Unfit, "a system folder");
            const fs::path top = *std::next(resolved.begin());
            for (const char* system : { "usr", "bin", "sbin", "lib", "lib64", "etc", "var", "boot", "sys", "proc", "dev", "run", "snap" })
                if (top == system)
                    return refuse(Why::Unfit, "a system folder");
            break;
        }
        }
        removal.paths.push_back(self);
        fs::path old = self;
        old += ".old"; // what an update leaves (updater.h, ReplaceInPlace)
        if (Exists(old) && !IsLink(old))
            removal.paths.push_back(old);
        removal.ok = true;
        removal.why = Why::NotRemoved;
        return removal;
    }

    std::string PosixScript(Os os)
    {
        // "$1" is the process to wait for (at most a minute), then the paths: with macOS, "$2" is the Trash they go into.
        std::string script =
            "pid=$1; shift\n"
            "n=0\n"
            "while kill -0 \"$pid\" 2>/dev/null && [ \"$n\" -lt 120 ]; do sleep 0.5; n=$((n+1)); done\n";
        if (os == Os::MacOS)
        {
            script +=
                "trash=$1; shift\n"
                "mkdir -p \"$trash\"\n"
                "for src in \"$@\"; do\n"
                "  [ -e \"$src\" ] || continue\n"
                "  name=$(basename \"$src\"); dest=\"$trash/$name\"; i=1\n"
                "  while [ -e \"$dest\" ]; do\n"
                "    case \"$name\" in\n"
                "      *.*) dest=\"$trash/${name%.*} $i.${name##*.}\" ;;\n"
                "      *) dest=\"$trash/$name $i\" ;;\n"
                "    esac\n"
                "    i=$((i+1))\n"
                "  done\n"
                "  mv \"$src\" \"$dest\"\n"
                "done\n";
        }
        else
            script += "for src in \"$@\"; do rm -f -- \"$src\"; done\n";
        return script;
    }

    namespace
    {
        std::string PowerShellQuote(const std::string& text)
        {
            std::string out = "'";
            for (char c : text)
                out += c == '\'' ? std::string("''") : std::string(1, c);
            return out + "'";
        }
    }

    std::string WindowsScript(const LauncherRemoval& removal, uint32_t pid)
    {
        std::string list;
        for (const fs::path& path : removal.paths)
            list += (list.empty() ? "" : ",") + PowerShellQuote(path.string());
        std::string main = PowerShellQuote(removal.paths.empty() ? std::string() : removal.paths.front().string());
        return "$ErrorActionPreference = 'SilentlyContinue'\n"
               "Wait-Process -Id " + std::to_string(pid) + " -Timeout 60\n"
               "for ($i = 0; $i -lt 40; $i++) {\n"
               "  Remove-Item -LiteralPath " + list + " -Force\n"
               "  if (-not (Test-Path -LiteralPath " + main + ")) { break }\n"
               "  Start-Sleep -Milliseconds 500\n"
               "}\n";
    }

    std::string EncodeForPowerShell(const std::string& utf8)
    {
        std::vector<uint8_t> bytes; // UTF-16LE
        auto unit = [&](uint32_t value) { bytes.push_back(uint8_t(value & 0xFF)); bytes.push_back(uint8_t(value >> 8)); };
        for (size_t i = 0; i < utf8.size();)
        {
            uint32_t c = uint8_t(utf8[i]);
            int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            c = extra == 0 ? c : c & (0x3F >> extra);
            for (int k = 1; k <= extra && i + size_t(k) < utf8.size(); k++)
                c = (c << 6) | (uint8_t(utf8[i + size_t(k)]) & 0x3F);
            i += size_t(extra) + 1;
            if (c >= 0x10000)
            {
                c -= 0x10000;
                unit(0xD800 + (c >> 10));
                unit(0xDC00 + (c & 0x3FF));
            }
            else
                unit(c);
        }
        static const char* const Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (size_t i = 0; i < bytes.size(); i += 3)
        {
            uint32_t block = uint32_t(bytes[i]) << 16 | (i + 1 < bytes.size() ? uint32_t(bytes[i + 1]) << 8 : 0) | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
            out += Alphabet[(block >> 18) & 63];
            out += Alphabet[(block >> 12) & 63];
            out += i + 1 < bytes.size() ? Alphabet[(block >> 6) & 63] : '=';
            out += i + 2 < bytes.size() ? Alphabet[block & 63] : '=';
        }
        return out;
    }

    std::vector<std::string> ScheduleCommand(Os os, const LauncherRemoval& removal, uint32_t pid)
    {
        if (!removal.ok || removal.paths.empty())
            return {};
        if (os == Os::Windows)
            return { "powershell.exe", "-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-ExecutionPolicy", "Bypass",
                "-EncodedCommand", EncodeForPowerShell(WindowsScript(removal, pid)) };
        std::vector<std::string> command = { "/bin/sh", "-c", PosixScript(os), "sh", std::to_string(pid) };
        if (os == Os::MacOS)
            command.push_back(datadir::Text(removal.trash));
        for (const fs::path& path : removal.paths)
            command.push_back(datadir::Text(path));
        return command;
    }

    bool StartScheduled(const std::vector<std::string>& command)
    {
        if (command.empty())
            return false;
        std::vector<const char*> argv;
        for (const std::string& part : command)
            argv.push_back(part.c_str());
        argv.push_back(nullptr);
        SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true); // outlives the launcher
        SDL_Process* process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        // Not waited for and not destroyed: it runs on after Arkeyan Launcher quits.
        return process != nullptr;
    }

    // ---- This machine ----------------------------------------------------------------------------------------------------

    uint32_t ProcessId()
    {
#if defined(_WIN32)
        return uint32_t(GetCurrentProcessId());
#else
        return uint32_t(getpid());
#endif
    }

    bool ScratchRun()
    {
        for (const char* name : { "SKYLANDERS_DATA", "SKY_LAUNCHER_DATA", "SKY_LAUNCHER_INI", "SKY_COLLECTION" })
            if (const char* value = LauncherEnv(name); value != nullptr && *value != 0)
                return true;
        return false;
    }

    Environment ThisMachine(const LauncherConfig& config)
    {
        Environment env;
        env.os = CurrentOs();
        const char* home = SDL_getenv(env.os == Os::Windows ? "USERPROFILE" : "HOME");
        env.home = home != nullptr ? datadir::FromText(home) : fs::path();
        env.root = datadir::Root();
        env.launcherData = LauncherDataFolder();
        env.settingsFile = datadir::FromText(LauncherConfigPath());
        env.collection = datadir::FromText(collection::Folder());
        env.desktop = DesktopFolder();
        if (!ScratchRun())
            env.launcherSelf = LauncherSelf();
        else
        {
            const char* dummy = LauncherEnv("SKY_LAUNCHER_UNINSTALL_SELF");
            const char* trash = LauncherEnv("SKY_LAUNCHER_UNINSTALL_TRASH");
            if (trash != nullptr && *trash != 0)
                env.trash = datadir::FromText(trash);
            // A scratch run never reaches the real launcher, nor the real Trash.
            if (dummy != nullptr && *dummy != 0 && (env.os != Os::MacOS || !env.trash.empty()))
                env.launcherSelf = datadir::FromText(dummy);
        }
        // The folders of the layout before 0.3.1, when they still exist and are not the data folders themselves. A scratch
        // run never names the real ones (they would be the player's own).
        if (!ScratchRun())
        {
            const datadir::OldRoots old = datadir::RealOldRoots();
            for (const fs::path& folder : { old.ssarecomp, old.skylauncher, old.localSsarecomp })
            {
                if (folder.empty() || !Exists(folder))
                    continue;
                if (folder == env.root || folder == env.launcherData || Inside(folder, env.root) || Inside(folder, env.launcherData))
                    continue;
                if (std::find(env.oldFolders.begin(), env.oldFolders.end(), folder) == env.oldFolders.end())
                    env.oldFolders.push_back(folder);
            }
        }
        for (const GameInfo& info : g_games)
        {
            GameFolders folders = FoldersOf(info.id);
            GameEntry game{ info.id, info.name, folders.game, folders.saves, GameIsRunning(folders) };
            env.games.push_back(game);
            if (auto section = config.find(info.id); section != config.end())
                if (auto iso = section->second.find("iso"); iso != section->second.end() && !iso->second.empty())
                    env.discImages.push_back(datadir::FromText(iso->second));
        }
        for (const auto& [name, values] : config)
            if (name.rfind("install.", 0) == 0)
                if (auto runtime = values.find("runtime"); runtime != values.end() && !runtime->second.empty())
                    env.builtGames.push_back(datadir::FromText(runtime->second));
        std::error_code error;
        for (const fs::directory_entry& entry : ListDirectory(env.collection, error))
            if (entry.path().extension() == ".lock")
            {
                fs::path dump = entry.path();
                dump.replace_extension();
                if (collection::GetLock(datadir::Text(dump)).Busy())
                    env.figuresInUse = true;
            }
        return env;
    }
}
