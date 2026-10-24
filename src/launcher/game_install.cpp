#include "launcher/game_install.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <fmt/format.h>
#include <fstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
    // Paths are UTF-8 strings everywhere else (SDL, the settings file); std::filesystem would read them in the
    // Windows code page.
    std::filesystem::path Path(const std::string& utf8)
    {
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    }

    std::string Utf8(const std::filesystem::path& path)
    {
        std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    std::string_view Trim(std::string_view text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
            text.remove_prefix(1);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
            text.remove_suffix(1);
        return text;
    }

    std::string Setting(const LauncherConfig& config, const std::string& section, const char* key)
    {
        auto s = config.find(section);
        if (s == config.end())
            return {};
        auto k = s->second.find(key);
        return k == s->second.end() ? std::string() : k->second;
    }

#if defined(_WIN32)
    constexpr char SsaBuildName[] = "SSABuild.exe";
#else
    constexpr char SsaBuildName[] = "SSABuild";
#endif

    std::optional<InstallKit> KitIn(const std::filesystem::path& folder)
    {
        std::error_code error;
        std::filesystem::path ssabuild = folder / SsaBuildName;
        if (!std::filesystem::is_regular_file(ssabuild, error))
            return {};
        InstallKit kit{ folder, ssabuild, {} };
        // kit.txt: "key=value" lines; "build" changes with every kit made, "version" with releases.
        std::ifstream file(folder / "kit.txt");
        std::string version;
        for (std::string line; std::getline(file, line);)
        {
            std::string_view text = Trim(line);
            if (text.starts_with("build="))
                kit.version = Trim(text.substr(6));
            else if (text.starts_with("version="))
                version = Trim(text.substr(8));
        }
        if (kit.version.empty())
            kit.version = version;
        if (kit.version.empty())
        {
            // No version file: SSABuild's size and date, which change with every kit.
            auto size = std::filesystem::file_size(ssabuild, error);
            auto time = std::filesystem::last_write_time(ssabuild, error).time_since_epoch().count();
            kit.version = fmt::format("{}-{}", uint64_t(size), int64_t(time));
        }
        return kit;
    }

    std::string InstallSection(const std::string& xexSha256)
    {
        return "install." + xexSha256;
    }
}

std::optional<InstallKit> FindInstallKit(const LauncherConfig& config)
{
    if (const char* path = SDL_getenv("SKYLAUNCHER_KIT"); path != nullptr && *path != 0)
        return KitIn(Path(path));
    if (std::string path = Setting(config, "launcher", "kit"); !path.empty())
        return KitIn(Path(path));

    const char* base = SDL_GetBasePath();
    if (base == nullptr)
        return {};
    std::vector<std::filesystem::path> folders{ Path(base) };
    // A macOS app's base path is Skylauncher.app/Contents/Resources: the kit sits next to the app.
    std::filesystem::path bundle = folders[0].parent_path();
    if (bundle.filename().empty())
        bundle = bundle.parent_path();
    if (bundle.filename() == "Resources" && bundle.parent_path().filename() == "Contents")
        folders.push_back(bundle.parent_path().parent_path().parent_path());

    for (const std::filesystem::path& folder : folders)
    {
        for (const char* name : { "ssa-kit", "kit" })
            if (auto kit = KitIn(folder / name))
                return kit;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(folder, error))
            if (entry.is_directory(error) && Utf8(entry.path().filename()).starts_with("ssa-kit"))
                if (auto kit = KitIn(entry.path()))
                    return kit;
    }
    return {};
}

std::string FindRuntime(const GameInfo& game, const LauncherConfig& config, bool overrideOnly)
{
    std::vector<std::filesystem::path> candidates;
    if (const char* path = SDL_getenv("SKYLAUNCHER_RUNTIME"))
        candidates.push_back(Path(path));
    if (!overrideOnly)
    {
        if (std::string runtime = Setting(config, "launcher", "runtime"); !runtime.empty())
            candidates.push_back(Path(runtime));
        if (const char* base = SDL_GetBasePath())
        {
            std::filesystem::path directory = Path(base);
            std::string name = game.runtime;
#if defined(_WIN32)
            candidates.push_back(directory / (name + ".exe"));
#else
            candidates.push_back(directory / name);
            candidates.push_back(directory / (name + ".app") / "Contents" / "MacOS" / name);
#endif
        }
    }

    for (std::filesystem::path path : candidates)
    {
        // An app bundle names its executable after itself.
        if (path.extension() == ".app")
            path = path / "Contents" / "MacOS" / path.stem();
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error))
            return Utf8(path);
    }
    return {};
}

std::filesystem::path GameInstallFolder(const std::string& xexSha256)
{
    std::filesystem::path data;
#if defined(_WIN32)
    if (const char* local = SDL_getenv("LOCALAPPDATA"); local != nullptr && *local != 0)
        data = Path(local) / "Skylauncher";
#endif
    if (data.empty())
        if (char* pref = SDL_GetPrefPath("", "Skylauncher"))
        {
            data = Path(pref);
            SDL_free(pref);
        }
    return data / "games" / xexSha256;
}

InstalledGame FindInstalledGame(const LauncherConfig& config, const std::string& xexSha256)
{
    if (xexSha256.empty())
        return {};
    InstalledGame game{ Setting(config, InstallSection(xexSha256), "runtime"), Setting(config, InstallSection(xexSha256), "kit") };
    std::filesystem::path runtime = Path(game.runtime);
    if (runtime.extension() == ".app") // a macOS app: its executable is named after it
        game.runtime = Utf8(runtime / "Contents" / "MacOS" / runtime.stem());
    std::error_code error;
    if (!game.runtime.empty() && !std::filesystem::is_regular_file(Path(game.runtime), error))
        game.runtime.clear();
    return game;
}

void RecordInstalledGame(LauncherConfig& config, const std::string& xexSha256, const std::string& runtime, const std::string& kitVersion)
{
    auto& section = config[InstallSection(xexSha256)];
    section["runtime"] = runtime;
    section["kit"] = kitVersion;
}

void InstallProgress::Apply(std::string_view line, uint64_t now)
{
    line = Trim(line);
    if (line.empty())
        return;
    auto event = [&](std::string_view name, std::string_view& rest) {
        if (!line.starts_with(name) || (line.size() > name.size() && line[name.size()] != ' '))
            return false;
        rest = Trim(line.substr(name.size()));
        return true;
    };
    std::string_view rest;
    if (event("@step", rest))
    {
        if (!steps.empty() && steps.back().endedAt == 0)
            steps.back().endedAt = now;
        size_t space = rest.find(' ');
        std::string id(rest.substr(0, space));
        std::string title(space == std::string_view::npos ? rest : Trim(rest.substr(space + 1)));
        steps.push_back({ id, title, now, 0 });
        done = total = 0;
        log.emplace_back(fmt::format("== {}", title));
    }
    else if (event("@progress", rest))
    {
        unsigned long long d = 0, t = 0;
        if (sscanf(std::string(rest).c_str(), "%llu %llu", &d, &t) == 2)
        {
            done = d;
            total = t;
        }
    }
    else if (event("@log", rest))
        log.emplace_back(rest);
    else if (event("@error", rest))
    {
        error = rest;
        log.emplace_back(fmt::format("error: {}", rest));
    }
    else if (event("@done", rest))
    {
        runtime = rest;
        if (!steps.empty() && steps.back().endedAt == 0)
            steps.back().endedAt = now;
    }
    else
        log.emplace_back(line);
    logLines = log.size();
}

GameInstaller::~GameInstaller()
{
    Cancel();
    Join();
}

bool GameInstaller::Start(const std::optional<InstallKit>& kit, const std::string& iso, const std::filesystem::path& out)
{
    Cancel();
    Join();
    m_cancelled = false;
    {
        std::lock_guard lock(m_mutex);
        m_progress = {};
        m_progress.state = InstallProgress::State::Running;
    }
    auto fail = [&](std::string message) {
        std::lock_guard lock(m_mutex);
        m_progress.state = InstallProgress::State::Failed;
        m_progress.error = std::move(message);
        m_progress.log.push_back(m_progress.error);
        m_progress.logLines = m_progress.log.size();
        return false;
    };

    if (!kit)
        return fail("The installer kit wasn't found. Put the ssa-kit folder (the one with SSABuild in it) next to Skylauncher, "
            "then press Retry.");
    std::error_code error;
    std::filesystem::create_directories(out, error);
    if (error)
        return fail(fmt::format("Couldn't create the game folder {}: {}", Utf8(out), error.message()));
    m_logFile = out / "install.log";
    std::ofstream(m_logFile, std::ios::trunc);

    std::string program = Utf8(kit->ssabuild), outText = Utf8(out), kitText = Utf8(kit->folder);
    std::vector<const char*> arguments{ program.c_str(), "--iso", iso.c_str(), "--out", outText.c_str(), "--kit", kitText.c_str() };
    const char* jobs = SDL_getenv("SKYLAUNCHER_BUILD_JOBS"); // else SSABuild picks
    if (jobs != nullptr && *jobs != 0)
        arguments.insert(arguments.end(), { "--jobs", jobs });
    arguments.push_back(nullptr);
    SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, arguments.data());
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
#if defined(_WIN32)
    // SDL's background processes are the ones it starts with CREATE_NO_WINDOW. They report no exit code, so success
    // is the @done line.
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
    m_process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (m_process == nullptr)
        return fail(fmt::format("Couldn't start {}: {}", program, SDL_GetError()));

#if defined(_WIN32)
    // A job holds SSABuild and the compilers it starts, so cancelling (or closing the launcher) stops them all.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        DWORD pid = DWORD(SDL_GetNumberProperty(SDL_GetProcessProperties(m_process), SDL_PROP_PROCESS_PID_NUMBER, 0));
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, pid);
        if (process == nullptr || !AssignProcessToJobObject(job, process))
        {
            CloseHandle(job);
            job = nullptr;
        }
        if (process != nullptr)
            CloseHandle(process);
    }
    m_job = job;
#endif
    m_running = true;
    m_thread = std::thread([this] { Read(); });
    return true;
}

void GameInstaller::Read()
{
    SDL_IOStream* output = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(SDL_GetProcessProperties(m_process), SDL_PROP_PROCESS_STDOUT_POINTER, nullptr));
    std::ofstream logFile(m_logFile, std::ios::app | std::ios::binary);
    std::string pending;
    char buffer[4096];
    uint64_t exitedAt = 0;
    int exitCode = 0;
    bool exited = false;
    auto apply = [&](std::string_view line) {
        logFile << line << std::endl; // kept if the launcher dies
        std::lock_guard lock(m_mutex);
        m_progress.Apply(line, SDL_GetTicks());
    };
    for (;;)
    {
        size_t read = output != nullptr ? SDL_ReadIO(output, buffer, sizeof(buffer)) : 0;
        if (read > 0)
        {
            pending.append(buffer, read);
            for (size_t end; (end = pending.find('\n')) != std::string::npos; pending.erase(0, end + 1))
                apply(std::string_view(pending).substr(0, end));
            continue;
        }
        if (output == nullptr || SDL_GetIOStatus(output) == SDL_IO_STATUS_EOF || SDL_GetIOStatus(output) == SDL_IO_STATUS_ERROR)
            break;
        // The pipe is non-blocking. Once SSABuild has exited, a process it left behind may still hold the pipe open.
        if (!exited && SDL_WaitProcess(m_process, false, &exitCode))
        {
            exited = true;
            exitedAt = SDL_GetTicks();
        }
        if (exited && SDL_GetTicks() - exitedAt > 1000)
            break;
        if (m_cancelled && !exited && SDL_GetTicks() - m_cancelledAt > 3000)
            SDL_KillProcess(m_process, true);
        SDL_Delay(20);
    }
    if (!pending.empty())
        apply(pending);
    if (!exited)
        SDL_WaitProcess(m_process, true, &exitCode);
    logFile.flush();

    std::lock_guard lock(m_mutex);
    InstallProgress& progress = m_progress;
    if (!progress.steps.empty() && progress.steps.back().endedAt == 0)
        progress.steps.back().endedAt = SDL_GetTicks();
    if (m_cancelled)
        progress.state = InstallProgress::State::Cancelled;
    else if (exitCode == 0 && !progress.runtime.empty())
        progress.state = InstallProgress::State::Done;
    else
    {
        progress.state = InstallProgress::State::Failed;
        if (progress.error.empty())
            progress.error = exitCode != 0 ? fmt::format("The installer stopped with exit code {}.", exitCode)
                                           : std::string("The installer stopped before the game was built.");
    }
    m_running = false;
}

void GameInstaller::Cancel()
{
    if (!m_running || m_process == nullptr)
        return;
    if (!m_cancelled)
        m_cancelledAt = SDL_GetTicks();
    m_cancelled = true;
#if defined(_WIN32)
    if (m_job != nullptr)
        TerminateJobObject(static_cast<HANDLE>(m_job), 1);
    else
        SDL_KillProcess(m_process, true);
#else
    SDL_KillProcess(m_process, false); // SIGTERM lets SSABuild stop its compilers; Read() forces it after a while
#endif
}

void GameInstaller::Join()
{
    if (m_thread.joinable())
        m_thread.join();
    if (m_process != nullptr)
    {
        SDL_DestroyProcess(m_process);
        m_process = nullptr;
    }
#if defined(_WIN32)
    if (m_job != nullptr)
        CloseHandle(static_cast<HANDLE>(m_job));
#endif
    m_job = nullptr;
}

InstallProgress GameInstaller::Snapshot(size_t logTail)
{
    std::lock_guard lock(m_mutex);
    InstallProgress copy;
    copy.state = m_progress.state;
    copy.steps = m_progress.steps;
    copy.done = m_progress.done;
    copy.total = m_progress.total;
    copy.error = m_progress.error;
    copy.runtime = m_progress.runtime;
    copy.logLines = m_progress.log.size();
    size_t first = m_progress.log.size() - std::min(logTail, m_progress.log.size());
    copy.log.assign(m_progress.log.begin() + first, m_progress.log.end());
    return copy;
}

std::string GameInstaller::FullLog()
{
    std::lock_guard lock(m_mutex);
    std::string text;
    for (const std::string& line : m_progress.log)
        text += line + "\n";
    return text;
}
