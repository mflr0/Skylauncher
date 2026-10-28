#pragma once

// "Report a problem" as a file (docs/support.md): the launcher's report (report.h) written to the Desktop, or to the logs folder
// when the Desktop cannot be written, and shown in the file manager. The Support page and the crash dialog both make it, and the
// game's own F3 menu writes the same sections.
#include "launcher/launcher.h"
#include <filesystem>
#include <string>

namespace launcher_report
{
    struct Sources;
}

// The sources of a report for the game in `config`: the logs folders, `configPath`, the kit, the disc image, the player's names.
launcher_report::Sources CurrentReportSources(const std::string& configPath, const LauncherConfig& config, const std::string& gameId,
    const std::string& iso);

struct SavedReport
{
    std::filesystem::path file; // empty: not saved
    std::string error;
};

// Builds the report and saves it as ArkeyanLauncher-report-<time>.txt; the folder is shown in the file manager when `show`.
SavedReport SaveProblemReport(const std::string& configPath, const LauncherConfig& config, const std::string& gameId,
    const std::string& iso, bool show = true);

// Saves a report already built (the Support page shows it before it is saved).
SavedReport SaveProblemReportText(const std::string& text, bool show = true);
