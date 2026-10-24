#include "shared/figures/figure_collection.h"
#include "shared/platform/platform.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>

namespace figures
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string Lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(tolower(c)); });
            return text;
        }

        std::string Timestamp()
        {
            std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm local{};
#if defined(_WIN32)
            localtime_s(&local, &now);
#else
            localtime_r(&now, &local);
#endif
            char text[32];
            std::strftime(text, sizeof(text), "%Y-%m-%d %H-%M-%S", &local); // no ':' (Windows file names)
            return text;
        }
    }

    bool IsFigureFileName(const fs::path& name)
    {
        std::string extension = Lower(name.extension().string());
        std::string stem = name.filename().string();
        return !stem.empty() && stem[0] != '.' && (extension == ".sky" || extension == ".bin" || extension == ".dmp");
    }

    bool LoadFigure(const fs::path& path, FigureData& data)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.good() || file.tellg() != std::streamoff(FigureSize))
            return false;
        file.seekg(0);
        return bool(file.read(reinterpret_cast<char*>(data.data()), FigureSize));
    }

    bool SaveFigure(const fs::path& path, const FigureData& data)
    {
        fs::path temporary = path;
        temporary += ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file.write(reinterpret_cast<const char*>(data.data()), FigureSize))
                return false;
        }
        std::error_code error;
        fs::rename(temporary, path, error);
        if (error)
            fs::remove(temporary, error);
        return !error;
    }

    fs::path UniquePath(const fs::path& folder, const std::string& fileName)
    {
        fs::path candidate = folder / fileName;
        std::string stem = fs::path(fileName).stem().string(), extension = fs::path(fileName).extension().string();
        for (int n = 2; fs::exists(candidate); n++)
            candidate = folder / (stem + " (" + std::to_string(n) + ")" + extension);
        return candidate;
    }

    ImportResult ImportFigure(const fs::path& folder, const fs::path& source)
    {
        FigureData data;
        if (!LoadFigure(source, data))
            return { {}, source.filename().string() + " isn't a figure dump (it must be exactly 1 KB)." };
        FigureHeader header = ReadHeader(data);
        const ToyInfo* toy = FindToy(header.toyType);
        if (toy == nullptr && !header.checksumValid)
            return { {}, source.filename().string() + " isn't a figure dump: its header doesn't check out." };
        std::error_code error;
        fs::create_directories(folder, error);
        std::string name = IsFigureFileName(source.filename()) ? source.filename().string() : source.stem().string() + ".sky";
        fs::path target = UniquePath(folder, name);
        if (!SaveFigure(target, data))
            return { {}, "Couldn't write " + target.string() + "." };
        return { target.filename().string(), {} };
    }

    std::string NewFigure(const fs::path& folder, const ToyInfo& toy)
    {
        std::error_code error;
        fs::create_directories(folder, error);
        fs::path target = UniquePath(folder, StarterFileName(toy));
        return SaveFigure(target, CreateFigure(toy.toyType, toy.variantId)) ? target.filename().string() : std::string();
    }

    fs::path BackupFolder(const fs::path& folder, const std::string& file)
    {
        return folder / "backups" / fs::path(file).stem();
    }

    std::string BackupFigure(const fs::path& folder, const std::string& file)
    {
        fs::path backups = BackupFolder(folder, file);
        std::error_code error;
        fs::create_directories(backups, error);
        fs::path target = UniquePath(backups, Timestamp() + fs::path(file).extension().string());
        fs::copy_file(folder / file, target, error);
        return error ? std::string() : target.filename().string();
    }

    std::vector<std::string> ListBackups(const fs::path& folder, const std::string& file)
    {
        std::vector<std::string> names;
        std::error_code error;
        for (const fs::directory_entry& entry : fs::directory_iterator(BackupFolder(folder, file), error))
            if (entry.is_regular_file() && IsFigureFileName(entry.path().filename()))
                names.push_back(entry.path().filename().string());
        std::sort(names.begin(), names.end()); // the names are dates
        return names;
    }

    bool RestoreBackup(const fs::path& folder, const std::string& file, const std::string& backup)
    {
        FigureData data;
        if (!LoadFigure(BackupFolder(folder, file) / backup, data))
            return false;
        BackupFigure(folder, file);
        return SaveFigure(folder / file, data);
    }

    bool DeleteFigure(const fs::path& folder, const std::string& file)
    {
        if (BackupFigure(folder, file).empty())
            return false;
        std::error_code error;
        return fs::remove(folder / file, error) && !error;
    }

    bool IsLocked(const fs::path& folder, const std::string& file)
    {
        std::ifstream lock(folder / (file + ".lock"));
        if (!lock.good())
            return false;
        uint32_t pid = 0;
        lock >> pid;
        return PlatformProcessAlive(pid);
    }
}
