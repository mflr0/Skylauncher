#pragma once

// Running the game and what it leaves behind for support (docs/support.md): the launcher starts the runtime with its output
// piped into skylauncher-game.log, waits for it, and reports an unexpected exit. SSARecomp.log, written by the runtime
// itself, sits in the same folder. "Test the portal" runs the runtime's --portal-probe.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The folder of launcher.ini, SSARecomp.log and skylauncher-game.log (the SDL preferences folder of "SSARecomp").
std::filesystem::path GameLogFolder();
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
