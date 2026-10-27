#pragma once

// "Uninstall everything" (Settings > Launcher; docs/install.md, "Uninstall everything"): what it removes, worked out first
// (MakePlan: a dry run that measures and touches nothing) and done after (RunUninstall). No UI and no SDL calls in the
// plan and the executor, so SkylauncherTest runs both on scratch folders for each system's layout.
//
// Safety, because this deletes files:
//  - Targets are only ever worked out from the Environment: the data root, Skylauncher's data folder, each game's own
//    folder, the shared figure folder and the launcher itself. Nothing else is named, and nothing is searched for.
//  - A target must lie inside one of those roots (after resolving links), must not be a link itself, and never is the
//    filesystem root, the user's home or a folder that holds it, or empty. A root that fails these refuses everything.
//  - Disc images are never deleted: a path the settings remember as a disc, and any .iso / .xiso file, is skipped, and a
//    folder that holds one is kept around it.
//  - Inside a folder a link is removed as a link, never followed. The figure folder is cleaned of figures and the
//    collection's own files only; anything else in it stays.
//  - The launcher's own removal (the .app, the .exe, the AppImage) is a separate, later step with an injectable path:
//    PlanLauncherRemoval says what the helper will do, ScheduleCommand turns it into a command that runs once the
//    launcher has quit. Nothing here starts it.
#include "launcher/launcher.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace uninstall
{
    enum class Os { Windows, MacOS, Linux };
    Os CurrentOs();

    // What the player sees in the list, in this order. The last three go only when the box "Also delete my saves and my
    // figures" is ticked.
    enum class Group
    {
        Games,     // each built game (and what its install left)
        GameData,  // the copies of the discs' game data
        Tools,     // the kits and the compilers downloaded
        Caches,    // shader caches, the art cache, portraits, logs
        Settings,  // launcher.ini
        Shortcuts, // the desktop shortcuts the launcher made
        Launcher,  // Skylauncher itself
        Saves,     // saves, their backups, the seats of the next session
        Figures,   // the figure collection, the games' own figure folders, texture packs
    };
    constexpr bool NeedsTick(Group group) { return group == Group::Saves || group == Group::Figures; }

    struct Item
    {
        Group group = Group::Games;
        std::string game;           // the game's id; empty for what belongs to none
        std::filesystem::path path; // a file or a folder that exists
        uint64_t bytes = 0;
        // A folder that is cleaned of known names only (the figure folder): its other files are left.
        bool figureFolder = false;
    };

    // Why a path was left alone, for the result message.
    enum class Why
    {
        Elsewhere,  // the game is not in its folder (moved by the player, or set up by hand): its folder was not touched
        Missing,    // the settings know a game build that is no longer there
        Link,       // a link: never followed
        Outside,    // not inside a folder of ours
        Unsafe,     // the filesystem root, the home folder, relative paths...
        DiscImage,  // a disc image or a folder around one
        InUse,      // a game or a figure in use
        NotRemoved, // the system refused (the error is in `detail`)
        Unfit,      // not a launcher we know how to remove (not a .app, .exe...)
        Kept,       // other files in the figure folder
    };
    struct Left
    {
        Why why{};
        Group group = Group::Games;
        std::filesystem::path path;
        std::string detail;
    };

    // Where each thing is. ThisMachine() fills it for the running launcher; tests fill it with scratch folders and any
    // system's rules.
    struct GameEntry
    {
        std::string id;
        std::string name;
        std::filesystem::path folder; // <data>/SSA, <data>/Giants...
        std::filesystem::path saves;  // its saves folder (<saves>-backup beside it); empty: <folder>/saves
        bool running = false;         // its lock says it plays now
    };
    struct Environment
    {
        Os os = Os::Linux;
        std::filesystem::path home;         // the user's home folder, never a target (nor anything that holds it)
        std::filesystem::path root;         // datadir::Root(): <data>/Skylanders
        std::filesystem::path launcherData; // Skylauncher's own data folder (the root unless SKYLAUNCHER_DATA says otherwise)
        std::filesystem::path settingsFile; // launcher.ini
        std::filesystem::path collection;   // the figure folder
        std::filesystem::path desktop;      // where the shortcuts are
        std::filesystem::path launcherSelf; // the .app, .exe or AppImage; empty when it cannot be removed (a build folder)
        std::filesystem::path trash;        // macOS: where the .app goes (~/.Trash); empty: <home>/.Trash
        std::vector<GameEntry> games;
        std::vector<std::filesystem::path> discImages; // the images the settings remember
        std::vector<std::filesystem::path> builtGames; // [install.*] runtime= : where the settings say a game was built
        bool figuresInUse = false;                     // a figure is on a running game's portal
    };

    // The data folder of each system, from the folders the system gives (pure).
    struct SystemDirs
    {
        std::filesystem::path home, appData /* %APPDATA% */, xdgData /* $XDG_DATA_HOME, may be empty */;
    };
    std::filesystem::path DefaultRoot(Os os, const SystemDirs& dirs);
    // The shortcut file name of a game on the Desktop of each system: "Skylanders Giants.app", ".lnk", ".desktop".
    std::string ShortcutName(Os os, const std::string& gameName);

    struct Plan
    {
        std::vector<Item> items;
        std::vector<Left> left;           // refused before anything runs (shown in the list too)
        bool refused = false;             // the environment is unsafe: nothing will be removed
        std::vector<std::string> running; // names of games that play now; the uninstall waits for them
        bool figuresInUse = false;
        uint64_t Bytes(Group group) const;
        // The total, with or without what needs the tick.
        uint64_t Total(bool withUserData) const;
        bool Has(Group group) const;
    };
    // The dry run: lists what exists, with sizes. Touches nothing.
    Plan MakePlan(const Environment& env);

    struct Result
    {
        uint64_t freed = 0;
        int removed = 0; // items fully removed
        std::vector<Left> left;
        bool Complete() const { return left.empty(); }
    };
    // Removes the plan's items; the tick adds the Saves and Figures groups. Every target is checked again here. The
    // launcher itself is not touched (see below). A running game stops everything (Left with Why::InUse).
    Result RunUninstall(const Plan& plan, const Environment& env, bool withUserData);

    // ---- The launcher itself ----------------------------------------------------------------------------------------

    // What the helper does once Skylauncher has quit: macOS moves the .app (and a .old beside it) to the Trash; Windows
    // deletes the .exe (and its .old) once the process is gone; Linux deletes the AppImage. A path that does not fit
    // the system (not a .app on macOS...), or is a link, or is inside a folder it must not be, is refused.
    struct LauncherRemoval
    {
        bool ok = false;
        std::vector<std::filesystem::path> paths; // what goes: the launcher, then its .old
        std::filesystem::path trash;               // macOS: ~/.Trash
        std::string problem;                       // why not (English, for logs; the UI says it in the player's words)
        Why why = Why::Unfit;
    };
    LauncherRemoval PlanLauncherRemoval(const Environment& env);

    // The command that does it after process `pid` ends: argv[0] is the program. Pure. macOS and Linux: /bin/sh with the
    // script and the paths as arguments (nothing to quote); Windows: PowerShell with an encoded script.
    std::vector<std::string> ScheduleCommand(Os os, const LauncherRemoval& removal, uint32_t pid);
    // The script text of each (for tests and docs).
    std::string PosixScript(Os os);
    std::string WindowsScript(const LauncherRemoval& removal, uint32_t pid);
    // UTF-16LE base64, as PowerShell's -EncodedCommand wants it.
    std::string EncodeForPowerShell(const std::string& utf8);
    // Starts the command detached (SDL). False when it could not start. Never called by tests on a real launcher.
    bool StartScheduled(const std::vector<std::string>& command);

    // ---- This machine ---------------------------------------------------------------------------------------------------

    // The running process's id, for the helper to wait for.
    uint32_t ProcessId();

    // The running launcher's Environment. With a scratch variable set (SKYLANDERS_DATA, SKYLAUNCHER_DATA, SSA_LAUNCHER_INI,
    // SSA_COLLECTION: a test, a screenshot) the launcher itself is never the target: only SKYLAUNCHER_UNINSTALL_SELF, a
    // dummy, can be, and on macOS only with SKYLAUNCHER_UNINSTALL_TRASH, a scratch folder, standing for ~/.Trash.
    Environment ThisMachine(const LauncherConfig& config);
    bool ScratchRun();
}
