#include "launcher/updater.h"
#include "launcher/i18n.h"
#include "launcher/launcher.h"
#include "shared/mini_json.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <fstream>
#include <miniz.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifndef SKYLAUNCHER_VERSION_STRING
#define SKYLAUNCHER_VERSION_STRING "0.0.0"
#endif

namespace fs = std::filesystem;

namespace
{
    fs::path FromUtf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

    std::string ToUtf8(const fs::path& path)
    {
        std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    std::string ReadText(const fs::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), {});
    }

    bool Writable(const fs::path& folder)
    {
        std::error_code error;
        fs::path probe = folder / ".skylauncher-write-test";
        std::ofstream(probe) << 1;
        bool ok = fs::exists(probe, error);
        fs::remove(probe, error);
        return ok;
    }

    std::string CurlProgram()
    {
#if defined(_WIN32)
        if (const char* root = SDL_getenv("SystemRoot"))
        {
            fs::path system = FromUtf8(root) / "System32" / "curl.exe";
            std::error_code error;
            if (fs::exists(system, error))
                return ToUtf8(system);
        }
        return "curl.exe";
#else
        return "curl";
#endif
    }

    // `arguments` run to completion, polling `tick` (which may ask to stop); curl's messages in `output`.
    bool RunCurl(const std::vector<std::string>& arguments, std::string& output, const std::function<bool()>& tick)
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
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
        SDL_Process* process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (process == nullptr)
        {
            output = Tf("curl couldn't start ({})", SDL_GetError());
            return false;
        }
        SDL_IOStream* stream = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_STDOUT_POINTER, nullptr));
        int code = -1;
        bool exited = false;
        char buffer[2048];
        for (;;)
        {
            size_t read = stream != nullptr ? SDL_ReadIO(stream, buffer, sizeof(buffer)) : 0;
            if (read > 0)
            {
                if (output.size() < 4096)
                    output.append(buffer, read);
                continue;
            }
            if (!exited && SDL_WaitProcess(process, false, &code))
                exited = true;
            if (exited)
            {
                // Drain what is left, then stop.
                while (stream != nullptr && (read = SDL_ReadIO(stream, buffer, sizeof(buffer))) > 0)
                    if (output.size() < 4096)
                        output.append(buffer, read);
                break;
            }
            if (!tick())
            {
                SDL_KillProcess(process, true);
                SDL_WaitProcess(process, true, &code);
                SDL_DestroyProcess(process);
                output = "cancelled";
                return false;
            }
            SDL_Delay(40);
        }
        SDL_DestroyProcess(process);
#if defined(_WIN32)
        return code == 0 || code == -1; // a background process reports no code: the file tells
#else
        return code == 0;
#endif
    }

    std::string Trimmed(std::string text)
    {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
            text.pop_back();
        size_t first = 0;
        while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])))
            first++;
        return text.substr(first);
    }

    // A zip entry's name safe to put under a folder: relative, no "..".
    bool SafeEntry(const std::string& name)
    {
        if (name.empty() || name[0] == '/' || name[0] == '\\' || (name.size() > 1 && name[1] == ':'))
            return false;
        size_t begin = 0;
        while (begin <= name.size())
        {
            size_t end = name.find_first_of("/\\", begin);
            if (end == std::string::npos)
                end = name.size();
            if (name.substr(begin, end - begin) == "..")
                return false;
            begin = end + 1;
        }
        return true;
    }

    fs::path SSABuildIn(const fs::path& folder)
    {
#if defined(_WIN32)
        return folder / "SSABuild.exe";
#else
        return folder / "SSABuild";
#endif
    }
}

// ---- Versions -----------------------------------------------------------------------------------------------------

SemVer ParseSemVer(std::string_view text)
{
    SemVer version;
    size_t i = 0;
    while (i < text.size() && !std::isdigit(static_cast<unsigned char>(text[i])))
        i++;
    if (i >= text.size())
        return version;
    int parts[3] = { 0, 0, 0 };
    for (int p = 0; p < 3; p++)
    {
        if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i])))
            break;
        long value = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
            value = std::min(value * 10 + (text[i++] - '0'), 1000000000L);
        parts[p] = int(value);
        if (i < text.size() && text[i] == '.' && p < 2)
            i++;
        else
            break;
    }
    version.major = parts[0];
    version.minor = parts[1];
    version.patch = parts[2];
    version.valid = true;
    if (i < text.size() && text[i] == '-')
    {
        size_t end = text.find('+', i);
        version.pre = std::string(text.substr(i + 1, end == std::string_view::npos ? end : end - i - 1));
    }
    return version;
}

int CompareSemVer(const SemVer& a, const SemVer& b)
{
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.pre == b.pre) return 0;
    if (a.pre.empty()) return 1;  // 1.0.0 is after 1.0.0-beta
    if (b.pre.empty()) return -1;
    return a.pre < b.pre ? -1 : 1;
}

std::string LauncherVersion() { return SKYLAUNCHER_VERSION_STRING; }

// ---- Releases -----------------------------------------------------------------------------------------------------

bool ParseRelease(const std::string& json, ReleaseInfo& release, std::string* error)
{
    JsonValue root;
    std::string parseError;
    if (!ParseJson(json, root, &parseError))
    {
        if (error != nullptr)
            *error = Tf("The answer isn't valid JSON ({})", parseError);
        return false;
    }
    if (root["tag_name"].Str().empty())
    {
        if (error != nullptr)
            *error = root["message"].IsString() ? Tf("GitHub says: {}", root["message"].Str()) : std::string(Tr("The answer has no release in it"));
        return false;
    }
    release = {};
    release.tag = root["tag_name"].Str();
    release.name = root["name"].Str();
    release.notes = root["body"].Str();
    release.pageUrl = root["html_url"].Str();
    release.prerelease = root["prerelease"].boolean;
    const JsonValue& assets = root["assets"];
    for (size_t i = 0; assets.IsArray() && i < assets.array.size(); i++)
        release.assets.push_back({ assets[i]["name"].Str(), assets[i]["browser_download_url"].Str(), uint64_t(assets[i]["size"].number) });
    return true;
}

const ReleaseAsset* FindAsset(const ReleaseInfo& release, std::string_view name)
{
    for (const ReleaseAsset& asset : release.assets)
        if (asset.name == name)
            return &asset;
    return nullptr;
}

std::string FindChecksum(const std::string& sums, std::string_view name)
{
    size_t begin = 0;
    while (begin < sums.size())
    {
        size_t end = sums.find('\n', begin);
        if (end == std::string::npos)
            end = sums.size();
        std::string line = Trimmed(sums.substr(begin, end - begin));
        begin = end + 1;
        size_t space = line.find_first_of(" \t");
        if (space != 64)
            continue;
        std::string file = Trimmed(line.substr(space));
        if (!file.empty() && file[0] == '*')
            file.erase(0, 1);
        if (file == name)
        {
            std::string hash = line.substr(0, 64);
            for (char& c : hash)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            return hash;
        }
    }
    return {};
}

std::string LauncherAssetName()
{
#if defined(_WIN32)
    return "Skylauncher.exe";
#elif defined(__APPLE__)
    return "Skylauncher-macOS.zip";
#else
    return "Skylauncher-x86_64.AppImage";
#endif
}

std::string KitAssetName()
{
#if defined(_WIN32)
    return "ssa-kit-windows-x64.zip";
#elif defined(__APPLE__)
#if defined(__aarch64__)
    return "ssa-kit-macos-arm64.zip";
#else
    return "ssa-kit-macos-x64.zip";
#endif
#else
    return "ssa-kit-linux-x64.zip";
#endif
}

std::string LatestReleaseUrl(const std::string& repo)
{
    if (const char* url = SDL_getenv("SKYLAUNCHER_UPDATE_URL"); url != nullptr && *url != 0)
        return url;
    return "https://api.github.com/repos/" + repo + "/releases/latest";
}

fs::path KitFolderFor(const fs::path& kitRoot, const std::string& tag)
{
    std::string name;
    for (char c : tag)
        name += std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_' ? c : '_';
    return kitRoot / FromUtf8(name.empty() ? "kit" : name);
}

std::string TaggedReleaseUrl(const std::string& repo, const std::string& tag)
{
    if (const char* url = SDL_getenv("SKYLAUNCHER_UPDATE_URL"); url != nullptr && *url != 0)
    {
        std::string base = url;
        return base.substr(0, base.find_last_of('/') + 1) + tag + ".json";
    }
    return "https://api.github.com/repos/" + repo + "/releases/tags/" + tag;
}

// ---- Download and checks ------------------------------------------------------------------------------------------

HttpResult HttpDownload(const std::string& url, const fs::path& file, const std::function<void(uint64_t)>& progress, const std::atomic<bool>* cancel)
{
    HttpResult result;
    if (url.empty() || !(url.starts_with("https://") || url.starts_with("file://") || SDL_getenv("SKYLAUNCHER_UPDATE_URL") != nullptr))
    {
        result.error = Tr("The address isn't a secure one (https)");
        return result;
    }
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    fs::path part = file;
    part += ".part";
    fs::remove(part, error);
    std::string agent = "Skylauncher/" + LauncherVersion();
    std::vector<std::string> arguments{ CurlProgram(), "-fsSL", "--connect-timeout", "15", "--max-time", "1800", "--retry", "2",
        "-A", agent, "-H", "Accept: application/vnd.github+json", "-o", ToUtf8(part), url };
    if (url.starts_with("https://"))
        arguments.insert(arguments.begin() + 1, { "--proto", "=https", "--tlsv1.2" }); // https only, never a redirect to plain http
    std::string output;
    uint64_t lastReport = 0;
    bool ok = RunCurl(arguments, output, [&] {
        if (cancel != nullptr && *cancel)
            return false;
        uint64_t now = SDL_GetTicks();
        if (progress && now - lastReport > 150)
        {
            lastReport = now;
            std::error_code sizeError;
            auto size = fs::file_size(part, sizeError);
            if (!sizeError)
                progress(uint64_t(size));
        }
        return true;
    });
    if (!ok || !fs::exists(part, error))
    {
        fs::remove(part, error);
        output = Trimmed(output);
        if (output.empty())
            output = Tr("no answer");
        result.error = output == "cancelled" ? output : Tf("The download failed: {}", output);
        return result;
    }
    fs::remove(file, error);
    fs::rename(part, file, error);
    if (error)
    {
        result.error = Tf("Couldn't save the download: {}", error.message());
        return result;
    }
    if (progress)
        progress(uint64_t(fs::file_size(file, error)));
    result.ok = true;
    return result;
}

fs::path DownloadVerified(const ReleaseInfo& release, std::string_view assetName, const fs::path& folder,
    const std::function<void(uint64_t)>& progress, const std::atomic<bool>* cancel, std::string& error)
{
    const ReleaseAsset* asset = FindAsset(release, assetName);
    const ReleaseAsset* sums = FindAsset(release, "SHA256SUMS");
    if (asset == nullptr)
    {
        error = Tf("{} has no {} for this computer", release.tag, assetName);
        return {};
    }
    if (sums == nullptr)
    {
        error = Tf("{} can't be verified, so it wasn't installed", release.tag);
        return {};
    }
    std::error_code fsError;
    fs::create_directories(folder, fsError);
    fs::path sumsFile = folder / "SHA256SUMS";
    if (HttpResult result = HttpDownload(sums->url, sumsFile, {}, cancel); !result.ok)
    {
        error = result.error;
        return {};
    }
    std::string expected = FindChecksum(ReadText(sumsFile), assetName);
    fs::remove(sumsFile, fsError);
    if (expected.empty())
    {
        error = Tf("{} can't be verified, so it wasn't installed", assetName);
        return {};
    }
    fs::path file = folder / FromUtf8(std::string(assetName));
    if (HttpResult result = HttpDownload(asset->url, file, progress, cancel); !result.ok)
    {
        error = result.error;
        return {};
    }
    std::string data = ReadText(file);
    std::string actual = Sha256Hex({ reinterpret_cast<const uint8_t*>(data.data()), data.size() });
    if (actual != expected)
    {
        fs::remove(file, fsError);
        error = Tr("The download is damaged: nothing was installed");
        return {};
    }
    return file;
}

// ---- Replacing what runs ------------------------------------------------------------------------------------------

bool ReplaceInPlace(const fs::path& target, const fs::path& fresh, std::string& error)
{
    std::error_code fsError;
    fs::path backup = target;
    backup += ".old";
    fs::remove_all(backup, fsError);
    bool hadTarget = fs::exists(target, fsError);
    if (hadTarget)
    {
        fs::rename(target, backup, fsError);
        if (fsError)
        {
            error = Tf("Couldn't move {} aside: {}", ToUtf8(target.filename()), fsError.message());
            return false;
        }
    }
    fs::rename(fresh, target, fsError);
    if (fsError)
    {
        std::string reason = fsError.message();
        if (hadTarget)
            fs::rename(backup, target, fsError); // roll back
        error = Tf("Couldn't put the new {} in place: {}", ToUtf8(target.filename()), reason);
        return false;
    }
    return true;
}

void CleanupOldUpdate(const fs::path& target)
{
    if (target.empty())
        return;
    fs::path backup = target;
    backup += ".old";
    std::error_code error;
    fs::remove_all(backup, error);
    fs::remove_all(target.parent_path() / ".skylauncher-update", error);
}

bool ExtractZip(const fs::path& zip, const fs::path& folder, std::string& error)
{
    mz_zip_archive archive{};
    if (!mz_zip_reader_init_file(&archive, ToUtf8(zip).c_str(), 0))
    {
        error = Tr("The download isn't a zip file");
        return false;
    }
    std::error_code fsError;
    fs::create_directories(folder, fsError);
    bool ok = true;
    for (mz_uint i = 0; ok && i < mz_zip_reader_get_num_files(&archive); i++)
    {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&archive, i, &stat))
        {
            error = Tr("The zip file is damaged");
            ok = false;
            break;
        }
        std::string name = stat.m_filename;
        if (!SafeEntry(name))
        {
            error = Tr("The zip file is damaged");
            ok = false;
            break;
        }
        fs::path out = folder / FromUtf8(name);
        if (mz_zip_reader_is_file_a_directory(&archive, i))
        {
            fs::create_directories(out, fsError);
            continue;
        }
        fs::create_directories(out.parent_path(), fsError);
        unsigned mode = (stat.m_external_attr >> 16) & 07777;
        bool fromUnix = (stat.m_version_made_by >> 8) == 3; // `unix` is a predefined macro on Linux
        if (fromUnix && (stat.m_external_attr >> 16 & 0170000) == 0120000) // a symbolic link: its target is the content
        {
            std::string target(size_t(stat.m_uncomp_size), '\0');
            if (mz_zip_reader_extract_to_mem(&archive, i, target.data(), target.size(), 0))
            {
                fs::remove(out, fsError);
                fs::create_symlink(FromUtf8(target), out, fsError);
            }
            continue;
        }
        if (!mz_zip_reader_extract_to_file(&archive, i, ToUtf8(out).c_str(), 0))
        {
            error = Tf("Couldn't unpack {}", name);
            ok = false;
            break;
        }
        if (fromUnix && mode != 0)
            fs::permissions(out, fs::perms(mode), fs::perm_options::replace, fsError);
    }
    mz_zip_reader_end(&archive);
    return ok;
}

fs::path LauncherSelf()
{
#if defined(_WIN32)
    wchar_t buffer[32768];
    DWORD length = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
    return length > 0 && length < std::size(buffer) ? fs::path(std::wstring(buffer, length)) : fs::path();
#elif defined(__APPLE__)
    const char* base = SDL_GetBasePath();
    if (base == nullptr)
        return {};
    // .../Skylauncher.app/Contents/Resources/ -> .../Skylauncher.app
    fs::path resources = FromUtf8(base);
    if (resources.filename().empty())
        resources = resources.parent_path();
    fs::path app = resources.parent_path().parent_path();
    if (resources.filename() == "Resources" && app.extension() == ".app" && Writable(app.parent_path()))
        return app;
    return {};
#else
    const char* image = SDL_getenv("APPIMAGE");
    return image != nullptr && *image != 0 ? FromUtf8(image) : fs::path();
#endif
}

bool InstallKitZip(const fs::path& zip, const fs::path& kitFolder, std::string& error)
{
    std::error_code fsError;
    fs::path staging = kitFolder;
    staging += ".new";
    fs::remove_all(staging, fsError);
    if (!ExtractZip(zip, staging, error))
    {
        fs::remove_all(staging, fsError);
        return false;
    }
    // A zip made around one folder ("ssa-kit-windows-x64/...") is that folder.
    fs::path fresh = staging;
    if (!fs::exists(SSABuildIn(fresh), fsError))
    {
        std::vector<fs::path> children;
        for (const auto& entry : fs::directory_iterator(staging, fsError))
            children.push_back(entry.path());
        if (children.size() == 1 && fs::exists(SSABuildIn(children[0]), fsError))
            fresh = children[0];
    }
    if (!fs::exists(SSABuildIn(fresh), fsError))
    {
        fs::remove_all(staging, fsError);
        error = Tr("The download isn't a game kit");
        return false;
    }
    bool ok;
    if (fs::exists(kitFolder, fsError))
        ok = ReplaceInPlace(kitFolder, fresh, error);
    else
    {
        fs::rename(fresh, kitFolder, fsError);
        ok = !fsError;
        if (!ok)
            error = Tf("Couldn't put the kit in place: {}", fsError.message());
    }
    fs::remove_all(staging, fsError);
    if (ok)
    {
        fs::path old = kitFolder;
        old += ".old";
        fs::remove_all(old, fsError);
    }
    return ok;
}

fs::path DownloadKit(const std::string& repo, const std::string& tag, const fs::path& kitRoot,
    const std::function<void(uint64_t, uint64_t)>& progress, const std::atomic<bool>* cancel, std::string& error, std::string* releaseTag)
{
    std::error_code fsError;
    fs::path work = kitRoot / ".download";
    fs::remove_all(work, fsError);
    fs::create_directories(work, fsError);
    ReleaseInfo release;
    std::string tagError;
    fs::path file = work / "release.json";
    // The release of this launcher's version, else the latest one.
    if (!(HttpDownload(TaggedReleaseUrl(repo, tag), file, {}, cancel).ok && ParseRelease(ReadText(file), release, &tagError)))
    {
        HttpResult result = HttpDownload(LatestReleaseUrl(repo), file, {}, cancel);
        if (result.ok)
            result.ok = ParseRelease(ReadText(file), release, &result.error);
        if (!result.ok)
        {
            fs::remove_all(work, fsError);
            error = result.error;
            return {};
        }
    }
    const ReleaseAsset* asset = FindAsset(release, KitAssetName());
    if (asset == nullptr)
    {
        fs::remove_all(work, fsError);
        error = Tf("The release {} has no {}", release.tag, KitAssetName());
        return {};
    }
    uint64_t size = asset->size;
    fs::path zip = DownloadVerified(release, KitAssetName(), work, [&](uint64_t bytes) { if (progress) progress(bytes, size); }, cancel, error);
    fs::path folder;
    if (!zip.empty())
    {
        folder = KitFolderFor(kitRoot, release.tag);
        if (InstallKitZip(zip, folder, error))
        {
            if (releaseTag != nullptr)
                *releaseTag = release.tag;
        }
        else
            folder.clear();
    }
    fs::remove_all(work, fsError);
    return folder;
}

void KitDownloader::Join()
{
    if (m_thread.joinable())
        m_thread.join();
}

KitDownloader::Snapshot_ KitDownloader::Snapshot()
{
    std::lock_guard lock(m_mutex);
    return m_state;
}

void KitDownloader::Start(const std::string& repo, const std::string& tag, const fs::path& kitRoot)
{
    if (m_running)
        return;
    Join();
    m_running = true;
    m_cancel = false;
    { std::lock_guard lock(m_mutex); m_state = {}; m_state.state = State::Running; }
    m_thread = std::thread([this, repo, tag, kitRoot] {
        std::string error, releaseTag;
        auto progress = [&](uint64_t bytes, uint64_t size) {
            std::lock_guard lock(m_mutex);
            m_state.done = bytes;
            m_state.total = size;
        };
        fs::path folder = DownloadKit(repo, tag, kitRoot, progress, &m_cancel, error, &releaseTag);
        std::lock_guard lock(m_mutex);
        m_state.state = !folder.empty() ? State::Done : m_cancel ? State::Cancelled : State::Failed;
        m_state.error = error;
        m_state.tag = releaseTag;
        m_state.folder = folder;
        m_running = false;
    });
}

// ---- The manager --------------------------------------------------------------------------------------------------

void UpdateManager::Join()
{
    if (m_thread.joinable())
        m_thread.join();
}

void UpdateManager::Dismiss()
{
    if (m_busy)
        return;
    std::lock_guard lock(m_mutex);
    if (m_state.phase == Phase::Failed || m_state.phase == Phase::Done)
        m_state.phase = Phase::Idle;
    m_state.error.clear();
    m_state.note.clear();
}

UpdateManager::State UpdateManager::Snapshot()
{
    std::lock_guard lock(m_mutex);
    return m_state;
}

void UpdateManager::SetPhase(Phase phase, const char* doing)
{
    std::lock_guard lock(m_mutex);
    m_state.phase = phase;
    if (phase == Phase::Downloading || phase == Phase::Verifying || phase == Phase::Installing)
        m_state.doing = doing;
    if (phase == Phase::Downloading)
        m_state.progress = -1;
}

void UpdateManager::Fail(const std::string& message)
{
    std::lock_guard lock(m_mutex);
    m_state.phase = Phase::Failed;
    m_state.error = message;
}

void UpdateManager::Check(const Settings& settings)
{
    if (m_busy)
        return;
    Join();
    m_busy = true;
    m_cancel = false;
    m_settings = settings;
    { std::lock_guard lock(m_mutex); m_state.phase = Phase::Checking; m_state.checkError.clear(); m_state.error.clear(); }
    m_thread = std::thread([this] {
        fs::path temp = fs::temp_directory_path() / "skylauncher-update";
        auto fetch = [&](const std::string& repo, ReleaseInfo& release, std::string& error) {
            fs::path file = temp / "latest.json";
            HttpResult result = HttpDownload(LatestReleaseUrl(repo), file, {}, &m_cancel);
            if (!result.ok)
            {
                error = result.error;
                return false;
            }
            return ParseRelease(ReadText(file), release, &error);
        };
        ReleaseInfo launcher, kit;
        std::string error, kitError;
        bool launcherOk = fetch(m_settings.repo, launcher, error);
        bool kitOk = false;
        if (!m_settings.kitRepo.empty())
        {
            // The kit has its own releases: the same file name would be overwritten by the first fetch, so each is read at once.
            kitOk = fetch(m_settings.kitRepo, kit, kitError);
        }
        std::error_code fsError;
        fs::remove_all(temp, fsError);
        {
            std::lock_guard lock(m_mutex);
            m_state.phase = Phase::Idle;
            m_state.checked = launcherOk;
            m_state.checkError = launcherOk ? std::string() : error;
            m_state.launcherNewer = launcherOk && !launcher.prerelease && IsNewer(launcher.tag, LauncherVersion());
            if (launcherOk)
                m_state.launcher = launcher;
            m_state.kitNewer = false;
            if (kitOk && FindAsset(kit, KitAssetName()) != nullptr)
            {
                const std::string& installed = m_settings.kitInstalled;
                m_state.kitNewer = IsNewer(kit.tag, installed) || (!installed.empty() && !ParseSemVer(installed).valid && kit.tag != installed);
                m_state.kit = kit;
            }
        }
        m_busy = false;
    });
}

void UpdateManager::UpdateLauncher()
{
    if (m_busy)
        return;
    Join();
    m_busy = true;
    m_cancel = false;
    m_thread = std::thread([this] {
        ReleaseInfo release;
        { std::lock_guard lock(m_mutex); release = m_state.launcher; m_state.error.clear(); m_state.note.clear(); }
        auto progress = [&](uint64_t bytes) {
            const ReleaseAsset* asset = FindAsset(release, LauncherAssetName());
            std::lock_guard lock(m_mutex);
            m_state.progress = asset != nullptr && asset->size > 0 ? std::min(1.0f, float(double(bytes) / double(asset->size))) : -1.0f;
        };
        fs::path self = LauncherSelf();
        std::error_code fsError;
        SetPhase(Phase::Downloading, "launcher");
#if defined(__APPLE__)
        if (self.empty())
        {
            // The app can't be replaced where it is: the disk image, opened for the player to drag from.
            std::string error;
            fs::path folder = fs::temp_directory_path() / "skylauncher-update";
            fs::path dmg = DownloadVerified(release, "Skylauncher-macOS.dmg", folder, progress, &m_cancel, error);
            if (dmg.empty())
                Fail(error);
            else
            {
                SDL_OpenURL(("file://" + ToUtf8(dmg)).c_str());
                std::lock_guard lock(m_mutex);
                m_state.phase = Phase::Done;
                m_state.note = Tr("Drag Skylauncher to Applications, replacing the old one.");
            }
            m_busy = false;
            return;
        }
#else
        if (self.empty())
        {
            SDL_OpenURL(release.pageUrl.c_str());
            std::lock_guard lock(m_mutex);
            m_state.phase = Phase::Done;
            m_state.note = Tr("Can't update here: the release page was opened.");
            m_busy = false;
            return;
        }
#endif
        fs::path folder = self.parent_path() / ".skylauncher-update";
        std::string error;
        fs::path file = DownloadVerified(release, LauncherAssetName(), folder, progress, &m_cancel, error);
        if (file.empty())
        {
            fs::remove_all(folder, fsError);
            Fail(error);
            m_busy = false;
            return;
        }
        SetPhase(Phase::Installing, "launcher");
        fs::path fresh = file;
#if defined(__APPLE__)
        if (!ExtractZip(file, folder / "app", error))
        {
            fs::remove_all(folder, fsError);
            Fail(error);
            m_busy = false;
            return;
        }
        fresh = folder / "app" / self.filename();
        if (!fs::exists(fresh, fsError))
        {
            fs::remove_all(folder, fsError);
            Fail(Tr("The download holds no Skylauncher.app"));
            m_busy = false;
            return;
        }
#elif !defined(_WIN32)
        fs::permissions(fresh, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, fsError);
#endif
        bool ok = ReplaceInPlace(self, fresh, error);
        if (ok)
        {
            std::lock_guard lock(m_mutex);
            m_state.phase = Phase::Done;
            m_state.restartNeeded = true;
            m_state.launcherNewer = false;
        }
        else
            Fail(error);
        m_busy = false;
    });
}

void UpdateManager::UpdateKit()
{
    if (m_busy)
        return;
    Join();
    m_busy = true;
    m_cancel = false;
    m_thread = std::thread([this] {
        ReleaseInfo release;
        fs::path kitFolder;
        { std::lock_guard lock(m_mutex); release = m_state.kit; kitFolder = m_settings.kitFolder; m_state.error.clear(); m_state.note.clear(); }
        std::error_code fsError;
        if (kitFolder.empty() && !m_settings.kitRoot.empty()) // no kit next to the launcher: a new one goes in kit/<tag>
        {
            kitFolder = KitFolderFor(m_settings.kitRoot, release.tag);
            fs::create_directories(m_settings.kitRoot, fsError);
        }
        if (kitFolder.empty() || !Writable(kitFolder.parent_path()))
        {
            Fail(Tr("Can't write next to Skylauncher: move it to a writable folder."));
            m_busy = false;
            return;
        }
        SetPhase(Phase::Downloading, "kit");
        auto progress = [&](uint64_t bytes) {
            const ReleaseAsset* asset = FindAsset(release, KitAssetName());
            std::lock_guard lock(m_mutex);
            m_state.progress = asset != nullptr && asset->size > 0 ? std::min(1.0f, float(double(bytes) / double(asset->size))) : -1.0f;
        };
        fs::path folder = kitFolder.parent_path() / ".skylauncher-update";
        std::string error;
        fs::path zip = DownloadVerified(release, KitAssetName(), folder, progress, &m_cancel, error);
        if (!zip.empty())
        {
            SetPhase(Phase::Installing, "kit");
            if (InstallKitZip(zip, kitFolder, error))
            {
                std::lock_guard lock(m_mutex);
                m_state.phase = Phase::Done;
                m_state.kitNewer = false;
                m_state.note = Tr("The game kit is up to date: update the game to use it.");
                fs::remove_all(folder, fsError);
                m_busy = false;
                return;
            }
        }
        fs::remove_all(folder, fsError);
        Fail(error);
        m_busy = false;
    });
}
