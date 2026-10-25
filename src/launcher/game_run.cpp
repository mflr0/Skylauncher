#include "launcher/game_run.h"
#include "launcher/game_install.h"
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
        SDL_Process* process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
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
    if (const char* override = SDL_getenv("SKYLAUNCHER_LOG_DIR"); override != nullptr && *override != 0) // tests
        return Path(override);
    return LauncherDataFolder() / "logs";
}

std::filesystem::path RuntimeLogFolder()
{
    if (const char* override = SDL_getenv("SKYLAUNCHER_LOG_DIR"); override != nullptr && *override != 0) // tests
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

    std::vector<std::string> arguments{ runtime, iso };
    arguments.insert(arguments.end(), extraArguments.begin(), extraArguments.end());
    SDL_Process* process = Spawn(arguments, result.error);
    if (process == nullptr)
    {
        result.error = fmt::format("Couldn't start {}: {}", runtime, result.error);
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
                result.notes.push_back(std::string(get("transport") == "usb" ? "USB (Xbox) portals" : "HID portals") + " can't be used: " + why);
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
        result.error = "The game didn't finish the portal test.";
    return result;
}

PortalProbeResult RunPortalProbe(const std::string& runtime, uint32_t timeoutMs)
{
    PortalProbeResult result;
    std::string error;
    SDL_Process* process = Spawn({ runtime, "--portal-probe" }, error);
    if (process == nullptr)
    {
        result.error = fmt::format("Couldn't start the game: {}", error);
        return result;
    }
    std::string output;
    bool timedOut = false;
    int code = Drain(process, [&](const char* data, size_t size) { output.append(data, size); }, timeoutMs, &timedOut);
    SDL_DestroyProcess(process);
    result = ParsePortalProbe(output);
    if (!result.ran)
        result.error = timedOut ? "The portal test took too long and was stopped."
            : code == 2 ? "This version of the game doesn't have the portal test yet. Update the game."
                        : "The game didn't finish the portal test.";
    return result;
}
