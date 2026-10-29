#include "launcher/game_run.h"
#include "launcher/env.h"
#include "launcher/i18n.h"
#include "launcher/game_install.h"
#include "launcher/game_keys.h"
#include "launcher/game_names.h"
#include "launcher/game_settings.h"
#include "launcher/launcher.h"
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
    std::filesystem::path Path(const std::string& utf8)
    {
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    }

    std::string Utf8(const std::filesystem::path& path)
    {
        std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    // Starts `arguments` with stdout and stderr piped; false (with SDL's error in `error`) when it can't start.
    SDL_Process* Spawn(const std::vector<std::string>& arguments, std::string& error)
    {
        std::vector<const char*> argv;
        for (const std::string& argument : arguments)
            argv.push_back(argument.c_str());
        argv.push_back(nullptr);
        SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
#if defined(_WIN32)
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true); // no console window
#endif
        SDL_Environment* environment = CreateGameEnvironment(); // both names of every variable: older and newer games
        if (environment != nullptr)
            SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
        SDL_Process* process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (environment != nullptr)
            SDL_DestroyEnvironment(environment);
        if (process == nullptr)
            error = SDL_GetError();
        return process;
    }

    // Reads the process's output until it ends, handing every chunk to `sink`; returns the exit code (-1 if unknown).
    // `timeoutMs` > 0 kills it after that long (the exit code is then -1 and `timedOut` is set).
    template<class Sink>
    int Drain(SDL_Process* process, Sink&& sink, uint32_t timeoutMs, bool* timedOut)
    {
        SDL_IOStream* output = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_STDOUT_POINTER, nullptr));
        int exitCode = -1;
        bool exited = false;
        uint64_t start = SDL_GetTicks(), exitedAt = 0;
#if defined(_WIN32)
        // SDL doesn't report the exit code of a background process: read it from a handle of our own.
        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(SDL_GetNumberProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_PID_NUMBER, 0)));
#endif
        char buffer[8192];
        for (;;)
        {
            size_t read = output != nullptr ? SDL_ReadIO(output, buffer, sizeof(buffer)) : 0;
            if (read > 0)
            {
                sink(buffer, read);
                continue;
            }
            if (output == nullptr || SDL_GetIOStatus(output) == SDL_IO_STATUS_EOF || SDL_GetIOStatus(output) == SDL_IO_STATUS_ERROR)
                break;
            if (!exited && SDL_WaitProcess(process, false, &exitCode))
            {
                exited = true;
                exitedAt = SDL_GetTicks();
            }
            if (exited && SDL_GetTicks() - exitedAt > 1000) // a child that kept the pipe open
                break;
            if (timeoutMs > 0 && !exited && SDL_GetTicks() - start > timeoutMs)
            {
                SDL_KillProcess(process, true);
                if (timedOut != nullptr)
                    *timedOut = true;
                timeoutMs = 0;
            }
            SDL_Delay(15);
        }
        if (!exited)
            SDL_WaitProcess(process, true, &exitCode);
#if defined(_WIN32)
        if (handle != nullptr)
        {
            DWORD code = 0;
            if (GetExitCodeProcess(handle, &code) && code != STILL_ACTIVE)
                exitCode = int(code);
            CloseHandle(handle);
        }
#endif
        return exitCode;
    }

    // Minimal reader for the probe's lines: one flat JSON object, string / number / boolean values.
    bool ParseFlatObject(const std::string& line, std::vector<std::pair<std::string, std::string>>& fields)
    {
        size_t i = line.find('{');
        if (i == std::string::npos)
            return false;
        auto skip = [&] { while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++; };
        auto string = [&](std::string& out) {
            if (i >= line.size() || line[i] != '"')
                return false;
            for (i++; i < line.size() && line[i] != '"'; i++)
            {
                if (line[i] == '\\' && i + 1 < line.size())
                {
                    char c = line[++i];
                    out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
                }
                else
                    out += line[i];
            }
            if (i >= line.size())
                return false;
            i++;
            return true;
        };
        i++;
        for (;;)
        {
            skip();
            if (i < line.size() && line[i] == '}')
                return true;
            std::string key, value;
            if (!string(key))
                return false;
            skip();
            if (i >= line.size() || line[i] != ':')
                return false;
            i++;
            skip();
            if (i < line.size() && line[i] == '"')
            {
                if (!string(value))
                    return false;
            }
            else
                while (i < line.size() && line[i] != ',' && line[i] != '}')
                    value += line[i++];
            while (!value.empty() && (value.back() == ' ' || value.back() == '\r'))
                value.pop_back();
            fields.emplace_back(key, value);
            skip();
            if (i < line.size() && line[i] == ',')
                i++;
        }
    }
}

std::filesystem::path GameLogFolder()
{
    if (const char* override = LauncherEnv("SKY_LAUNCHER_LOG_DIR"); override != nullptr && *override != 0) // tests
        return Path(override);
    return LauncherDataFolder() / "logs";
}

std::filesystem::path RuntimeLogFolder()
{
    if (const char* override = LauncherEnv("SKY_LAUNCHER_LOG_DIR"); override != nullptr && *override != 0) // tests
        return Path(override);
    return GameInstallFolder({});
}

GameRunResult RunGame(const std::string& runtime, const std::string& iso, const std::vector<std::string>& extraArguments)
{
    GameRunResult result;
    std::filesystem::path folder = GameLogFolder();
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::filesystem::path capture = folder / CaptureLogName;
    std::filesystem::rename(capture, folder / "skylauncher-game.prev.log", error);

    // On Linux SDL forks before it finds out that the program is missing: the child then only exits.
    if (!std::filesystem::exists(Path(runtime), error))
    {
        result.error = Tr("Couldn't start the game: the file is missing");
        return result;
    }
    std::vector<std::string> arguments{ runtime, iso };
    arguments.insert(arguments.end(), extraArguments.begin(), extraArguments.end());
    SDL_Process* process = Spawn(arguments, result.error);
    if (process == nullptr)
    {
        result.error = Tf("Couldn't start the game: {}", result.error);
        return result;
    }
    result.started = true;
    std::ofstream log(capture, std::ios::binary | std::ios::trunc);
    log << "# " << runtime << " " << iso << "\n";
    result.exitCode = Drain(process, [&](const char* data, size_t size) { log.write(data, std::streamsize(size)); log.flush(); }, 0, nullptr);
    log << "\n# exit code " << result.exitCode << "\n";
    SDL_DestroyProcess(process);
    return result;
}

GameLaunch PrepareGameLaunch(const GameInfo& game, LauncherConfig& config, const std::string& configPath, const std::string& installed)
{
    GameLaunch launch;
    launch.runtime = FindRuntime(game, config, true);
    if (launch.runtime.empty())
        launch.runtime = !installed.empty() ? installed : FindRuntime(game, config);
    if (launch.runtime.empty())
    {
        launch.error = Tf("The game ({}) wasn't found next to Arkeyan Launcher.\n\nPut it in the same folder.", game.runtime);
        return launch;
    }
    // The video settings of this game are what the runtime reads from [video] (game_settings.h), and its keys from [input].
    ApplyGameVideo(config, game.id);
    ApplyGameInput(config, game.id);
    SaveLauncherConfig(configPath, config);
    // Settings the runtime already takes from its environment (the rest it reads from launcher.ini; docs/settings.md).
    auto setting = [&](const char* section, const char* key) {
        auto entry = config.find(section);
        if (entry == config.end())
            return std::string();
        auto value = entry->second.find(key);
        return value != entry->second.end() ? value->second : std::string();
    };
    auto pass = [&](const char* variable, const char* section, const char* key) {
        if (std::string value = setting(section, key); !value.empty())
            launch.variables.emplace_back(variable, value);
    };
    pass("SKY_PORTAL", "portal", "mode");
    // [portal] place is the older list of figures to start on the portal; once the seats moved to the session file
    // ([collection] moved) a left-over one would put its figures back over the seats the player chose.
    if (setting("collection", "moved") != "1")
        pass("SKY_PORTAL_PLACE", "portal", "place");
    // A language the disc does not hold is not passed as is: the text takes the disc's first language and the voices
    // follow the text, which is what the Language tab shows then. Nothing known about the disc: passed as saved.
    const art::DiscLanguages onDisc = StoredDiscLanguages(config, game.id);
    auto passLanguage = [&](const char* variable, const char* key, const std::vector<std::string>& held, bool firstHeld) {
        const std::string value = setting(game.id, key);
        if (value.empty())
            return;
        if (held.empty() || std::find(held.begin(), held.end(), value) != held.end())
            launch.variables.emplace_back(variable, value);
        else if (firstHeld)
            launch.variables.emplace_back(variable, held.front());
    };
    passLanguage("SKY_LANGUAGE", "language", onDisc.text, true);
    passLanguage("SKY_VOICE_LANGUAGE", "voice_language", onDisc.voice, false);
    // A game built before the one data folder would look for its saves and settings in the older folders, which the
    // migration emptied: point it at the new ones until "Update the game" rebuilds it (docs/install.md, "Older builds").
    const std::filesystem::path gameFolder = GameInstallFolder({});
    if (IsOlderLayoutBuild(Path(launch.runtime), gameFolder))
    {
        launch.variables.emplace_back("SKY_LAUNCHER_INI", configPath);
        launch.variables.emplace_back("SKY_SAVE_DIR", Utf8(gameFolder / "saves"));
        launch.variables.emplace_back("SKY_LOG", Utf8(gameFolder / "SSARecomp.log"));
        launch.variables.emplace_back("SKY_PORTRAITS", Utf8(gameFolder / "portraits"));
    }
    return launch;
}

GameSession::GameSession(std::vector<std::string> games, Marker marker)
    : m_games(std::move(games)), m_marker(std::move(marker))
{
    if (!m_marker)
        m_marker = [](const std::string& game) { return GameIsRunning(FoldersOf(game)); };
}

GameSession::~GameSession()
{
    if (m_log != nullptr)
        SDL_CloseIO(m_log);
    if (m_process != nullptr)
        SDL_DestroyProcess(m_process); // the game itself is not stopped
#if defined(_WIN32)
    if (m_handle != nullptr)
        CloseHandle(HANDLE(m_handle));
#endif
}

std::string GameSession::Marked() const
{
    for (const std::string& game : m_games)
        if (m_marker(game))
            return game;
    return {};
}

bool GameSession::Start(const std::string& game, const GameLaunch& launch, const std::string& iso, std::string& error)
{
    // A game started by hand a moment ago counts too: the markers are looked at now, not at the next poll.
    if (m_game.empty())
        m_game = Marked();
    if (!m_game.empty())
    {
        error = Tf("{} is running", GameName(m_game, GameNameForm::Short));
        return false;
    }
    std::error_code ignored;
    if (launch.runtime.empty() || !std::filesystem::exists(Path(launch.runtime), ignored))
    {
        error = Tr("Couldn't start the game: the file is missing");
        return false;
    }
    // The output goes straight into the file (the previous run's kept as .prev): nothing of the launcher stands between the
    // game and its log, and a launcher that is closed leaves the game running.
    const std::filesystem::path folder = GameLogFolder(), capture = folder / CaptureLogName;
    std::filesystem::create_directories(folder, ignored);
    std::filesystem::rename(capture, folder / "skylauncher-game.prev.log", ignored);
    SDL_IOStream* log = SDL_IOFromFile(Utf8(capture).c_str(), "ab");
    if (log != nullptr)
    {
        const std::string head = fmt::format("# {} {}\n", launch.runtime, iso);
        SDL_WriteIO(log, head.data(), head.size());
        SDL_FlushIO(log);
    }
    std::vector<const char*> argv{ launch.runtime.c_str(), iso.c_str(), nullptr };
    SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    if (log != nullptr)
    {
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, log);
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    }
    else
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
#if defined(_WIN32)
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true); // no console window
#endif
    SDL_Environment* environment = CreateGameEnvironment(launch.variables);
    if (environment != nullptr)
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
    SDL_Process* process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (environment != nullptr)
        SDL_DestroyEnvironment(environment);
    if (process == nullptr)
    {
        error = Tf("Couldn't start the game: {}", SDL_GetError());
        if (log != nullptr)
            SDL_CloseIO(log);
        return false;
    }
    m_process = process;
    m_log = log;
    m_game = game;
#if defined(_WIN32)
    // SDL doesn't report the exit code of a background process: read it from a handle of our own.
    m_handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, DWORD(SDL_GetNumberProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_PID_NUMBER, 0)));
#endif
    fprintf(stderr, "launcher: %s started (process %lld)\n", game.c_str(), (long long)SDL_GetNumberProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_PID_NUMBER, 0));
    return true;
}

std::optional<GameSession::Ended> GameSession::Poll(uint64_t nowMs)
{
    if (m_process != nullptr)
    {
        int exitCode = -1;
        if (!SDL_WaitProcess(m_process, false, &exitCode))
            return std::nullopt;
#if defined(_WIN32)
        if (m_handle != nullptr)
        {
            DWORD code = 0;
            if (GetExitCodeProcess(HANDLE(m_handle), &code) && code != STILL_ACTIVE)
                exitCode = int(code);
            CloseHandle(HANDLE(m_handle));
            m_handle = nullptr;
        }
#endif
        if (m_log != nullptr)
        {
            const std::string tail = fmt::format("\n# exit code {}\n", exitCode);
            SDL_WriteIO(m_log, tail.data(), tail.size());
            SDL_CloseIO(m_log);
            m_log = nullptr;
        }
        SDL_DestroyProcess(m_process);
        m_process = nullptr;
        m_nextLook = 0; // and the markers again at once
        fprintf(stderr, "launcher: %s stopped (exit code %d)\n", m_game.c_str(), exitCode);
        return Ended{ std::exchange(m_game, std::string()), true, exitCode };
    }
    if (nowMs < m_nextLook)
        return std::nullopt;
    m_nextLook = nowMs + MarkerMs;
    std::string marked = Marked();
    if (marked == m_game)
        return std::nullopt;
    std::string before = std::exchange(m_game, std::move(marked));
    if (before.empty())
        return std::nullopt; // one found running
    return Ended{ std::move(before), false, 0 };
}

std::string ReadLogTail(const std::filesystem::path& file, size_t maxBytes)
{
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream)
        return {};
    size_t size = size_t(stream.tellg());
    size_t start = size > maxBytes ? size - maxBytes : 0;
    stream.seekg(std::streamoff(start));
    std::string text(size - start, '\0');
    stream.read(text.data(), std::streamsize(text.size()));
    if (start > 0)
        if (size_t newline = text.find('\n'); newline != std::string::npos)
            text.erase(0, newline + 1);
    return text;
}

std::string GameLogText(size_t maxBytesEach)
{
    std::string text;
    for (const auto& [folder, name] : { std::pair{ RuntimeLogFolder(), RuntimeLogName }, std::pair{ GameLogFolder(), CaptureLogName } })
        if (std::string part = ReadLogTail(folder / name, maxBytesEach); !part.empty())
            text += fmt::format("===== {} =====\n{}\n", name, part);
    return text;
}

PortalProbeResult ParsePortalProbe(const std::string& output)
{
    PortalProbeResult result;
    size_t begin = 0;
    while (begin <= output.size())
    {
        size_t end = output.find('\n', begin);
        if (end == std::string::npos)
            end = output.size();
        std::vector<std::pair<std::string, std::string>> fields;
        if (ParseFlatObject(output.substr(begin, end - begin), fields))
        {
            auto get = [&](const char* key) {
                for (const auto& [name, value] : fields)
                    if (name == key)
                        return value;
                return std::string();
            };
            if (!get("transport").empty() && get("available") == "false")
            {
                std::string why = get("status");
                if (why.starts_with("error: "))
                    why.erase(0, 7);
                result.notes.push_back(Tf("{} can't be used: {}", get("transport") == "usb" ? Tr("USB (Xbox) portals") : Tr("HID portals"), why));
            }
            else if (get("done") == "true")
                result.ran = true;
            else if (!get("kind").empty())
            {
                PortalProbeEntry entry{ get("kind"), get("vid"), get("pid"), get("product"), get("opened") == "true", get("status") };
                entry.id = get("id");
                entry.model = get("model");
                entry.verdict = get("verdict");
                entry.verdictReason = get("verdict_reason");
                if (entry.product.empty())
                    entry.product = get("name"); // the first draft of the contract
                if (entry.status.empty())
                    entry.status = entry.opened ? "ok" : "error: not opened";
                result.portals.push_back(std::move(entry));
            }
        }
        begin = end + 1;
    }
    if (!result.ran)
        result.error = Tr("The game didn't finish the portal test.");
    return result;
}

PortalProbeResult RunPortalProbe(const std::string& runtime, uint32_t timeoutMs)
{
    PortalProbeResult result;
    std::string error;
    SDL_Process* process = Spawn({ runtime, "--portal-probe" }, error);
    if (process == nullptr)
    {
        result.error = Tf("Couldn't start the game: {}", error);
        return result;
    }
    std::string output;
    bool timedOut = false;
    int code = Drain(process, [&](const char* data, size_t size) { output.append(data, size); }, timeoutMs, &timedOut);
    SDL_DestroyProcess(process);
    result = ParsePortalProbe(output);
    if (!result.ran)
        result.error = timedOut ? Tr("The portal test took too long and was stopped.")
            : code == 2 ? Tr("This version of the game doesn't have the portal test yet. Update the game.")
                        : Tr("The game didn't finish the portal test.");
    return result;
}
