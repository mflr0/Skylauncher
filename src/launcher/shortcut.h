#pragma once

// The desktop shortcut of a game (Game settings > Create a desktop shortcut): a launcher for Skylauncher itself, started
// as "Skylauncher --game <id> --play" with the game's icon, the title icon decoded from the player's disc.
//   macOS    a small "<Game>.app" on the Desktop: a shell script that starts Skylauncher, and the icon as .icns
//   Windows  a "<Game>.lnk" with the icon as a .ico kept in the game's folder
//   Linux    a "<game>.desktop" file with the icon as a PNG kept in the game's folder
// The file builders are pure and are checked on every system by SkylauncherTest. Nothing here writes outside `folder`
// (and the icon file's folder): SKYLAUNCHER_DESKTOP replaces the Desktop, so tests never touch the real one.
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct ShortcutSpec
{
    std::string gameId;   // "giants": what --game takes
    std::string gameName; // "Skylanders: Giants"
    std::filesystem::path launcher;    // the Skylauncher executable the shortcut starts
    std::vector<uint8_t> iconPng;      // the game's title icon; empty: the shortcut has no icon of its own
    std::filesystem::path folder;      // where the shortcut goes (the Desktop)
    std::filesystem::path iconFolder;  // where Windows and Linux keep the icon file the shortcut points to (the game's folder)
};

struct ShortcutResult
{
    bool ok = false;
    std::filesystem::path file; // the .app, .lnk or .desktop that was written
    std::string error;
};

// The Desktop: SKYLAUNCHER_DESKTOP, else the system's Desktop folder (OneDrive's included on Windows).
std::filesystem::path DesktopFolder();
// The running Skylauncher: SKYLAUNCHER_EXE (tests), else the AppImage file on Linux, else the executable itself.
std::filesystem::path SelfExecutable();

// Writes the shortcut for this system (macOS, Windows or Linux) and replaces an older one of the same game.
ShortcutResult CreateDesktopShortcut(const ShortcutSpec& spec);

// ---- The files, one builder each (pure) --------------------------------------------------------------------------------

// "Skylanders: Giants" -> "Skylanders Giants": a name every file system takes.
std::string ShortcutFileName(std::string_view gameName);
// "--game giants --play"
std::string ShortcutArguments(std::string_view gameId);
// 'text' for a POSIX shell, a quote inside it closed and reopened.
std::string ShellQuote(std::string_view text);
// "text" for the Exec key of a .desktop file (the specification's quoting).
std::string DesktopExecQuote(std::string_view text);

std::string MacLaunchScript(const std::filesystem::path& launcher, std::string_view gameId);
std::string MacInfoPlist(std::string_view gameName, std::string_view gameId, bool icon);
// An .icns holding the PNG as it is (icp4..ic10 by its width); empty when the PNG's width has no icns type.
std::vector<uint8_t> MacIcns(const std::vector<uint8_t>& png);
std::string LinuxDesktopEntry(std::string_view gameName, const std::filesystem::path& launcher, std::string_view gameId,
    const std::filesystem::path& icon);
// A .ico holding the PNG as it is (Vista and later); empty when the bytes are not a PNG.
std::vector<uint8_t> WindowsIco(const std::vector<uint8_t>& png);

// The two writers that need nothing but files, so they run (and are tested) on every system.
ShortcutResult WriteMacApp(const ShortcutSpec& spec);
ShortcutResult WriteLinuxDesktop(const ShortcutSpec& spec);
