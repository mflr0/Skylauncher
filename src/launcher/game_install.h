#pragma once

// Installing the game: the player kit's SSABuild recompiles and builds the game from the player's own disc, on their
// machine (docs/install.md). The launcher finds the kit, starts SSABuild, follows its "@" events on stdout and remembers
// the built game per disc, by the SHA-256 of its default.xex.
#include "launcher/launcher.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

struct SDL_Process;

struct InstallKit
{
    std::filesystem::path folder;
    std::filesystem::path ssabuild; // the kit's SSABuild executable
    std::string release;            // "version=" in kit.txt: the release it came from, "" when it has none
    std::string version;            // "build=" (else "version=") in the kit's kit.txt, else a fingerprint of SSABuild
    std::vector<std::string> games; // "games=" in kit.txt: what the kit builds ("ssa", "giants", "swapforce", "trapteam"); a kit before 0.5.0 has none: SSA only
    std::map<std::string, std::string> gameVersions; // "game_version.<id>=" in kit.txt: the version of each game the kit builds ("0.8.2"); a kit before 0.5.2 has none
    std::string tag;                // the release it was downloaded from (its kit/<tag>/ folder), "" for any other kit
    bool chosen = false;            // SKY_LAUNCHER_KIT or "[launcher] kit=": the player's own, never replaced by the launcher

    bool Builds(std::string_view gameId) const;
    std::string GameVersion(std::string_view gameId) const; // "" when the kit does not say
};

// The kit comes from an older release than the launcher's version: its kit/<tag>/ folder, else "version=" in its kit.txt.
// False for a kit that says nothing about it, and for the player's own (`chosen`).
bool KitOlderThanLauncher(const InstallKit& kit, std::string_view launcherVersion);

// Where Arkeyan Launcher keeps what it makes: the "Skylanders" data folder shared with the games (datadir.h: SKYLANDERS_DATA, else
// %APPDATA%\Skylanders on Windows, ~/Library/Application Support/Skylanders on macOS, ~/.local/share/Skylanders on Linux), or
// SKY_LAUNCHER_DATA (tests). The built games are in <game>/ (SSA/), the downloaded kits in kit/, the compiler in toolchains/.
std::filesystem::path LauncherDataFolder();
std::filesystem::path DownloadedKitsFolder(); // kit/<release tag>/ folders, each holding SSABuild

// The kit in `folder`: empty when it holds no SkyBuild (or SSABuild, in a kit for older launchers).
std::optional<InstallKit> InstallKitIn(const std::filesystem::path& folder);

// The kit: SKY_LAUNCHER_KIT, else "[launcher] kit=", else a folder next to the launcher (or its macOS app bundle) named
// skylanders-kit, ssa-kit, kit, or starting with skylanders-kit or ssa-kit (an unzipped "skylanders-kit-windows-x64") unless a downloaded kit comes from a newer
// release (or when it cannot build `gameId`, if one is given and the downloaded kit can), else the newest downloaded kit.
// Empty when none holds SkyBuild or SSABuild.
std::optional<InstallKit> FindInstallKit(const LauncherConfig& config, std::string_view gameId = {});

// A runtime the player set up themselves: SKY_LAUNCHER_RUNTIME, else "[launcher] runtime=" in the settings file, else
// the game's runtime executable next to the launcher (or inside its macOS app bundle). Empty when none exists.
// `overrideOnly` keeps only SKY_LAUNCHER_RUNTIME, which wins over an installed game.
std::string FindRuntime(const GameInfo& game, const LauncherConfig& config, bool overrideOnly = false);

// The game the launcher is showing ("ssa", "giants", "swapforce": GameInfo::id). Set by main() before a game's window opens; the
// functions below that take no game (GameInstallFolder, the log folders) answer for it. "ssa" until set.
void SetActiveGame(std::string_view id);
std::string_view ActiveGame();

// Where the active game is built: <data>/SSA or <data>/Giants (datadir::GameFolderName), which also holds the game's
// saves, settings and log. Never next to the ISO. The argument is unused (one folder per game, whatever its release).
std::filesystem::path GameInstallFolder(const std::string& xexSha256);

// The game built for a disc, from "[install.<sha256>] runtime= kit= version=". Empty runtime when none was built or it is gone.
struct InstalledGame
{
    std::string runtime;
    std::string kitVersion;
    std::string gameVersion; // "version=": the game's version when it was built ("0.8.2"); empty for a build from before versions
};
InstalledGame FindInstalledGame(const LauncherConfig& config, const std::string& xexSha256);
void RecordInstalledGame(LauncherConfig& config, const std::string& xexSha256, const std::string& runtime, const std::string& kitVersion,
    const std::string& gameVersion = {});

// After a successful install (docs/install.md, "Housekeeping"): the build's work folder (2-3 GB) goes but its logs stay in
// <game>/install-logs, the kits older than `kitFolder` (a downloaded kit, kit/<release tag>/) go, and so do compiler
// archives that are already extracted. Never touches a kit that is not under DownloadedKitsFolder().
void CleanAfterInstall(const std::filesystem::path& gameFolder, const std::filesystem::path& kitFolder);

// A game built by a kit up to 0.3.0 still uses the older folders (SSARecomp/...). Installs from 0.3.1 on leave
// "layout.txt" in the game folder; a runtime under `gameFolder` without it is such an older build, and the launcher
// then points it at the new places through the variables it already reads (docs/install.md, "Older builds").
void MarkGameLayout(const std::filesystem::path& gameFolder);
bool IsOlderLayoutBuild(const std::filesystem::path& runtime, const std::filesystem::path& gameFolder);

// Rough figures shown before installing.
// Kit 0.5.0 on an M1 Pro: SSA 193 s, Giants 563 s (4443 shaders instead of 1118); slower PCs take longer.
inline constexpr int InstallMinutes = 10;
inline constexpr int GiantsInstallMinutes = 15;
inline int InstallMinutesFor(std::string_view gameId) { return gameId == "giants" ? GiantsInstallMinutes : InstallMinutes; }
inline constexpr int InstallGigabytes = 6;

// What SSABuild has said so far.
struct InstallProgress
{
    enum class State { Idle, Running, Failed, Cancelled, Done, Interrupted }; // Interrupted: read back from install.log
    struct Step
    {
        std::string id, title;
        uint64_t startedAt = 0, endedAt = 0; // SDL ticks (ms); endedAt is 0 while it runs
    };

    State state = State::Idle;
    std::vector<Step> steps;
    uint64_t done = 0, total = 0;  // the current step's @progress
    std::string error;             // @error, or why SSABuild stopped
    bool startFailed = false;      // the installer never started (the kit, the folder): `error` is in the launcher's language
    std::string runtime;           // @done
    std::string action;            // the current step's last @log line
    std::vector<std::string> log;  // every line, @log text and anything else SSABuild printed
    size_t logLines = 0;           // lines in the full log (the copy may hold only the last ones)

    // Applies one stdout line at time `now`.
    void Apply(std::string_view line, uint64_t now);
};

// An install read back from its install.log (the launcher was closed while it ran, or it failed): Interrupted, or
// Failed when SSABuild reported an error. Its steps have no times. Idle when there is no log.
InstallProgress ReadInstallLog(const std::filesystem::path& gameFolder);

// Runs SSABuild for one disc. Its stdout (and stderr) is read on a thread; the window polls Snapshot().
class GameInstaller
{
public:
    ~GameInstaller();
    // Starts SSABuild; false (and Failed with the reason) when there is no kit or it can't start.
    bool Start(const std::optional<InstallKit>& kit, const std::string& iso, const std::filesystem::path& out);
    // Stops SSABuild, the process this installer started, and on Windows the processes it started in turn. Returns at
    // once; the state becomes Cancelled when SSABuild has exited.
    void Cancel();
    // A copy of the progress with at most `logTail` log lines (the last ones).
    InstallProgress Snapshot(size_t logTail = 0);
    std::string FullLog();
    bool Running() const { return m_running; }
    uint32_t ProcessId() const { return m_pid; } // SSABuild's, while it runs

private:
    void Read();
    void Join();

    std::mutex m_mutex;
    InstallProgress m_progress;
    std::thread m_thread;
    std::atomic<bool> m_running{ false }, m_cancelled{ false };
    std::atomic<uint64_t> m_cancelledAt{ 0 };
    SDL_Process* m_process = nullptr;
    std::atomic<uint32_t> m_pid{ 0 };
    void* m_job = nullptr; // Windows job object holding SSABuild and its compilers
    std::filesystem::path m_logFile;
    std::filesystem::path m_out, m_kitFolder; // the game folder being built and the kit building it
};
