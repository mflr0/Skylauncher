// SkylauncherTest's checks for "Uninstall everything" (launcher/uninstall.h): the plan (a dry run), what the run removes and
// keeps, the refusals, the launcher's own removal on every system's rules, and the helper that runs after the launcher quits.
// Scratch trees only: nothing here names the real data folders, the real Desktop, the real Trash, the real figures or the
// running launcher. The launcher to remove is always a dummy file or bundle made in the scratch folder.
// Called from launcher_test.cpp's main; returns the number of failures.
#include "launcher/uninstall.h"
#include "launcher/launcher.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>

namespace
{
    namespace fs = std::filesystem;
    using namespace uninstall;

    int g_failures = 0;

#define UN_EXPECT(condition) \
    do { if (!(condition)) { fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition); g_failures++; } } while (0)

    void Put(const fs::path& file, size_t bytes = 100, const std::string& text = {})
    {
        fs::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        std::string data = text;
        data.resize(std::max(bytes, text.size()), 'x');
        out << data;
    }

    // Everything under `folder` as "relative path (size)", links included as such, to compare a tree before and after.
    std::set<std::string> Listing(const fs::path& folder)
    {
        std::set<std::string> all;
        std::error_code error;
        if (!fs::exists(folder, error))
            return all;
        for (fs::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
        {
            std::error_code e;
            std::string entry = it->path().lexically_relative(folder).generic_string();
            if (it->is_symlink(e))
                entry += " -> link";
            else if (it->is_regular_file(e))
                entry += " (" + std::to_string(it->file_size(e)) + ")";
            all.insert(entry);
        }
        return all;
    }

    bool Has(const fs::path& path)
    {
        std::error_code error;
        return fs::exists(fs::symlink_status(path, error));
    }

    // A game folder as the launcher leaves it: the built game, its install logs, the player's saves and the caches.
    void MakeGame(const fs::path& folder, const std::string& runtime, bool bundle)
    {
        if (bundle)
        {
            Put(folder / (runtime + ".app") / "Contents" / "MacOS" / runtime, 1000);
            Put(folder / (runtime + ".app") / "Contents" / "Resources" / "shaders.bin", 2000);
        }
        else
            Put(folder / (runtime + ".exe"), 3000);
        Put(folder / "install.log", 500);
        Put(folder / "install-logs" / "a.log", 200);
        Put(folder / "layout.txt", 20);
        Put(folder / "saves" / "slot1.sav", 4000);
        Put(folder / "saves-backup" / "20260101" / "slot1.sav", 4000);
        Put(folder / "portal.txt", 30);
        Put(folder / "pipelines.bin", 700);
        Put(folder / "launcher-cache" / "logo.png", 800);
        Put(folder / "portraits" / "p.png", 90);
        Put(folder / (runtime + ".log"), 60);
        Put(folder / "gamedata" / "default.xex", 5000);
        Put(folder / "textures" / "pack.dds", 600);
        Put(folder / "figures" / "old.sky", 1024);
    }

    // The data tree of a player with Spyro's Adventure and Giants built, a figure collection, downloaded tools, and the
    // desktop shortcut and the settings file; plus things that must never go.
    struct Tree
    {
        fs::path base, root, outside, desktop, discs, self;
        Environment env;
    };

    Tree MakeTree(const fs::path& base, Os os)
    {
        std::error_code error;
        fs::remove_all(base, error);
        Tree tree;
        tree.base = base;
        tree.root = base / "home" / "data" / "Skylanders";
        tree.outside = base / "outside";
        tree.desktop = base / "home" / "Desktop";
        tree.discs = base / "discs";
        const bool bundle = os == Os::MacOS;
        MakeGame(tree.root / "SSA", "SSARecomp", bundle);
        MakeGame(tree.root / "Giants", "SGRecomp", bundle);
        Put(tree.root / "kit" / "v0.4.2" / "SSABuild", 9000);
        Put(tree.root / "toolchains" / "zig" / "zig", 7000);
        Put(tree.root / "logs" / "skylauncher-game.log", 300);
        Put(tree.root / "launcher.ini", 400, "[giants]\niso=");
        Put(tree.root / "Collection" / "Spyro.sky", 1024);
        Put(tree.root / "Collection" / "Gill.bin", 1024);
        Put(tree.root / "Collection" / "collection.json", 80);
        Put(tree.root / "Collection" / "backups" / "Spyro" / "1.sky", 1024);
        Put(tree.root / "Collection" / "trash" / "old.sky", 1024);
        Put(tree.root / "Collection" / "notes-to-self.txt", 50); // not the collection's: stays
        Put(tree.discs / "spyro.iso", 100000);
        Put(tree.discs / "giants.iso", 100000);
        Put(tree.outside / "precious.txt", 10);
        Put(tree.outside / "precious-folder" / "a.txt", 10);

        // Things a careless delete would reach.
        fs::create_symlink(tree.outside / "precious-folder", tree.root / "Giants" / "linked-folder", error);
        fs::create_symlink(tree.outside / "precious.txt", tree.root / "Collection" / "linked.sky", error);
        Put(tree.root / "SSA" / "copy-of-my-disc.iso", 5000); // a disc image in a game's folder: kept
        Put(tree.root / "notes.txt", 12);                      // the player's, in the data folder: kept

        // The desktop shortcuts of each system, and a file of the player's with the same kind of name.
        const std::string name = "Skylanders: Giants";
        const fs::path shortcut = tree.desktop / ShortcutName(os, name);
        if (os == Os::MacOS)
            Put(shortcut / "Contents" / "MacOS" / "run", 100, "#!/bin/sh\nexec '/Applications/Skylauncher.app' --game giants --play\n");
        else if (os == Os::Linux)
            Put(shortcut, 100, "[Desktop Entry]\nExec=\"/x/Skylauncher.AppImage\" --game giants --play\n");
        else
        {
            std::string wide;
            for (char c : std::string("--game giants --play"))
                wide += std::string(1, c) + '\0';
            Put(shortcut, 100, std::string("L\0\0\0", 4) + wide);
        }
        Put(tree.desktop / "My notes.txt", 10);

        // The launcher: a dummy of this system's kind, in a scratch Applications folder.
        const fs::path apps = tree.base / "apps";
        if (os == Os::MacOS)
        {
            tree.self = apps / "Skylauncher.app";
            Put(tree.self / "Contents" / "MacOS" / "Skylauncher", 5000);
            Put(tree.self / "Contents" / "Info.plist", 100);
        }
        else if (os == Os::Windows)
        {
            tree.self = apps / "Skylauncher.exe";
            Put(tree.self, 6000);
        }
        else
        {
            tree.self = apps / "Skylauncher.AppImage";
            Put(tree.self, 6000);
        }

        Environment& env = tree.env;
        env.os = os;
        env.home = tree.base / "home";
        env.root = tree.root;
        env.launcherData = tree.root;
        env.settingsFile = tree.root / "launcher.ini";
        env.collection = tree.root / "Collection";
        env.desktop = tree.desktop;
        env.launcherSelf = tree.self;
        env.trash = tree.base / "home" / ".Trash";
        env.games = { { "ssa", "Skylanders: Spyro's Adventure", tree.root / "SSA", tree.root / "SSA" / "saves", false },
                      { "giants", "Skylanders: Giants", tree.root / "Giants", tree.root / "Giants" / "saves", false } };
        env.discImages = { tree.discs / "spyro.iso", tree.discs / "giants.iso" };
        env.builtGames = { tree.root / "SSA" / (bundle ? "SSARecomp.app" : "SSARecomp.exe"), tree.root / "Giants" / (bundle ? "SGRecomp.app" : "SGRecomp.exe") };
        return tree;
    }

    // ---- The rules of each system, as path logic -------------------------------------------------------------------

    void LayoutChecks()
    {
        SystemDirs dirs{ "/home/p", "/win/AppData/Roaming", {} };
        UN_EXPECT(DefaultRoot(Os::Windows, dirs) == fs::path("/win/AppData/Roaming") / "Skylanders");
        UN_EXPECT(DefaultRoot(Os::MacOS, dirs) == fs::path("/home/p/Library/Application Support/Skylanders"));
        UN_EXPECT(DefaultRoot(Os::Linux, dirs) == fs::path("/home/p/.local/share/Skylanders"));
        dirs.xdgData = "/xdg/data";
        UN_EXPECT(DefaultRoot(Os::Linux, dirs) == fs::path("/xdg/data/Skylanders"));
        dirs.xdgData = "relative/data"; // the specification: a relative XDG_DATA_HOME is ignored
        UN_EXPECT(DefaultRoot(Os::Linux, dirs) == fs::path("/home/p/.local/share/Skylanders"));
        UN_EXPECT(DefaultRoot(Os::Windows, {}).empty() && DefaultRoot(Os::MacOS, {}).empty());

        UN_EXPECT(ShortcutName(Os::MacOS, "Skylanders: Giants") == "Skylanders Giants.app");
        UN_EXPECT(ShortcutName(Os::Windows, "Skylanders: Giants") == "Skylanders Giants.lnk");
        UN_EXPECT(ShortcutName(Os::Linux, "Skylanders: Giants") == "skylanders-giants.desktop");

        // On this machine, with nothing overriding it, the shared folder code agrees with the table above.
        const char* names[] = { "SKYLANDERS_DATA", "XDG_DATA_HOME" };
        std::map<std::string, std::string> saved;
        for (const char* name : names)
            if (const char* value = SDL_getenv(name))
                saved[name] = value;
        for (const char* name : names)
            SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), name), SDL_unsetenv_unsafe(name);
        SystemDirs here;
        if (const char* home = SDL_getenv("HOME"))
            here.home = home;
        if (const char* appdata = SDL_getenv("APPDATA"))
            here.appData = appdata;
        UN_EXPECT(DefaultRoot(CurrentOs(), here) == ThisMachine({}).root || SDL_getenv("SKYLAUNCHER_DATA") != nullptr);
        for (const auto& [name, value] : saved)
            SDL_setenv_unsafe(name.c_str(), value.c_str(), 1), SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name.c_str(), value.c_str(), true);
    }

    // ---- The plan and the run --------------------------------------------------------------------------------------

    void PlanAndRunChecks(const fs::path& base, Os os)
    {
        Tree tree = MakeTree(base / (os == Os::MacOS ? "mac" : os == Os::Windows ? "win" : "linux"), os);
        const Environment& env = tree.env;
        const std::set<std::string> before = Listing(tree.base);
        const std::set<std::string> outsideBefore = Listing(tree.outside), discsBefore = Listing(tree.discs);

        // The dry run lists what is there, with sizes, and touches nothing.
        Plan plan = MakePlan(env);
        UN_EXPECT(Listing(tree.base) == before);
        UN_EXPECT(!plan.refused && plan.running.empty() && !plan.figuresInUse);
        UN_EXPECT(plan.Has(Group::Games) && plan.Has(Group::GameData) && plan.Has(Group::Tools) && plan.Has(Group::Caches) &&
            plan.Has(Group::Settings) && plan.Has(Group::Shortcuts) && plan.Has(Group::Launcher) && plan.Has(Group::Saves) && plan.Has(Group::Figures));
        UN_EXPECT(plan.Bytes(Group::GameData) == 10000);   // both gamedata folders
        UN_EXPECT(plan.Bytes(Group::Tools) == 16000);      // kit + toolchains
        UN_EXPECT(plan.Bytes(Group::Settings) == 400);
        UN_EXPECT(plan.Bytes(Group::Saves) == 2 * (4000 + 4000 + 30));
        // Figures: the collection's own files (not notes-to-self.txt, not the link), and each game's figures and textures.
        UN_EXPECT(plan.Bytes(Group::Figures) == (1024 * 4 + 80) + 2 * (1024 + 600));
        UN_EXPECT(plan.Total(true) - plan.Total(false) == plan.Bytes(Group::Saves) + plan.Bytes(Group::Figures));
        // The disc image in the game's folder is not counted: it is not going.
        uint64_t spyroGame = 0;
        for (const Item& item : plan.items)
            if (item.group == Group::Games && item.game == "ssa")
                spyroGame += item.bytes;
        UN_EXPECT(spyroGame == (os == Os::MacOS ? 3000u : 3000u) + 500 + 200 + 20 + 0);
        for (const Item& item : plan.items)
        {
            UN_EXPECT(item.path.string().rfind(tree.outside.string(), 0) != 0); // nothing outside
            UN_EXPECT(item.path.filename() != "precious.txt" && item.path.filename() != "notes.txt" && item.path.filename() != "My notes.txt");
        }
        // The launcher is listed with its size (and nothing else is done about it in RunUninstall).
        UN_EXPECT(plan.Bytes(Group::Launcher) == (os == Os::MacOS ? 5100u : 6000u));

        // Nothing built elsewhere: the settings' builds are all in their folders.
        UN_EXPECT(std::none_of(plan.left.begin(), plan.left.end(), [](const Left& left) { return left.why == Why::Elsewhere; }));

        // Without the tick: everything but the player's saves and figures; the launcher is left to its helper.
        Result without = RunUninstall(plan, env, false);
        UN_EXPECT(!Has(tree.root / "SSA" / (os == Os::MacOS ? "SSARecomp.app" : "SSARecomp.exe")));
        UN_EXPECT(!Has(tree.root / "Giants" / (os == Os::MacOS ? "SGRecomp.app" : "SGRecomp.exe")));
        UN_EXPECT(!Has(tree.root / "SSA" / "gamedata") && !Has(tree.root / "SSA" / "pipelines.bin") && !Has(tree.root / "Giants" / "launcher-cache"));
        UN_EXPECT(!Has(tree.root / "kit") && !Has(tree.root / "toolchains") && !Has(tree.root / "logs") && !Has(tree.root / "launcher.ini"));
        UN_EXPECT(!Has(tree.desktop / ShortcutName(os, "Skylanders: Giants")));
        UN_EXPECT(Has(tree.root / "SSA" / "saves" / "slot1.sav") && Has(tree.root / "Giants" / "saves-backup" / "20260101" / "slot1.sav") &&
            Has(tree.root / "SSA" / "portal.txt"));
        UN_EXPECT(Has(tree.root / "Collection" / "Spyro.sky") && Has(tree.root / "Giants" / "figures" / "old.sky") && Has(tree.root / "Giants" / "textures" / "pack.dds"));
        UN_EXPECT(Has(tree.self)); // the launcher is not removed here
        UN_EXPECT(without.freed > 0 && without.removed > 0);

        // With the tick: the saves and figures too, but never the disc images, the links' targets, or what is not ours.
        Result with = RunUninstall(plan, env, true);
        UN_EXPECT(!Has(tree.root / "SSA" / "saves") && !Has(tree.root / "Giants" / "saves-backup") && !Has(tree.root / "SSA" / "portal.txt"));
        UN_EXPECT(!Has(tree.root / "Collection" / "Spyro.sky") && !Has(tree.root / "Collection" / "Gill.bin") && !Has(tree.root / "Collection" / "collection.json") &&
            !Has(tree.root / "Collection" / "backups") && !Has(tree.root / "Collection" / "trash"));
        UN_EXPECT(!Has(tree.root / "Giants" / "figures") && !Has(tree.root / "Giants" / "textures"));
        UN_EXPECT(Has(tree.root / "Collection" / "notes-to-self.txt"));             // not a figure: stays, and says so
        UN_EXPECT(Has(tree.root / "SSA" / "copy-of-my-disc.iso"));                  // a disc image stays, and so does its folder
        UN_EXPECT(Has(tree.root / "notes.txt") && Has(tree.root));                  // the player's file in the data folder
        UN_EXPECT(!Has(tree.root / "Giants" / "linked-folder"));                    // a link in a game's folder goes as a link...
        UN_EXPECT(Has(tree.root / "Collection" / "linked.sky"));                    // ...one in the figure folder is left alone
        UN_EXPECT(Listing(tree.outside) == outsideBefore && Listing(tree.discs) == discsBefore); // and what they pointed at never moves
        UN_EXPECT(Has(tree.desktop / "My notes.txt") && Has(tree.self));
        UN_EXPECT(std::any_of(with.left.begin(), with.left.end(), [](const Left& left) { return left.why == Why::DiscImage; }));
        UN_EXPECT(std::any_of(with.left.begin(), with.left.end(), [](const Left& left) { return left.why == Why::Kept; }));
        (void)without;
        printf("Uninstall (%s layout): plan with sizes, the run without and with the tick, discs, links and strangers kept\n",
            os == Os::MacOS ? "macOS" : os == Os::Windows ? "Windows" : "Linux");
    }

    // ---- A scratch folder reached through a link ($TMPDIR is /var/... on macOS, really /private/var/...) ------------------

    void SymlinkChecks(const fs::path& scratch)
    {
        // The whole plan and run, with every path written through a link to the folder that holds the trees.
        const fs::path real = scratch / "via-link-real", link = scratch / "via-link";
        fs::remove_all(real);
        std::error_code error;
        fs::remove(link, error);
        fs::create_directories(real);
        fs::create_symlink(real, link, error);
        UN_EXPECT(!error);
        for (Os os : { Os::MacOS, Os::Windows, Os::Linux })
            PlanAndRunChecks(link, os);
        // The links are followed in both directions: the same trees written by their real path are the same tree.
        UN_EXPECT(Has(real / "linux" / "apps" / "Skylauncher.AppImage") && Has(real / "mac" / "apps" / "Skylauncher.app"));

        // A launcher reached through a link into a system folder is still refused, and one in an ordinary folder is not.
        const fs::path system = "/usr/bin/true";
        if (Has(system))
        {
            fs::create_symlink("/usr", scratch / "via-link-usr", error);
            Tree tree = MakeTree(scratch / "via-link-system", Os::Linux);
            tree.env.launcherSelf = scratch / "via-link-usr" / "bin" / "true";
            LauncherRemoval removal = PlanLauncherRemoval(tree.env);
            UN_EXPECT(!removal.ok && removal.paths.empty());
            tree.env.launcherSelf = tree.self;
            UN_EXPECT(PlanLauncherRemoval(tree.env).ok);
        }
        printf("Uninstall through a link: the plan and the run work on a folder reached by a link, a link into a system folder is refused\n");
    }

    // ---- The folders of the layout before 0.3.1 ---------------------------------------------------------------------

    void OldFolderChecks(const fs::path& base)
    {
        Tree tree = MakeTree(base / "old-folders", Os::Linux);
        const fs::path old = tree.base / "home" / "SSARecomp";
        const fs::path gone = tree.base / "home" / "Skylauncher";
        Put(old / "a.txt", 10);
        Put(old / "xbox-signing" / "key", 20);
        Put(gone / "b.txt", 30);
        Environment env = tree.env;
        env.oldFolders = { old, gone };

        Plan plan = MakePlan(env);
        bool listed = false, kept = false;
        for (const Item& item : plan.items)
            if (item.group == Group::OldFolders && item.path == old / "a.txt")
                listed = item.bytes == 10;
        for (const Left& left : plan.left)
            if (left.why == Why::Kept && left.group == Group::OldFolders && left.path == old / "xbox-signing")
                kept = true;
        UN_EXPECT(listed && kept);
        UN_EXPECT(std::none_of(plan.items.begin(), plan.items.end(), [&](const Item& item) { return item.path.filename() == "xbox-signing"; }));

        RunUninstall(plan, env, false);
        UN_EXPECT(!Has(old / "a.txt") && Has(old / "xbox-signing" / "key") && Has(old));
        UN_EXPECT(!Has(gone)); // its entries went, and it was empty
        printf("Uninstall of the pre-0.3.1 folders: their entries go, xbox-signing stays, an emptied folder goes\n");
    }

    void RefusalChecks(const fs::path& base)
    {
        Tree tree = MakeTree(base / "refuse", Os::Linux);
        const std::set<std::string> before = Listing(tree.base);

        // A game or a figure in use: nothing is removed.
        {
            Environment env = tree.env;
            env.games[0].running = true;
            Plan plan = MakePlan(env);
            UN_EXPECT(plan.running.size() == 1);
            Result result = RunUninstall(plan, env, true);
            UN_EXPECT(result.left.size() == 1 && result.left[0].why == Why::InUse && Listing(tree.base) == before);
            env.games[0].running = false;
            env.figuresInUse = true;
            result = RunUninstall(MakePlan(env), env, true);
            UN_EXPECT(result.left.size() == 1 && result.left[0].why == Why::InUse && Listing(tree.base) == before);
        }
        // Unsafe roots refuse everything: empty, '/', a folder under it, the home folder, a folder that holds it, relative, ".."
        const fs::path home = tree.env.home;
        for (const fs::path& root : { fs::path(), fs::path("/"), fs::path("/tmp"), home, home.parent_path(), fs::path("relative/Skylanders"),
                 tree.root / ".." / "data" })
        {
            Environment env = tree.env;
            env.root = env.launcherData = root;
            Plan plan = MakePlan(env);
            UN_EXPECT(plan.refused && plan.items.empty());
            Result result = RunUninstall(plan, env, true);
            UN_EXPECT(result.freed == 0 && result.left.size() == 1 && result.left[0].why == Why::Unsafe && Listing(tree.base) == before);
        }
        // A plan that was tampered with: a path outside, the home folder, the root, a link, relative.
        {
            Plan forged;
            for (const fs::path& path : { tree.outside / "precious.txt", home, tree.root, fs::path("/"), tree.root / "SSA" / ".." / ".." / "outside", fs::path("kit") })
                forged.items.push_back({ Group::Games, "ssa", path, 1, false });
            forged.items.push_back({ Group::Figures, {}, tree.outside / "precious-folder", 1, true }); // as if it were the figure folder
            forged.items.push_back({ Group::Settings, {}, tree.outside / "precious.txt", 1, false });
            Result result = RunUninstall(forged, tree.env, true);
            UN_EXPECT(result.freed == 0 && Listing(tree.base) == before);
            UN_EXPECT(result.left.size() == forged.items.size());
        }
        // A game folder that is a link to elsewhere: refused whole, not followed.
        {
            fs::remove_all(tree.root / "Giants");
            std::error_code error;
            fs::create_symlink(tree.outside, tree.root / "Giants", error);
            const std::set<std::string> outsideBefore = Listing(tree.outside);
            Plan plan = MakePlan(tree.env);
            UN_EXPECT(std::any_of(plan.left.begin(), plan.left.end(), [](const Left& left) { return left.why == Why::Link; }));
            RunUninstall(plan, tree.env, true);
            UN_EXPECT(Listing(tree.outside) == outsideBefore);
            fs::remove(tree.root / "Giants");
        }
        // The data folder itself a link: refused.
        {
            Tree other = MakeTree(base / "refuse-link", Os::Linux);
            const fs::path real = other.base / "home" / "data" / "Real";
            fs::rename(other.root, real);
            std::error_code error;
            fs::create_symlink(real, other.root, error);
            other.env.launcherData = other.env.root;
            Plan plan = MakePlan(other.env);
            UN_EXPECT(plan.refused);
            UN_EXPECT(Listing(real).size() > 10);
        }
        // Built elsewhere (the player moved the game folder): said, not touched.
        {
            Tree moved = MakeTree(base / "refuse-moved", Os::Linux);
            const fs::path away = moved.base / "away";
            fs::rename(moved.root / "Giants", away);
            moved.env.games[1].folder = moved.root / "Giants"; // where the launcher looks: gone
            moved.env.builtGames[1] = away / "SGRecomp.exe";   // where the settings say it is built: moved
            moved.env.builtGames.push_back(moved.base / "nowhere" / "SGRecomp.exe"); // and a build that is simply gone
            Plan plan = MakePlan(moved.env);
            UN_EXPECT(std::count_if(plan.left.begin(), plan.left.end(), [](const Left& left) { return left.why == Why::Elsewhere; }) == 1);
            UN_EXPECT(std::count_if(plan.left.begin(), plan.left.end(), [](const Left& left) { return left.why == Why::Missing; }) == 1);
            const std::set<std::string> awayBefore = Listing(away);
            RunUninstall(plan, moved.env, true);
            UN_EXPECT(Listing(away) == awayBefore);
        }
        // Saves kept outside the data folder (SSA_SAVE_DIR): not ours to remove.
        {
            Tree odd = MakeTree(base / "refuse-saves", Os::Linux);
            odd.env.games[0].saves = odd.outside / "my-saves";
            Put(odd.outside / "my-saves" / "s.sav", 10);
            Plan plan = MakePlan(odd.env);
            UN_EXPECT(std::any_of(plan.left.begin(), plan.left.end(), [](const Left& left) { return left.why == Why::Outside; }));
            RunUninstall(plan, odd.env, true);
            UN_EXPECT(Has(odd.outside / "my-saves" / "s.sav"));
        }
        printf("Uninstall refusals: a game running, unsafe roots, a forged plan, links, a moved game, saves elsewhere\n");
    }

    // ---- The launcher itself, on every system's rules ------------------------------------------------------------

    std::string Decode(const std::string& base64)
    {
        std::string utf16;
        int bits = 0, value = 0;
        for (char c : base64)
        {
            if (c == '=')
                break;
            const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            value = (value << 6) | int(std::string(alphabet).find(c));
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                utf16 += char((value >> bits) & 0xFF);
            }
        }
        std::string text; // ASCII and BMP text only, enough for the checks
        for (size_t i = 0; i + 1 < utf16.size(); i += 2)
        {
            unsigned unit = uint8_t(utf16[i]) | uint8_t(utf16[i + 1]) << 8;
            if (unit < 0x80)
                text += char(unit);
            else
                text += unit < 0x800 ? std::string{ char(0xC0 | (unit >> 6)), char(0x80 | (unit & 0x3F)) }
                                      : std::string{ char(0xE0 | (unit >> 12)), char(0x80 | ((unit >> 6) & 0x3F)), char(0x80 | (unit & 0x3F)) };
        }
        return text;
    }

    // Runs the script as a helper would, with a process that is already gone, and waits for it.
    bool RunHelper(const std::vector<std::string>& command)
    {
        std::vector<const char*> argv;
        for (const std::string& part : command)
            argv.push_back(part.c_str());
        argv.push_back(nullptr);
        SDL_Process* process = SDL_CreateProcess(argv.data(), false);
        if (process == nullptr)
            return false;
        int code = -1;
        SDL_WaitProcess(process, true, &code);
        SDL_DestroyProcess(process);
        return code == 0;
    }

    uint32_t DeadPid()
    {
        const char* argv[] = { "/usr/bin/true", nullptr };
        SDL_Process* process = SDL_CreateProcess(argv, false);
        uint32_t pid = process == nullptr ? 999999 : uint32_t(SDL_GetNumberProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_PID_NUMBER, 999999));
        if (process != nullptr)
        {
            SDL_WaitProcess(process, true, nullptr);
            SDL_DestroyProcess(process);
        }
        return pid;
    }

    void LauncherChecks(const fs::path& base)
    {
        for (Os os : { Os::MacOS, Os::Windows, Os::Linux })
        {
            Tree tree = MakeTree(base / "launcher" / (os == Os::MacOS ? "mac" : os == Os::Windows ? "win" : "linux"), os);
            Environment env = tree.env;
            // An update leaves a .old beside the launcher: it goes too.
            fs::path old = tree.self;
            old += ".old";
            if (os == Os::MacOS)
                Put(old / "Contents" / "MacOS" / "Skylauncher", 10);
            else
                Put(old, 10);
            LauncherRemoval removal = PlanLauncherRemoval(env);
            UN_EXPECT(removal.ok && removal.paths.size() == 2 && removal.paths[0] == tree.self && removal.paths[1] == old);
            UN_EXPECT(os != Os::MacOS || removal.trash == tree.base / "home" / ".Trash");

            // What does not fit this system is refused: the other systems' kinds, a folder, a link, a data folder, the disc.
            auto refused = [&](const fs::path& self, Why why) {
                Environment other = env;
                other.launcherSelf = self;
                LauncherRemoval r = PlanLauncherRemoval(other);
                return !r.ok && r.why == why && r.paths.empty() && ScheduleCommand(os, r, 1).empty();
            };
            UN_EXPECT(refused({}, Why::Unfit));
            UN_EXPECT(refused("/", Why::Unsafe) && refused(env.home, Why::Unsafe) && refused(env.home.parent_path(), Why::Unsafe));
            UN_EXPECT(refused("Skylauncher.exe", Why::Unsafe) && refused(tree.base / "apps" / ".." / ".." / "x.exe", Why::Unsafe));
            UN_EXPECT(refused(tree.root / "SSA" / (os == Os::MacOS ? "SSARecomp.app" : "SSARecomp.exe"), Why::Outside)); // inside the data folder
            UN_EXPECT(refused(tree.discs / "spyro.iso", Why::DiscImage));
            UN_EXPECT(refused(tree.base / "apps" / "missing", Why::Unfit));
            if (os == Os::MacOS)
                UN_EXPECT(refused(tree.base / "apps" / "Skylauncher.exe", Why::Unfit));
            else
                UN_EXPECT(refused(tree.base / "apps", Why::Unfit)); // a folder
            if (os == Os::Windows)
            {
                Put(tree.base / "apps" / "tool.sh", 10);
                UN_EXPECT(refused(tree.base / "apps" / "tool.sh", Why::Unfit));
            }
            if (os == Os::Linux)
                for (const char* system : { "/usr/bin/env", "/bin/sh" })
                    UN_EXPECT(refused(system, Why::Unfit) || refused(system, Why::Link) || refused(system, Why::Unsafe)); // never a system file, whatever it is
            std::error_code error;
            fs::path link = tree.base / "apps" / (os == Os::MacOS ? "Linked.app" : os == Os::Windows ? "Linked.exe" : "Linked.AppImage");
            fs::create_symlink(tree.self, link, error);
            UN_EXPECT(refused(link, Why::Link));

            // The command each system runs after the launcher quits.
            auto command = ScheduleCommand(os, removal, 4242);
            if (os == Os::Windows)
            {
                UN_EXPECT(command.size() == 9 && command[0] == "powershell.exe" && command[7] == "-EncodedCommand");
                std::string script = Decode(command[8]);
                UN_EXPECT(script == WindowsScript(removal, 4242));
                UN_EXPECT(script.find("Wait-Process -Id 4242") != std::string::npos && script.find(tree.self.string()) != std::string::npos);
                UN_EXPECT(script.find("Remove-Item -LiteralPath") != std::string::npos);
            }
            else
            {
                UN_EXPECT(command.size() >= 6 && command[0] == "/bin/sh" && command[1] == "-c" && command[2] == PosixScript(os) && command[4] == "4242");
                UN_EXPECT(command.back() == old.string() && std::find(command.begin(), command.end(), tree.self.string()) != command.end());
                UN_EXPECT((os == Os::MacOS) == (std::find(command.begin(), command.end(), removal.trash.string()) != command.end()));
            }
            // A quote in a name cannot end the string (PowerShell): doubled.
            LauncherRemoval quote = removal;
            quote.paths = { fs::path("C:/Users/o'brien/Skylauncher.exe") };
            UN_EXPECT(WindowsScript(quote, 1).find("'C:/Users/o''brien/Skylauncher.exe'") != std::string::npos);
            UN_EXPECT(Decode(EncodeForPowerShell("a\xC3\xA9 b")) == "a\xC3\xA9 b" && Decode(EncodeForPowerShell("")).empty());
        }

        // The helper itself, run for real on a copy of a dummy in scratch (macOS and Linux scripts; the process it waits for is gone).
        const fs::path run = base / "helper";
        fs::remove_all(run);
        const fs::path trash = run / "Trash";
        {
            Tree mac = MakeTree(run / "mac", Os::MacOS);
            Put(trash / "Skylauncher.app" / "Contents" / "old.txt", 5); // a name already taken in the Trash
            fs::path old = mac.self;
            old += ".old";
            Put(old / "Contents" / "MacOS" / "Skylauncher", 10);
            mac.env.trash = trash;
            LauncherRemoval removal = PlanLauncherRemoval(mac.env);
            UN_EXPECT(removal.ok && removal.trash == trash);
            UN_EXPECT(RunHelper(ScheduleCommand(Os::MacOS, removal, DeadPid())));
            UN_EXPECT(!Has(mac.self) && !Has(old));
            UN_EXPECT(Has(trash / "Skylauncher.app" / "Contents" / "old.txt") && Has(trash / "Skylauncher 1.app" / "Contents" / "MacOS" / "Skylauncher") &&
                Has(trash / "Skylauncher.app.old"));
            UN_EXPECT(Has(mac.root / "notes.txt")); // nothing else was touched
        }
        {
            Tree linux = MakeTree(run / "linux", Os::Linux);
            LauncherRemoval removal = PlanLauncherRemoval(linux.env);
            UN_EXPECT(removal.ok && RunHelper(ScheduleCommand(Os::Linux, removal, DeadPid())) && !Has(linux.self) && Has(linux.root / "notes.txt"));
        }
        {
            // It waits for the launcher to end: a live process holds it back, then it runs once that process is gone.
            Tree linux = MakeTree(run / "linux-wait", Os::Linux);
            LauncherRemoval removal = PlanLauncherRemoval(linux.env);
            const char* sleeper[] = { "/bin/sleep", "1", nullptr };
            SDL_Process* live = SDL_CreateProcess(sleeper, false);
            uint32_t pid = live == nullptr ? 0 : uint32_t(SDL_GetNumberProperty(SDL_GetProcessProperties(live), SDL_PROP_PROCESS_PID_NUMBER, 0));
            if (pid != 0)
            {
                std::vector<std::string> command = ScheduleCommand(Os::Linux, removal, pid);
                std::vector<const char*> argv;
                for (const std::string& part : command)
                    argv.push_back(part.c_str());
                argv.push_back(nullptr);
                SDL_Process* helper = SDL_CreateProcess(argv.data(), false);
                SDL_Delay(300);
                UN_EXPECT(Has(linux.self)); // the sleeper still runs: not yet
                SDL_WaitProcess(live, true, nullptr);
                SDL_WaitProcess(helper, true, nullptr);
                UN_EXPECT(!Has(linux.self));
                SDL_DestroyProcess(helper);
                SDL_DestroyProcess(live);
            }
        }
        printf("Uninstall of the launcher: each system's rules, the refusals, the commands, the helper run on a dummy in scratch\n");
    }

    // ---- This machine ---------------------------------------------------------------------------------------------

    void MachineChecks(const fs::path& base)
    {
        // With scratch variables the launcher is never the target, and the real Trash is never named.
        const fs::path data = base / "machine" / "data";
        fs::create_directories(data);
        auto set = [](const char* name, const std::string& value) {
            if (value.empty())
                SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), name), SDL_unsetenv_unsafe(name);
            else
                SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value.c_str(), true), SDL_setenv_unsafe(name, value.c_str(), 1);
        };
        const char* oldData = SDL_getenv("SKYLANDERS_DATA");
        const std::string saved = oldData != nullptr ? oldData : std::string();
        set("SKYLANDERS_DATA", data.string());
        set("SKYLAUNCHER_UNINSTALL_SELF", "");
        set("SKYLAUNCHER_UNINSTALL_TRASH", "");
        UN_EXPECT(ScratchRun());
        UN_EXPECT(ThisMachine({}).launcherSelf.empty());
        const fs::path dummy = base / "machine" / "Skylauncher.app";
        Put(dummy / "Contents" / "MacOS" / "Skylauncher", 10);
        set("SKYLAUNCHER_UNINSTALL_SELF", dummy.string());
        Environment env = ThisMachine({});
        UN_EXPECT(env.os != Os::MacOS || env.launcherSelf.empty()); // macOS: no Trash named, so no launcher
        set("SKYLAUNCHER_UNINSTALL_TRASH", (base / "machine" / "Trash").string());
        env = ThisMachine({});
        UN_EXPECT(env.launcherSelf == dummy && env.trash == base / "machine" / "Trash");
        UN_EXPECT(env.root == data && !env.games.empty());
        // None of its paths is in the real application folders.
        const std::string home = SDL_getenv("HOME") != nullptr ? SDL_getenv("HOME") : "";
        if (!home.empty())
            for (const char* real : { "/Library/Application Support/Skylanders", "/Library/Application Support/SSARecomp", "/Library/Application Support/Skylauncher",
                     "/.local/share/Skylanders", "/.Trash" })
                for (const fs::path& path : { env.root, env.launcherData, env.settingsFile, env.collection, env.trash })
                    UN_EXPECT(path.string().rfind(home + real, 0) != 0);
        set("SKYLAUNCHER_UNINSTALL_SELF", "");
        set("SKYLAUNCHER_UNINSTALL_TRASH", "");
        set("SKYLANDERS_DATA", saved);
        printf("Uninstall on this machine: scratch variables keep the launcher, the Trash and the real folders out of it\n");
    }
}

int UninstallChecks(const fs::path& scratch)
{
    const fs::path base = scratch / "uninstall";
    fs::remove_all(base);
    fs::create_directories(base);
    LayoutChecks();
    for (Os os : { Os::MacOS, Os::Windows, Os::Linux })
        PlanAndRunChecks(base, os);
    SymlinkChecks(scratch);
    OldFolderChecks(base);
    RefusalChecks(base);
    LauncherChecks(base);
    MachineChecks(base);
    return g_failures;
}
