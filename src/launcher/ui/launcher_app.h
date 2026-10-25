#pragma once

// The launcher window's internals, shared by its page files: launcher_ui.cpp (window, sidebar, Play, Settings, widgets)
// and collection_page.cpp (the figure collection). Nothing outside launcher/ui includes this; launcher_ui.h is the
// public entry point.
#include "assets/launcher_assets.h"
#include "launcher/game_install.h"
#include "launcher/game_run.h"
#include "launcher/updater.h"
#include "launcher/launcher.h"
#include "launcher/portal_status.h"
#include "launcher/theme/launcher_theme.h"
#include "launcher/ui/collection_page.h"
#include "shared/collection/save_backup.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <filesystem>
#include <imgui.h>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace launcher_ui
{
    using namespace LauncherTheme;

    // ---- Settings file --------------------------------------------------------------------------------------------

    std::string SettingGet(const LauncherConfig& config, std::string_view section, std::string_view key, std::string fallback = {});
    int SettingInt(const LauncherConfig& config, std::string_view section, std::string_view key, int fallback);
    bool ParseBool(std::string_view value);

    // Keyboard bindings: the runtime reads "[input] key_<action>=<SDL key name>".
    struct KeyAction
    {
        const char* id;
        const char* label;
        SDL_Keycode fallback;
    };

    inline constexpr KeyAction KeyActions[] = {
        { "a", "A (jump, confirm)", SDLK_SPACE }, { "b", "B (back)", SDLK_X }, { "x", "X (attack)", SDLK_C },
        { "y", "Y (attack)", SDLK_V }, { "start", "Start", SDLK_RETURN }, { "back", "Back", SDLK_BACKSPACE },
        { "lb", "Left bumper", SDLK_Q }, { "rb", "Right bumper", SDLK_E }, { "lt", "Left trigger", SDLK_1 },
        { "rt", "Right trigger", SDLK_3 }, { "left_up", "Move up", SDLK_W }, { "left_down", "Move down", SDLK_S },
        { "left_left", "Move left", SDLK_A }, { "left_right", "Move right", SDLK_D }, { "right_up", "Camera up", SDLK_I },
        { "right_down", "Camera down", SDLK_K }, { "right_left", "Camera left", SDLK_J },
        { "right_right", "Camera right", SDLK_L }, { "dpad_up", "D-pad up", SDLK_UP }, { "dpad_down", "D-pad down", SDLK_DOWN },
        { "dpad_left", "D-pad left", SDLK_LEFT }, { "dpad_right", "D-pad right", SDLK_RIGHT },
    };

    struct Choice
    {
        const char* value;
        const char* label;
        const char* aliases = nullptr; // other values of the settings file that show as this choice (space separated)
    };

    // The runtime runs at 30 or 60: any other limit it is given means 60 (docs/settings.md).
    inline constexpr Choice FpsChoices[] = { { "30", "30 (original)" }, { "60", "60 (experimental)", "0 120 144 240" } };
    inline constexpr Choice AspectChoices[] = { { "auto", "Auto (follows the window)" }, { "16:9", "16:9 (with bars)" } };
    inline constexpr Choice ScaleChoices[] = { { "auto", "Auto" }, { "1", "x1" }, { "2", "x2" }, { "3", "x3" }, { "4", "x4" },
        { "5", "x5" }, { "6", "x6" }, { "7", "x7" }, { "8", "x8" }, { "9", "x9" }, { "10", "x10" } };
    inline constexpr Choice GlyphChoices[] = { { "auto", "Automatic (follows what you play with)" }, { "xbox360", "Xbox 360" },
        { "xboxone", "Xbox One / Series", "xbox" }, { "playstation", "PlayStation" }, { "nintendo", "Nintendo" }, { "keyboard", "Keyboard" } };
    inline constexpr Choice FilterChoices[] = { { "1", "Trilinear" }, { "2", "Anisotropic 2x" }, { "4", "Anisotropic 4x" },
        { "8", "Anisotropic 8x" }, { "16", "Anisotropic 16x" } };
    inline constexpr Choice PortalChoices[] = { { "auto", "Auto: a real portal if plugged in, else virtual" }, { "emulated", "Virtual (no portal needed)" },
        { "real", "Real portal (Wii, PlayStation, Xbox)", "hid usb" } };
    inline constexpr Choice LanguageChoices[] = { { "en", "English" }, { "fr", "Français" }, { "de", "Deutsch" }, { "es", "Español" },
        { "it", "Italiano" }, { "nl", "Nederlands" }, { "sv", "Svenska" }, { "no", "Norsk" }, { "da", "Dansk" }, { "fi", "Suomi" } };

    template<size_t N>
    int ChoiceIndex(const Choice (&choices)[N], const std::string& value, int fallback)
    {
        for (size_t i = 0; i < N; i++)
            if (value == choices[i].value)
                return int(i);
        for (size_t i = 0; i < N; i++)
            if (choices[i].aliases != nullptr && (" " + std::string(choices[i].aliases) + " ").find(" " + value + " ") != std::string::npos)
                return int(i);
        return fallback;
    }

    // ---- Files and images -----------------------------------------------------------------------------------------

    bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out);
    SDL_Texture* LoadPng(SDL_Renderer* renderer, std::span<const uint8_t> png);
    SDL_Texture* LoadPngFile(SDL_Renderer* renderer, const std::string& path);
    SDL_Texture* LoadRgba(SDL_Renderer* renderer, const std::vector<uint8_t>& rgba, int width, int height);
    ImTextureRef Ref(SDL_Texture* texture);
    // Opens a folder in Explorer / the Finder / the file manager (a path, not a URL: spaces and accents are fine).
    bool OpenFolder(const std::filesystem::path& folder);

    // ---- Drawing --------------------------------------------------------------------------------------------------

    void DrawGear(ImDrawList* draw, ImVec2 center, float radius, ImU32 ink, ImU32 hole);
    void DrawGamepad(ImDrawList* draw, ImVec2 center, float width, ImU32 ink, ImU32 hole);
    // `passes` extra copies, each BoldOffset further right: 1 is bold, more is heavier.
    void AddBoldText(ImDrawList* draw, ImVec2 at, ImU32 color, const char* text, int passes = 1);

    // ---- Disc verification on a worker thread ---------------------------------------------------------------------

    struct VerifyJob
    {
        std::thread thread;
        std::atomic<bool> done{ false };
        std::atomic<float> progress{ 0 };
        std::mutex mutex;
        std::string stage;
        GameSourceCheck check;
        TitleMetadata meta;

        ~VerifyJob() { Join(); }
        void Join() { if (thread.joinable()) thread.join(); }
        std::string Stage() { std::lock_guard lock(mutex); return stage; }
        void Start(const std::string& image, const GameInfo& game);
    };

    // "Test the portal": the runtime's --portal-probe, on a worker thread.
    struct ProbeJob
    {
        std::thread thread;
        std::atomic<bool> done{ true }, ran{ false }; // `ran`: a result is there to show
        PortalProbeResult result;
        ~ProbeJob() { Join(); }
        void Join() { if (thread.joinable()) thread.join(); }
        void Start(const std::string& runtime)
        {
            Join();
            done = false;
            ran = true;
            thread = std::thread([this, runtime] { result = RunPortalProbe(runtime); done = true; });
        }
    };

    // Home is the menu (the SKYLAUNCHER title opens it), Play is the game art and the main button, Collection is the
    // figures (its sidebar row), the gear opens Settings.
    enum class Page { Home, Play, Collection, Settings };
    enum class SettingsTab { Graphics, Controls, Language, GameData, About };
    enum class DiscState { None, Verifying, Ready, Failed };

    class App
    {
    public:
        App(const GameInfo& game, const std::string& configPath, LauncherConfig& config) : m_game(game), m_configPath(configPath), m_config(config) {}
        ~App() { Shutdown(); }
        std::string Run();
        const std::string& ChosenRuntime() const { return m_chosenRuntime; }
        void DrawPortalStatus(); // "Portal: <model>" or "Portal: not detected", with its tooltip; nothing when HID is unavailable

    private:
        bool Init();
        void Shutdown();
        bool LoadFonts();
        void HandleEvent(const SDL_Event& event);
        void Frame();
        void DrawBackdrop();
        void DrawSidebar();
        void DrawHome();
        void DrawPlay();
        void DrawSettings();
        void DrawGraphics();
        void DrawControls();
        void DrawLanguage();
        void DrawGameData();
        void DrawSaveBackups();
        void RefreshSaveBackups();
        void RestoreSaveBackup(const std::string& stamp);
        void DrawAbout();        // update_page.cpp: About, and the update banner
        void StartUpdateCheck(bool manual);
        void PollUpdates();
        float BannerHeight();
        void DrawUpdateBanner(float height);
        void DrawWhatsNew();
        void DrawPortalTest();   // support_page.cpp
        void DrawLogs();
        void OpenProblemReport();
        void DrawProblemReport();
        void DrawToast();
        void DrawLanguageMenu();

        // The collection (collection_page.h): its page draws with the widgets below.
        friend class CollectionPage;

        // Widgets in the launcher's style.
        bool BigButton(const char* label, ImVec2 size, unsigned color, unsigned dark, unsigned ink, bool enabled = true);
        bool PillButton(const char* label, bool primary = false, float width = 0, bool bold = false, unsigned fill = PanelRaised,
            unsigned fillHover = PanelRaisedHover, unsigned fillActive = PanelRaisedActive);
        void BoldText(const char* text, unsigned color);
        bool Toggle(const char* id, bool* value);
        template<size_t N> bool Combo(const char* label, const char* section, const char* key, const Choice (&choices)[N], int fallback, float width = -1);
        void SettingRow(const char* label, const char* hint);
        bool BeginSettingsTable(const char* id);
        void EndSettingsTable();
        void BeginCard(const char* id, ImVec2 size, ImGuiChildFlags flags = ImGuiChildFlags_None);
        void EndCard();
        void Title(const char* text, float size = TitleSize, unsigned color = Accent);
        // A rounded "chip" (filters, badges); returns true when clicked.
        bool Chip(const char* label, bool selected, unsigned color = Accent);
        void Badge(ImDrawList* draw, ImVec2 at, const char* text, unsigned fill, unsigned ink);

        // Installing the game (the band's INSTALL button, the progress card in place of the logo).
        void RefreshInstall();
        bool NeedsInstall() const;
        bool Installing() const { return m_installer.Running() || m_kitDownload.Running(); }
        void StartInstaller();
        void StartKitDownload();
        bool UpdateAvailable() const;
        void StartInstall();
        void PollInstall();
        void DrawInstall(ImVec2 at, ImVec2 size);
        InstallProgress InstallSnapshot(size_t logTail = 0); // this session's install, else the one read back from its log
        void SetInstallState(const std::string& sha, const char* state); // [install.<sha>] state=, for a launcher restart

        void UpdateUiScale(bool resize);
        void StartVerify(const std::string& path);
        void FinishVerify();
        void Browse();
        void Set(const char* section, const char* key, const std::string& value);
        void Toast(std::string text) { m_toast = std::move(text); m_toastUntil = SDL_GetTicks() + 2500; }
        void RefreshGamepads();
        std::vector<std::pair<std::string, std::string>> DeviceOptions() const; // value, label
        std::string CurrentDevice() const;
        std::string DeviceLabel() const;
        void SetDevice(const std::string& value);
        void Screenshot(const char* name);
        void TestScript();
        void TestInstallScript(uint64_t now);
        void TestUpdateScript(uint64_t now);

        const GameInfo& m_game;
        std::string m_configPath;
        LauncherConfig& m_config;
        std::string m_chosen;
        std::string m_chosenRuntime; // the game built for the chosen disc, if any
        bool m_running = true;

        SDL_Window* m_window = nullptr;
        float m_uiScale = 1.0f; // launcher points per window unit (UpdateUiScale)
        SDL_Renderer* m_renderer = nullptr;
        std::vector<uint8_t> m_fontData[3];
        ImFont* m_textFont = nullptr;
        ImFont* m_titleFont = nullptr;
        ImFont* m_monoFont = nullptr;
        ImFont* m_boldFont = nullptr; // Outfit SemiBold

        Page m_page = Page::Home; // the launcher opens on its menu; the player picks a game in the sidebar
        SettingsTab m_tab = SettingsTab::Graphics;
        DiscState m_disc = DiscState::None;
        std::string m_discPath;
        uint64_t m_discSize = 0;
        VerifyJob m_verify;
        ProbeJob m_probe;
        launcher_portal::Watcher m_portal;
        bool m_reportOpen = false;      // "Report a problem" shown (support_page.cpp)
        std::string m_reportText;

        // Settings > Game data: the saves' backups (save_backup.h), read when the tab opens and after a restore.
        std::vector<savebackup::Entry> m_saveBackups;
        bool m_saveBackupsLoaded = false;

        // Updates (update_page.cpp).
        UpdateManager m_updates;
        bool m_updateManual = false;     // the running check was asked for: say the answer, even "up to date"
        bool m_updateLater = false;      // the player closed the banner for this session
        bool m_kitLater = false;
        bool m_whatsNewOpen = false;
        bool m_updateWasBusy = false;
        std::string m_updateDoneNote;    // the last answer of a manual check, shown in About
        float m_bannerH = 0;             // the height the banner takes at the top of the page, this frame
        GameSourceCheck m_check;
        TitleMetadata m_meta;
        LauncherAssets::Provider m_assets;
        SDL_Texture* m_icon = nullptr;
        SDL_Texture* m_logo = nullptr;
        SDL_Texture* m_background = nullptr; // the window's backdrop, from the player's disc once verified
        SDL_Texture* m_github = nullptr;
        bool m_dragOver = false;

        // Install state for the verified disc.
        std::optional<InstallKit> m_kit;
        InstalledGame m_installed;
        std::string m_ownRuntime;    // a runtime the player set up (FindRuntime): no install needed
        GameInstaller m_installer;
        KitDownloader m_kitDownload;  // the first step of an install when there is no kit yet
        std::string m_kitError;       // why the downloaded kit can't be used
        bool m_kitUsed = false;       // this install started with that download
        InstallProgress::Step m_kitStep;
        std::string m_installSha;    // the disc being installed
        std::string m_installKitVersion;
        bool m_installView = false;  // the progress card replaces the logo
        bool m_installLog = false;   // "Show details"
        InstallProgress m_installPast; // an install of an earlier launcher session, read back from its install.log
        uint32_t m_installPastPid = 0; // its SSABuild, if that one may still be running

        std::vector<SDL_JoystickID> m_gamepads;
        const KeyAction* m_capturing = nullptr;

        // The collection page and its state.
        std::unique_ptr<CollectionPage> m_collection = std::make_unique<CollectionPage>(*this);

        std::string m_toast;
        uint64_t m_toastUntil = 0;
        std::vector<ImVec4> m_stars; // x, y (0..1), radius, phase

        // Screenshot test (SSA_LAUNCHER_SHOTS, SSA_LAUNCHER_TEST_DROP): drives the screens without a player.
        std::string m_shots, m_testDrop;
        bool m_testUpdate = false;  // SSA_LAUNCHER_TEST_UPDATE: the update screens (SKYLAUNCHER_UPDATE_URL names a fake release)
        bool m_testReal = false;    // SSA_LAUNCHER_TEST_INSTALL=real: the real kit, a screenshot every SSA_LAUNCHER_TEST_EVERY seconds
        bool m_testInstall = false; // SSA_LAUNCHER_TEST_INSTALL: the install screens instead
        bool m_testCollection = false; // SSA_LAUNCHER_TEST_COLLECTION: the collection's screens instead
        int m_testStep = 0;
        uint64_t m_testAt = 0;
    };
}
