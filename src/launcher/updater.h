#pragma once

// Updates from GitHub releases (docs/updates.md): the launcher itself (Skylauncher.exe, the .app, the AppImage) and, from
// a configurable repository, the player kit. No telemetry: one request to api.github.com for the latest release, at most
// once a day, then the asset. HTTPS is curl (curl.exe ships with Windows 10 and 11, as SSABuild already relies on).
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

struct SemVer
{
    int major = 0, minor = 0, patch = 0;
    std::string pre; // "beta.1": before the release of the same numbers
    bool valid = false;
};
// "v1.2.3", "1.2", "kit-2.0.1-beta" (the first digits found): invalid when there are none.
SemVer ParseSemVer(std::string_view text);
int CompareSemVer(const SemVer& a, const SemVer& b); // <0, 0, >0
inline bool IsNewer(std::string_view candidate, std::string_view current)
{
    SemVer a = ParseSemVer(candidate), b = ParseSemVer(current);
    return a.valid && b.valid && CompareSemVer(a, b) > 0;
}
std::string LauncherVersion(); // the built version, "0.1.0"

struct ReleaseAsset
{
    std::string name, url;
    uint64_t size = 0;
};
struct ReleaseInfo
{
    std::string tag, name, notes, pageUrl;
    bool prerelease = false;
    std::vector<ReleaseAsset> assets;
};
bool ParseRelease(const std::string& json, ReleaseInfo& release, std::string* error = nullptr);
const ReleaseAsset* FindAsset(const ReleaseInfo& release, std::string_view name);
// The expected hash of `name` in a SHA256SUMS file ("<64 hex>  <name>" or "<hex> *<name>" lines); empty when absent.
std::string FindChecksum(const std::string& sums, std::string_view name);

// The release asset for this OS: Skylauncher.exe, Skylauncher-macOS.zip, Skylauncher-x86_64.AppImage.
std::string LauncherAssetName();
// The kit's: ssa-kit-windows-x64.zip, ssa-kit-macos-arm64.zip, ssa-kit-linux-x64.zip.
std::string KitAssetName();

// ---- Pieces (synchronous, tested on their own) ------------------------------------------------------------------------

// Where the latest release of "owner/name" is told; SKYLAUNCHER_UPDATE_URL replaces it (tests, mirrors).
std::string LatestReleaseUrl(const std::string& repo);

struct HttpResult
{
    bool ok = false;
    std::string error;
};
// Downloads `url` into `file` with curl. `progress` gets the bytes so far (about every 150 ms); setting `cancel` stops it.
HttpResult HttpDownload(const std::string& url, const std::filesystem::path& file, const std::function<void(uint64_t)>& progress = {},
    const std::atomic<bool>* cancel = nullptr);

// Downloads `asset` of `release` into `folder`, then checks its SHA-256 against the release's SHA256SUMS asset. A release
// without SHA256SUMS, or with another hash, is refused (the file is deleted). Returns the file ("" and `error` otherwise).
std::filesystem::path DownloadVerified(const ReleaseInfo& release, std::string_view assetName, const std::filesystem::path& folder,
    const std::function<void(uint64_t)>& progress, const std::atomic<bool>* cancel, std::string& error);

// target -> target.old, fresh -> target; if the second move fails the first is undone. False and `error` on failure, with
// `target` as it was. The .old stays (a running exe can't delete itself): CleanupOldUpdate removes it at the next start.
bool ReplaceInPlace(const std::filesystem::path& target, const std::filesystem::path& fresh, std::string& error);
void CleanupOldUpdate(const std::filesystem::path& target);

// Unzips into `folder` (made if needed), refusing names that leave it. Unix file modes are kept.
bool ExtractZip(const std::filesystem::path& zip, const std::filesystem::path& folder, std::string& error);

// What runs now and can be replaced: the exe (Windows), the .app (macOS, when its folder is writable), the AppImage
// (Linux, $APPIMAGE). Empty when none applies (a build run from a build folder): updates then open the release page.
std::filesystem::path LauncherSelf();

// Installs the kit zip over `kitFolder` (the folder holding SSABuild; created when there is none): extracted next to it,
// then swapped in with ReplaceInPlace, so a failure leaves the old kit.
bool InstallKitZip(const std::filesystem::path& zip, const std::filesystem::path& kitFolder, std::string& error);

// ---- The manager the window polls -------------------------------------------------------------------------------------

class UpdateManager
{
public:
    enum class Phase { Idle, Checking, Downloading, Verifying, Installing, Done, Failed };
    struct Settings
    {
        std::string repo = "mflr0/Skylauncher";
        std::string kitRepo;            // empty: no kit updates
        std::string kitInstalled;       // the installed kit's version or release tag
        std::filesystem::path kitFolder; // where the kit is, or goes
    };
    struct State
    {
        Phase phase = Phase::Idle;
        bool checked = false;           // a check has answered
        std::string checkError;         // why the last check failed
        bool launcherNewer = false;
        ReleaseInfo launcher;
        bool kitNewer = false;
        ReleaseInfo kit;
        std::string doing;              // "launcher" or "kit", what Downloading..Installing is for
        float progress = -1;            // 0..1 while downloading, -1 when unknown
        std::string error;              // why Failed
        bool restartNeeded = false;     // the launcher was replaced: restart to use it
        std::string note;               // e.g. "The installer was opened: drag Skylauncher to Applications."
    };

    ~UpdateManager() { Cancel(); Join(); }
    void Check(const Settings& settings);      // the latest launcher release (and the kit's), in the background
    void UpdateLauncher();                     // download, verify, replace; needs a Check that found one
    void UpdateKit();
    void Cancel() { m_cancel = true; }
    void Dismiss();                            // clears a failure or a note

    State Snapshot();
    bool Busy() const { return m_busy; }

private:
    void Join();
    void SetPhase(Phase phase, const char* doing = "");
    void Fail(const std::string& message);

    std::mutex m_mutex;
    State m_state;
    Settings m_settings;
    std::thread m_thread;
    std::atomic<bool> m_busy{ false }, m_cancel{ false };
};
