#pragma once

// The player's figures on disk: one folder of figure files (the runtime's "figures" folder in the settings folder),
// read and changed by the launcher's collection and by the game.
//
//   <folder>/<name>.sky             a figure (.sky, .bin and .dmp are the same 1 KB dump)
//   <folder>/<name>.sky.lock        while a game has the figure on its portal: the game's process id, as text. The
//                                   launcher leaves a locked figure alone; a lock whose process is gone is ignored.
//   <folder>/backups/<name>/<date>.sky   copies made before every change, newest last
#include "shared/figures/figure_catalog.h"
#include "shared/figures/figure_file.h"
#include <filesystem>
#include <string>
#include <vector>

namespace figures
{
    bool IsFigureFileName(const std::filesystem::path& name);

    bool LoadFigure(const std::filesystem::path& path, FigureData& data);
    // Writes next to the file then renames it over, so a figure is never left half written.
    bool SaveFigure(const std::filesystem::path& path, const FigureData& data);

    // `file_name` in `folder`, or "<stem> (2)<ext>", "(3)"... when that name is taken.
    std::filesystem::path UniquePath(const std::filesystem::path& folder, const std::string& fileName);

    struct ImportResult
    {
        std::string file;  // the name it got in the folder, empty on failure
        std::string error; // why it was refused
    };
    // Copies a figure dump into the folder after checking it is one (1 KB, a toy this launcher knows or at least a
    // valid header).
    ImportResult ImportFigure(const std::filesystem::path& folder, const std::filesystem::path& source);

    // A new, never played figure of `toy` (StarterFileName, made unique). Returns its file name, empty on failure.
    std::string NewFigure(const std::filesystem::path& folder, const ToyInfo& toy);

    // Backups of one figure, oldest first; each is the name of a file in BackupFolder.
    std::filesystem::path BackupFolder(const std::filesystem::path& folder, const std::string& file);
    std::string BackupFigure(const std::filesystem::path& folder, const std::string& file);
    std::vector<std::string> ListBackups(const std::filesystem::path& folder, const std::string& file);
    // Puts a backup back in place (the current state is backed up first).
    bool RestoreBackup(const std::filesystem::path& folder, const std::string& file, const std::string& backup);
    // Removes the figure from the folder; a backup of it is kept.
    bool DeleteFigure(const std::filesystem::path& folder, const std::string& file);

    bool IsLocked(const std::filesystem::path& folder, const std::string& file);
}
