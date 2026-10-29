#include "launcher/problem_report.h"
#include "launcher/game_install.h"
#include "launcher/game_run.h"
#include "launcher/i18n.h"
#include "launcher/report.h"
#include "launcher/shortcut.h"
#include "launcher/updater.h"
#include "shared/collection/datadir.h"
#include <SDL3/SDL.h>
#include <cctype>
#include <fmt/format.h>

namespace
{
    std::string UrlEncode(const std::string& text)
    {
        std::string out;
        for (unsigned char c : text)
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                out += char(c);
            else
                out += fmt::format("%{:02X}", c);
        return out;
    }

    // The folder in the file manager: a file URL on macOS and Linux, the path itself on Windows (ShellExecute).
    void ShowFolder(const std::filesystem::path& folder)
    {
        std::string path = datadir::Text(folder);
#if defined(_WIN32)
        SDL_OpenURL(path.c_str());
#else
        std::string url = "file://";
        for (unsigned char c : path)
        {
            if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
                url += char(c);
            else
            {
                static const char Hex[] = "0123456789ABCDEF";
                url += '%';
                url += Hex[c >> 4];
                url += Hex[c & 15];
            }
        }
        SDL_OpenURL(url.c_str());
#endif
    }
}

launcher_report::Sources CurrentReportSources(const std::string& configPath, const LauncherConfig& config, const std::string& gameId,
    const std::string& iso)
{
    launcher_report::Sources sources;
    sources.runtimeFolder = RuntimeLogFolder();
    sources.logFolder = GameLogFolder();
    sources.settingsFile = datadir::FromText(configPath);
    sources.iso = iso;
    std::string kit;
    if (std::optional<InstallKit> found = FindInstallKit(config, gameId))
        kit = found->version;
    sources.versions = launcher_report::ReadVersions(LauncherVersion(), kit, sources.runtimeFolder);
    launcher_report::Identity identity = launcher_report::CurrentIdentity(sources.iso);
    sources.userName = identity.userName;
    sources.computerName = identity.computerName;
    sources.homeFolder = identity.homeFolder;
    return sources;
}

SavedReport SaveProblemReportText(const std::string& text, bool show)
{
    SavedReport saved;
    saved.file = launcher_report::SaveToFirstFolder(text, launcher_report::FileName(), { DesktopFolder(), GameLogFolder() });
    if (saved.file.empty())
        saved.error = "no folder could be written";
    else if (show)
        ShowFolder(saved.file.parent_path());
    return saved;
}

SavedReport SaveProblemReport(const std::string& configPath, const LauncherConfig& config, const std::string& gameId,
    const std::string& iso, bool show)
{
    return SaveProblemReportText(launcher_report::BuildReport(CurrentReportSources(configPath, config, gameId, iso)), show);
}

void ShowGameStopped(const GameInfo& game, int exitCode, const std::string& configPath, const LauncherConfig& config,
    const std::string& iso, SDL_Window* parent)
{
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    std::string message = Tf("{} stopped unexpectedly (exit code {}).\n\nPress Report to save a report file with the log and where the game stopped.", game.name, exitCode);
    const SDL_MessageBoxButtonData buttons[] = { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 2, Tr("Report") }, { 0, 1, Tr("Copy log") },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, Tr("Close") } };
    SDL_MessageBoxData box{ SDL_MESSAGEBOX_ERROR, parent, Tr("The game stopped unexpectedly"), message.c_str(), 3, buttons, nullptr };
    for (;;)
    {
        int pressed = 0;
        if (!SDL_ShowMessageBox(&box, &pressed) || pressed == 0)
            return;
        if (pressed == 1)
        {
            SDL_SetClipboardText(GameLogText().c_str());
            continue;
        }
        SavedReport saved = SaveProblemReport(configPath, config, game.id, iso);
        std::string title = fmt::format("The game stopped unexpectedly (exit code {})", exitCode);
        std::string body = "What I was doing:\n\n\n" + (saved.file.empty() ? std::string("The report file could not be saved; the log is below.\n")
            : "The report file is " + saved.file.filename().string() + " (on my Desktop or in the logs folder); I attach it here:\n\n");
        SDL_OpenURL(("https://github.com/mflr0/Skylauncher/issues/new?title=" + UrlEncode(title) + "&body=" + UrlEncode(body)).c_str());
        return;
    }
}
