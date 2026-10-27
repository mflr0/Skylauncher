#pragma once

// "Report a problem" (docs/support.md): one text file with the logs, the settings and what the game runs on, cleaned
// of the player's own details before it is shown, saved or copied. Nothing is uploaded.
#include <filesystem>
#include <string>

namespace launcher_report
{
    // What identifies the player in a report, and must not leave their computer.
    struct Identity
    {
        std::string userName;     // the account name ("alice"); empty when unknown
        std::string computerName; // the machine's name ("MacBook-Pro.local"); empty when unknown
        std::string homeFolder;   // the account's home path (/Users/alice, C:\Users\alice, /home/alice)
        std::string isoFolder;    // the folder holding the chosen disc image
    };

    // Replaces the home paths (/Users/<name>, C:\Users\<name>, C:/Users/<name>, /home/<name>), the disc image's folder,
    // the account name and the computer name with <home>, <iso folder>, <user> and <computer>. Everything else is kept
    // byte for byte. Case-insensitive (Windows paths), whole names only for the account and the computer.
    std::string Redact(const std::string& text, const Identity& identity);

    // This machine's identity, with the disc image the launcher was given (empty: none).
    Identity CurrentIdentity(const std::string& iso);

    struct Versions
    {
        std::string launcher;
        std::string kit;       // the kit that built the game, else the one that would
        std::string gameBuild; // the [version] line of the last game log, without its tag
        std::string os;
        std::string gpu;       // the [gpu] adapter line of the last game log
    };

    // The version numbers, the OS and the GPU as the game itself logged them.
    Versions ReadVersions(const std::string& launcherVersion, const std::string& kitVersion, const std::filesystem::path& runtimeFolder);

    // The files the report is made of. Missing files are named in the report, not skipped silently.
    struct Sources
    {
        std::filesystem::path runtimeFolder; // <data>/SSA: SSARecomp.log, install.log, install-logs/
        std::filesystem::path logFolder;     // <data>/logs: skylauncher-game.log, migration.log
        std::filesystem::path settingsFile;  // <data>/launcher.ini
        Versions versions;
        std::string iso;
        std::string userName, computerName, homeFolder; // for the redaction
    };

    // The whole report, already redacted.
    std::string BuildReport(const Sources& sources);

    // "ArkeyanLauncher-report-<yyyymmdd-hhmmss>.txt".
    std::string FileName();
}
