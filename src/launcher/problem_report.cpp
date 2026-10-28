#include "launcher/problem_report.h"
#include "launcher/game_install.h"
#include "launcher/game_run.h"
#include "launcher/report.h"
#include "launcher/shortcut.h"
#include "launcher/updater.h"
#include "shared/collection/datadir.h"
#include <SDL3/SDL.h>
#include <cctype>

namespace
{
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
