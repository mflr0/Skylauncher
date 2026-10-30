#pragma once

// Running the game and what it leaves behind for support (docs/support.md): the launcher starts the runtime with its output
// in skylauncher-game.log, watches it while the window stays open (GameSession: one game at a time), and reports an unexpected
// exit. SSARecomp.log, written by the runtime itself, sits in the same folder. "Test the portal" runs the runtime's --portal-probe.
#include "launcher/launcher.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct SDL_IOStream;
struct SDL_Process;

// The launcher's logs folder: skylauncher-game.log, migration.log (<data root>/logs).
std::filesystem::path GameLogFolder();
// Where the runtime writes SSARecomp.log: the game's folder (<data root>/SSA).
std::filesystem::path RuntimeLogFolder();
inline constexpr char RuntimeLogName[] = "SSARecomp.log";
inline constexpr char RuntimePreviousLogName[] = "SSARecomp.prev.log";
inline constexpr char CaptureLogName[] = "skylauncher-game.log";

struct GameRunResult
{
    bool started = false;
    int exitCode = 0;
    std::string error; // why it didn't start
};

// Starts `runtime` with the disc image and waits until it exits. Its stdout and stderr go to skylauncher-game.log (the
// previous one is kept as skylauncher-game.prev.log). `extraArguments` follow the disc image.
GameRunResult RunGame(const std::string& runtime, const std::string& iso, const std::vector<std::string>& extraArguments = {});

// What PLAY starts: the runtime, and the variables it gets on top of the launcher's own environment (each under all its names:
// launcher.h, CreateGameEnvironment).
struct GameLaunch
{
    std::string runtime;
    std::vector<std::pair<std::string, std::string>> variables;
    std::string error; // no runtime: what to tell the player
};
// Puts the game's own video settings and keys into [video] and [input] and saves the settings file, then finds the runtime
// (SKY_LAUNCHER_RUNTIME, else `installed`: the game the launcher built, else the player's own) and the variables: the portal,
// the languages the disc holds, an older build's folders (docs/settings.md).
GameLaunch PrepareGameLaunch(const GameInfo& game, LauncherConfig& config, const std::string& configPath, const std::string& installed);

// One game at a time (docs/launcher_ui.md, "A game running"). PLAY starts the game without waiting for it: its output goes
// straight into skylauncher-game.log, so it runs on if the launcher is closed. The game started here is watched until it
// exits (its exit code tells a crash); one this launcher did not start (an earlier launcher's, one started by hand) is seen
// by the marker each game keeps while it runs (save_backup.h, GameRunning), looked at every MarkerMs.
class GameSession
{
public:
    using Marker = std::function<bool(const std::string& game)>; // true while that game's marker says it runs
    static constexpr uint64_t MarkerMs = 1000;
    static constexpr uint64_t StopTimeoutMs = 5000;
    // Native window operations receive only the PID held by this session. Override for headless checks.
    struct WindowControl
    {
        std::function<bool(uint32_t)> available;
        std::function<bool(uint32_t)> show;
        std::function<bool(uint32_t)> close;
    };
    // `games`: the ids whose markers are looked at; `marker`: the game's own by default (GameIsRunning).
    explicit GameSession(std::vector<std::string> games, Marker marker = {}, WindowControl windows = {});
    ~GameSession(); // the game runs on: only the watch ends
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    // Starts `launch.runtime` with the disc image for `game`. False, with `error`, when a game runs already (this one or
    // another, started here or not) or the runtime can't start.
    bool Start(const std::string& game, const GameLaunch& launch, const std::string& iso, std::string& error);
    struct Ended
    {
        std::string game;
        bool ours = false; // started here: its exit code is known
        int exitCode = 0;
        bool stopped = false; // requested here: do not report a forced stop as a crash
    };
    // Sees the game's exit, and the markers (`nowMs`: SDL_GetTicks). The game that stopped since the last call, if any.
    std::optional<Ended> Poll(uint64_t nowMs);
    const std::string& Running() const { return m_game; } // the game that runs, "" for none
    bool Ours() const { return m_process != nullptr; }
    uint32_t Pid() const;
    bool Stop(uint64_t nowMs);
    bool Stopping() const { return m_stopping; }
    bool CanShow() const;
    bool Show();

private:
    std::string Marked() const; // the first game whose marker is up
    std::vector<std::string> m_games;
    Marker m_marker;
    std::string m_game;
    SDL_Process* m_process = nullptr;
    SDL_IOStream* m_log = nullptr;
    uint64_t m_nextLook = 0;
    void* m_handle = nullptr; // Windows: our own handle on the process, for its exit code
    WindowControl m_windows;
    bool m_stopping = false;
    uint64_t m_forceAt = 0;
};

// The last `maxBytes` of a file, from the start of a line; empty when missing.
std::string ReadLogTail(const std::filesystem::path& file, size_t maxBytes = 256 * 1024);
// What to send for a bug report: the runtime's log, then the capture, each under a heading; the tail of both.
std::string GameLogText(size_t maxBytesEach = 192 * 1024);

// One portal found by `<runtime> --portal-probe`: a JSON object per line, then {"done":true,"count":N}.
struct PortalProbeEntry
{
    std::string kind;    // "hid" or "usb"
    std::string vid, pid;
    std::string product;
    bool opened = false;
    std::string status;  // "ok" or "error: ..."
    // What the portal answered to the game's reset (older runtimes: nothing): its id ("01 28"), the model's name, and what
    // Spyro's Adventure makes of it: "works", "warns", "rejected" or "unknown", with the reason.
    std::string id, model, verdict, verdictReason;
};
struct PortalProbeResult
{
    bool ran = false;    // the runtime started and printed its final line
    std::string error;   // when it didn't
    std::vector<PortalProbeEntry> portals;
    std::vector<std::string> notes; // transports that can't be used at all here ("USB portals can't be used: libusb-1.0 not found")
};
// Parses the probe's output (the part tests and the UI share).
PortalProbeResult ParsePortalProbe(const std::string& output);
// Runs the probe, waiting at most `timeoutMs`.
PortalProbeResult RunPortalProbe(const std::string& runtime, uint32_t timeoutMs = 15000);
