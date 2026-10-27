#include "launcher/report.h"
#include "shared/collection/datadir.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{
    std::string Lower(std::string text)
    {
        for (char& c : text)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    // Alphanumeric or underscore: a name may not start or end inside one of these.
    bool WordChar(char c)
    {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    bool MatchesAt(const std::string& text, size_t at, const std::string& needle)
    {
        if (at + needle.size() > text.size())
            return false;
        for (size_t i = 0; i < needle.size(); i++)
            if (std::tolower(static_cast<unsigned char>(text[at + i])) != std::tolower(static_cast<unsigned char>(needle[i])))
                return false;
        return true;
    }

    std::string ReadFile(const fs::path& file, size_t maxBytes = SIZE_MAX)
    {
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
            return {};
        std::string text;
        if (maxBytes == SIZE_MAX)
        {
            std::ostringstream buffer;
            buffer << stream.rdbuf();
            return buffer.str();
        }
        text.resize(maxBytes);
        stream.read(text.data(), std::streamsize(text.size()));
        text.resize(size_t(stream.gcount()));
        return text;
    }

    // The first line that starts with `prefix`, without the line's ending.
    std::string FindLogLine(const std::string& log, const char* prefix)
    {
        size_t begin = 0;
        while (begin < log.size())
        {
            size_t end = log.find('\n', begin);
            if (end == std::string::npos)
                end = log.size();
            std::string line = log.substr(begin, end - begin);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.starts_with(prefix))
                return line.substr(std::strlen(prefix));
            begin = end + 1;
        }
        return {};
    }

    std::string Utf8(const std::wstring& text)
    {
#if defined(_WIN32)
        if (text.empty())
            return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
        std::string out(size_t(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr);
        return out;
#else
        return std::string(text.begin(), text.end());
#endif
    }

    std::string HostName()
    {
#if defined(_WIN32)
        wchar_t buffer[256] = {};
        DWORD size = DWORD(std::size(buffer));
        if (GetComputerNameW(buffer, &size))
            return Utf8(std::wstring(buffer, size));
        return {};
#else
        char buffer[256] = {};
        if (gethostname(buffer, sizeof(buffer) - 1) == 0)
            return buffer;
        return {};
#endif
    }

    std::string UserName()
    {
#if defined(_WIN32)
        wchar_t buffer[256] = {};
        DWORD size = DWORD(std::size(buffer));
        if (GetUserNameW(buffer, &size))
            return Utf8(std::wstring(buffer, size > 0 ? size - 1 : 0)); // size counts the ending 0
        return {};
#else
        if (const passwd* entry = getpwuid(getuid()); entry != nullptr && entry->pw_name != nullptr)
            return entry->pw_name;
        return {};
#endif
    }

    std::string HomeFolder()
    {
#if defined(_WIN32)
        if (const wchar_t* profile = _wgetenv(L"USERPROFILE"); profile != nullptr && *profile != L'\0')
            return Utf8(profile);
        return {};
#else
        if (const passwd* entry = getpwuid(getuid()); entry != nullptr && entry->pw_dir != nullptr)
            return entry->pw_dir;
        if (const char* home = std::getenv("HOME"); home != nullptr)
            return home;
        return {};
#endif
    }

    // --- Redaction rules ------------------------------------------------------------------------------------------

    struct Rule
    {
        std::string needle;
        std::string replacement;
    };

    void AddRule(std::vector<Rule>& rules, const std::string& needle, const std::string& replacement)
    {
        if (needle.empty())
            return;
        for (const Rule& rule : rules)
            if (rule.needle == needle)
                return;
        rules.push_back({ needle, replacement });
    }

    // The path with its two separator spellings, without a trailing separator.
    void AddPathRules(std::vector<Rule>& rules, std::string path, const std::string& replacement)
    {
        while (path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
            path.pop_back();
        if (path.empty() || path == "/" || path == "\\")
            return;
        std::string forward = path, back = path;
        std::replace(forward.begin(), forward.end(), '\\', '/');
        std::replace(back.begin(), back.end(), '/', '\\');
        AddRule(rules, path, replacement);
        AddRule(rules, forward, replacement);
        AddRule(rules, back, replacement);
    }

    std::string Sanitise(std::string text)
    {
        // The report ends with a newline, and no NUL byte survives a text editor.
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        return text;
    }

    void Section(std::string& report, const std::string& title, const fs::path& file)
    {
        report += "===== " + title + " =====\n";
        std::string text = ReadFile(file);
        report += text.empty() ? "(empty or not found)\n" : text;
        if (!report.empty() && report.back() != '\n')
            report += '\n';
        report += '\n';
    }

    std::string Stamp()
    {
        std::time_t now = std::time(nullptr);
        std::tm parts{};
#if defined(_WIN32)
        localtime_s(&parts, &now);
#else
        localtime_r(&now, &parts);
#endif
        char text[32] = {};
        std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &parts);
        return text;
    }

    std::string FileStamp()
    {
        std::time_t now = std::time(nullptr);
        std::tm parts{};
#if defined(_WIN32)
        localtime_s(&parts, &now);
#else
        localtime_r(&now, &parts);
#endif
        char text[32] = {};
        std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &parts);
        return text;
    }
}

std::string launcher_report::Redact(const std::string& text, const Identity& identity)
{
    std::vector<Rule> rules;
    // The disc image's folder first: it often sits inside the home folder.
    if (!identity.isoFolder.empty())
        AddPathRules(rules, identity.isoFolder, "<iso folder>");

    if (!identity.homeFolder.empty())
        AddPathRules(rules, identity.homeFolder, "<home>");
    if (!identity.userName.empty())
    {
        AddPathRules(rules, "/Users/" + identity.userName, "<home>");
        AddPathRules(rules, "/home/" + identity.userName, "<home>");
        AddPathRules(rules, "C:\\Users\\" + identity.userName, "<home>");
        AddPathRules(rules, "C:/Users/" + identity.userName, "<home>");
    }
    if (!identity.computerName.empty())
    {
        AddRule(rules, identity.computerName, "<computer>");
        if (size_t dot = identity.computerName.find('.'); dot != std::string::npos && dot > 0)
            AddRule(rules, identity.computerName.substr(0, dot), "<computer>");
    }
    if (!identity.userName.empty())
        AddRule(rules, identity.userName, "<user>");
    // The longest match first, so a full path is replaced before the name it ends with.
    std::stable_sort(rules.begin(), rules.end(),
        [](const Rule& a, const Rule& b) { return a.needle.size() > b.needle.size(); });

    std::string out;
    out.reserve(text.size());
    for (size_t at = 0; at < text.size();)
    {
        const Rule* match = nullptr;
        for (const Rule& rule : rules)
        {
            if (!MatchesAt(text, at, rule.needle))
                continue;
            bool starts = at == 0 || !WordChar(text[at - 1]);
            bool ends = at + rule.needle.size() == text.size() || !WordChar(text[at + rule.needle.size()]);
            if (!starts || !ends)
                continue;
            match = &rule;
            break;
        }
        if (match != nullptr)
        {
            out += match->replacement;
            at += match->needle.size();
        }
        else
            out += text[at++];
    }
    return out;
}

launcher_report::Identity launcher_report::CurrentIdentity(const std::string& iso)
{
    Identity identity;
    identity.userName = UserName();
    identity.homeFolder = HomeFolder();
    identity.computerName = HostName();
    if (!iso.empty())
    {
        std::error_code error;
        fs::path folder = fs::path(std::u8string(iso.begin(), iso.end())).parent_path();
        if (!folder.empty())
            identity.isoFolder = datadir::Text(folder);
    }
    return identity;
}

launcher_report::Versions launcher_report::ReadVersions(const std::string& launcherVersion, const std::string& kitVersion,
    const fs::path& runtimeFolder)
{
    Versions versions;
    versions.launcher = launcherVersion;
    versions.kit = kitVersion;
    versions.os = SDL_GetPlatform();
    std::string log = ReadFile(runtimeFolder / "SSARecomp.log", 256 * 1024);
    versions.gameBuild = FindLogLine(log, "[version] ");
    if (versions.gameBuild.empty())
        versions.gameBuild = "(no game log yet)";
    versions.gpu = FindLogLine(log, "[gpu] adapter ");
    if (versions.gpu.empty())
        versions.gpu = FindLogLine(log, "[gpu] ");
    if (versions.gpu.empty())
        versions.gpu = "(no GPU line in the log)";
    return versions;
}

std::string launcher_report::BuildReport(const Sources& sources)
{
    std::string report;
    report += "Arkeyan Launcher problem report\n";
    report += "Generated: " + Stamp() + "\n";
    report += "Launcher: " + (sources.versions.launcher.empty() ? "(unknown)" : sources.versions.launcher) + "\n";
    report += "Kit: " + (sources.versions.kit.empty() ? "(no kit)" : sources.versions.kit) + "\n";
    report += "Game build: " + (sources.versions.gameBuild.empty() ? "(unknown)" : sources.versions.gameBuild) + "\n";
    report += "OS: " + (sources.versions.os.empty() ? "(unknown)" : sources.versions.os) + "\n";
    report += "GPU: " + (sources.versions.gpu.empty() ? "(unknown)" : sources.versions.gpu) + "\n";
    if (!sources.iso.empty())
        report += "Disc image: " + sources.iso + "\n";
    report += "\n";

    Section(report, "launcher.ini", sources.settingsFile);
    Section(report, "migration.log", sources.logFolder / "migration.log");
    Section(report, "SSARecomp.log", sources.runtimeFolder / "SSARecomp.log");
    Section(report, "SSARecomp.prev.log", sources.runtimeFolder / "SSARecomp.prev.log");
    Section(report, "skylauncher-game.log", sources.logFolder / "skylauncher-game.log");
    Section(report, "install.log", sources.runtimeFolder / "install.log");
    Section(report, "install.prev.log", sources.runtimeFolder / "install.prev.log");

    // The step logs SSABuild kept after a successful install.
    std::vector<fs::path> kept;
    std::error_code error;
    for (fs::directory_iterator it(sources.runtimeFolder / "install-logs", error), end; !error && it != end; it.increment(error))
        if (it->is_regular_file(error))
            kept.push_back(it->path());
    std::sort(kept.begin(), kept.end());
    for (const fs::path& file : kept)
        Section(report, "install-logs/" + datadir::Text(file.filename()), file);

    Identity identity;
    identity.userName = sources.userName;
    identity.computerName = sources.computerName;
    identity.homeFolder = sources.homeFolder;
    if (!sources.iso.empty())
    {
        fs::path folder = fs::path(std::u8string(sources.iso.begin(), sources.iso.end())).parent_path();
        if (!folder.empty())
            identity.isoFolder = datadir::Text(folder);
    }
    return Redact(Sanitise(report), identity) + "\n";
}

std::string launcher_report::FileName()
{
    return "ArkeyanLauncher-report-" + FileStamp() + ".txt";
}
