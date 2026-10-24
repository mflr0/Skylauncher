#pragma once

// The launcher window's internals, shared by its page files: launcher_ui.cpp (window, sidebar, Play, Settings, widgets)
// and collection_page.cpp (the figure collection). Nothing outside launcher/ui includes this; launcher_ui.h is the
// public entry point.
#include "assets/launcher_assets.h"
#include "launcher/game_install.h"
#include "launcher/launcher.h"
#include "launcher/theme/launcher_theme.h"
#include "shared/figures/figure_collection.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <filesystem>
#include <imgui.h>
#include <map>
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
    };

    inline constexpr Choice FpsChoices[] = { { "30", "30 (original)" }, { "60", "60" }, { "120", "120" }, { "144", "144" },
        { "240", "240" }, { "0", "Unlimited" } };
    inline constexpr Choice FilterChoices[] = { { "1", "Trilinear" }, { "2", "Anisotropic 2x" }, { "4", "Anisotropic 4x" },
        { "8", "Anisotropic 8x" }, { "16", "Anisotropic 16x" } };
    inline constexpr Choice PortalChoices[] = { { "auto", "Real portal if plugged in, else virtual" }, { "emulated", "Virtual portal" },
        { "hid", "Wii / Wii U / PlayStation portal (HID)" }, { "usb", "Xbox 360 / Xbox One portal (USB)" } };
    inline constexpr Choice LanguageChoices[] = { { "en", "English" }, { "fr", "Français" }, { "de", "Deutsch" }, { "es", "Español" },
        { "it", "Italiano" }, { "nl", "Nederlands" }, { "sv", "Svenska" }, { "no", "Norsk" }, { "da", "Dansk" }, { "fi", "Suomi" } };

    template<size_t N>
    int ChoiceIndex(const Choice (&choices)[N], const std::string& value, int fallback)
    {
        for (size_t i = 0; i < N; i++)
            if (value == choices[i].value)
                return int(i);
        return fallback;
    }

    // ---- Files and images -----------------------------------------------------------------------------------------

    bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out);
    std::string PreferencePath(const char* sub); // in the settings folder shared with the runtime
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

    // ---- The collection -------------------------------------------------------------------------------------------

    // One figure file of the collection, read once and refreshed after every change.
    struct CollectionFigure
    {
        std::string file;                     // name in the figures folder
        figures::FigureHeader header;
        figures::FigureStats stats;
        const figures::ToyInfo* toy = nullptr; // null for a toy this launcher doesn't know
        bool locked = false;                  // a running game has it on its portal
        bool favourite = false;
        bool onPortal = false;                // starts on the portal ([portal] place)
    };

    // A figure's picture: the disc's portrait, or a generated icon (the page writes the initials over those).
    struct FigureIconTexture
    {
        SDL_Texture* texture = nullptr;
        bool generated = false;
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
        void DrawAbout();
        void DrawToast();
        void DrawLanguageMenu();

        // The collection (collection_page.cpp).
        void DrawCollection();
        void DrawCollectionFilters();
        void DrawPortalBar();
        bool DrawFigureCard(const CollectionFigure& figure, float size);
        void DrawFigureSheet();
        void DrawNewFigure();
        void DrawFigureIcon(ImDrawList* draw, const figures::ToyInfo* toy, ImVec2 at, float size, bool large);
        const FigureIconTexture& FigureIcon(const figures::ToyInfo* toy, bool large);
        void ClearFigureIcons();
        std::filesystem::path FiguresFolder() const;
        void LoadCollection();
        void RefreshFigure(const std::string& file);
        void RefreshLocks();
        void ImportFigures();
        void ImportFigureFile(const std::string& path);
        void SetFavourite(const std::string& file, bool on);
        std::vector<std::string> Favourites() const;
        CollectionFigure* FindFigure(const std::string& file);

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
        std::vector<std::string> StartupFigures() const;
        void SetStartupFigure(const std::string& file, bool on);
        void Screenshot(const char* name);
        void TestScript();
        void TestInstallScript(uint64_t now);

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
        std::string m_installSha;    // the disc being installed
        std::string m_installKitVersion;
        bool m_installView = false;  // the progress card replaces the logo
        bool m_installLog = false;   // "Show details"
        InstallProgress m_installPast; // an install of an earlier launcher session, read back from its install.log
        uint32_t m_installPastPid = 0; // its SSABuild, if that one may still be running

        std::vector<SDL_JoystickID> m_gamepads;
        const KeyAction* m_capturing = nullptr;

        // Collection state.
        std::vector<CollectionFigure> m_collection;
        bool m_collectionLoaded = false;
        std::map<std::string, FigureIconTexture> m_figureIcons; // by image name (and size)
        char m_search[64] = {};
        int m_categoryFilter = -1;   // figures::Category, -1: all
        int m_gameFilter = -1;       // figures::Game, -1: any
        bool m_favouritesOnly = false;
        std::string m_sheetFile;     // the figure whose sheet is open
        char m_renameBuffer[64] = {};
        std::string m_confirm;       // the sheet action waiting for confirmation ("reset", "delete", "restore:<backup>")
        bool m_newFigureOpen = false;
        int m_newCategory = int(figures::Category::Skylander);
        uint16_t m_newBase = 0xFFFF; // the toy picked in the creator, then its variants
        uint16_t m_newVariant = 0xFFFF; // the variant picked (a toy type); starts as the plain toy
        uint64_t m_locksCheckedAt = 0;

        int m_uiLanguage = 0; // index in LanguageChoices; the launcher isn't translated yet, so nothing reads it

        std::string m_toast;
        uint64_t m_toastUntil = 0;
        std::vector<ImVec4> m_stars; // x, y (0..1), radius, phase

        // Screenshot test (SSA_LAUNCHER_SHOTS, SSA_LAUNCHER_TEST_DROP): drives the screens without a player.
        std::string m_shots, m_testDrop;
        bool m_testInstall = false; // SSA_LAUNCHER_TEST_INSTALL: the install screens instead
        int m_testStep = 0;
        uint64_t m_testAt = 0;
    };
}
