#pragma once

// The launcher window's internals, shared by its page files: launcher_ui.cpp (window, sidebar, Play, Settings, widgets)
// and collection_page.cpp (the figure collection). Nothing outside launcher/ui includes this; launcher_ui.h is the
// public entry point.
#include "assets/launcher_assets.h"
#include "launcher/game_copy.h"
#include "launcher/game_install.h"
#include "launcher/game_run.h"
#include "launcher/game_settings.h"
#include "launcher/i18n.h"
#include "launcher/updater.h"
#include "launcher/launcher.h"
#include "launcher/ui/launcher_ui.h"
#include "launcher/portal_status.h"
#include "launcher/uninstall.h"
#include "launcher/theme/launcher_theme.h"
#include "launcher/ui/collection_page.h"
#include "launcher/ui/hold_confirm.h"
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
        { "a", N_("A (jump, confirm)"), SDLK_SPACE }, { "b", N_("B (back)"), SDLK_X }, { "x", N_("X (attack)"), SDLK_C },
        { "y", N_("Y (attack)"), SDLK_V }, { "start", N_("Start"), SDLK_RETURN }, { "back", N_("Back"), SDLK_BACKSPACE },
        { "lb", N_("Left bumper"), SDLK_Q }, { "rb", N_("Right bumper"), SDLK_E }, { "lt", N_("Left trigger"), SDLK_1 },
        { "rt", N_("Right trigger"), SDLK_3 }, { "left_up", N_("Move up"), SDLK_W }, { "left_down", N_("Move down"), SDLK_S },
        { "left_left", N_("Move left"), SDLK_A }, { "left_right", N_("Move right"), SDLK_D }, { "right_up", N_("Camera up"), SDLK_I },
        { "right_down", N_("Camera down"), SDLK_K }, { "right_left", N_("Camera left"), SDLK_J },
        { "right_right", N_("Camera right"), SDLK_L }, { "dpad_up", N_("D-pad up"), SDLK_UP }, { "dpad_down", N_("D-pad down"), SDLK_DOWN },
        { "dpad_left", N_("D-pad left"), SDLK_LEFT }, { "dpad_right", N_("D-pad right"), SDLK_RIGHT },
    };

    struct Choice
    {
        const char* value;
        const char* label;
        const char* aliases = nullptr; // other values of the settings file that show as this choice (space separated)
    };

    // The runtime runs at 30 or 60: any other limit it is given means 60 (docs/settings.md).
    inline constexpr Choice FpsChoices[] = { { "30", N_("30 (original)") }, { "60", N_("60 (experimental)"), "0 120 144 240" } };
    inline constexpr Choice AspectChoices[] = { { "auto", N_("Auto (follows the window)") }, { "16:9", N_("16:9 (with bars)") } };
    inline constexpr Choice ScaleChoices[] = { { "auto", N_("Auto") }, { "1", "x1" }, { "2", "x2" }, { "3", "x3" }, { "4", "x4" },
        { "5", "x5" }, { "6", "x6" }, { "7", "x7" }, { "8", "x8" }, { "9", "x9" }, { "10", "x10" } };
    inline constexpr Choice GlyphChoices[] = { { "auto", N_("Automatic") }, { "xbox360", "Xbox 360" },
        { "xboxone", N_("Xbox One / Series"), "xbox" }, { "playstation", "PlayStation" }, { "nintendo", "Nintendo" }, { "keyboard", N_("Keyboard") } };
    inline constexpr Choice FilterChoices[] = { { "1", N_("Trilinear") }, { "2", N_("Anisotropic 2x") }, { "4", N_("Anisotropic 4x") },
        { "8", N_("Anisotropic 8x") }, { "16", N_("Anisotropic 16x") } };
    inline constexpr Choice PortalChoices[] = { { "auto", N_("Auto: a real portal if plugged in, else virtual") }, { "emulated", N_("Virtual (no portal needed)") },
        { "real", N_("Real portal (Wii, PlayStation, Xbox)"), "hid usb" } };
    inline constexpr Choice LanguageChoices[] = { { "en", "English" }, { "fr", "Français" }, { "de", "Deutsch" }, { "es", "Español" },
        { "it", "Italiano" }, { "nl", "Nederlands" }, { "sv", "Svenska" }, { "no", "Norsk" }, { "da", "Dansk" }, { "fi", "Suomi" } };
    // Giants' disc has twelve text and voice languages: Spanish twice (Spain, Latin America) and Portuguese. Its voices follow the
    // text unless one is chosen (an empty voice_language).
    inline constexpr Choice GiantsLanguageChoices[] = { { "en", "English" }, { "fr", "Français" }, { "de", "Deutsch" }, { "es", "Español" },
        { "es-419", "Español (Latinoamérica)" }, { "it", "Italiano" }, { "nl", "Nederlands" }, { "pt", "Português" }, { "sv", "Svenska" },
        { "no", "Norsk" }, { "da", "Dansk" }, { "fi", "Suomi" } };
    // Swap Force's disc has English and French.
    inline constexpr Choice SwapForceLanguageChoices[] = { { "en", "English" }, { "fr", "Français" } };
    inline constexpr Choice GiantsVoiceChoices[] = { { "", N_("Same as text") }, { "en", "English" }, { "fr", "Français" }, { "de", "Deutsch" },
        { "es", "Español" }, { "es-419", "Español (Latinoamérica)" }, { "it", "Italiano" }, { "nl", "Nederlands" }, { "pt", "Português" },
        { "sv", "Svenska" }, { "no", "Norsk" }, { "da", "Dansk" }, { "fi", "Suomi" } };

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
        LauncherAssets::Provider assets; // the disc's art and figure pictures, cached here too (4 s on the UI thread before)

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
    // figures (its sidebar row), the gear opens the launcher's Settings, and the game's Settings button (on its Play
    // page) opens that game's own settings.
    enum class Page { Home, Play, Collection, Settings, GameSettings };
    enum class SettingsTab { Controls, Launcher, Support, About };
    enum class GameTab { Video, Language, Disc, Saves, Game };
    enum class DiscState { None, Verifying, Ready, Failed };
    // Everything a game can be in, read from its slot by App::SlotStatus: the one place that decides (docs/launcher_ui.md).
    enum class SlotState
    {
        Closed, NoDisc, Checking, DiscGone, DiscUnreadable, DiscWrong, DiscUnknown, DiscNotGame, DataDamaged,
        DownloadingTools, Installing, Updating, Finishing, Failed, Interrupted, ReadyToInstall, UpdateAvailable, Ready
    };
    // Where the keyboard and pad focus goes on the next frame: the sidebar's current row, or the page's first control.
    enum class FocusTo { None, Sidebar, Page };

    class App;

    // A picture from the disc and the part of it that is drawn (a logo's opaque bounds: the disc's logos have wide empty
    // margins), possibly made smaller once on the CPU: the renderer has no mipmaps, so art drawn much smaller than its
    // size would shimmer.
    struct ArtTexture
    {
        SDL_Texture* texture = nullptr;
        ImVec2 uv0{ 0, 0 }, uv1{ 1, 1 };
        float aspect = 2; // width / height of that part

        explicit operator bool() const { return texture != nullptr; }
        void Destroy() { if (texture != nullptr) SDL_DestroyTexture(texture); texture = nullptr; }
    };
    // Loads a PNG; `crop`: to its opaque bounds; `maxWidth` > 0: averaged down to at most that many pixels wide. Empty if
    // the file can't be read.
    ArtTexture LoadArt(SDL_Renderer* renderer, const std::string& path, bool crop, int maxWidth = 0);
    // A rounded rectangle filled with a vertical gradient, alpha included (`flags`: which corners are rounded).
    void GradientRect(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom, float rounding, ImDrawFlags flags = 0);
    // Whether the system asks for less motion (macOS: Reduce motion; Windows: animations off). [launcher] reduce_motion
    // = 1 / 0 overrides it.
    bool SystemReducesMotion();

    // One game's state in the window: its disc and install, its art, its saves' backups, its collection page. Each game
    // the player opens keeps one for the session, so going back to it is instant; its jobs go on in the background.
    struct GameSlot
    {
        GameSlot(const GameInfo& info, App& app) : game(info), collection(std::make_unique<CollectionPage>(app)) {}
        const GameInfo& game;
        bool started = false; // its remembered disc has been sent to the check
        std::string chosen;
        std::string chosenRuntime; // the game built for the chosen disc, if any
        DiscState disc = DiscState::None;
        std::string discPath;
        uint64_t discSize = 0;
        VerifyJob verify;
        // "Copy files" (game_copy.h): what the game's copy is, whether discPath is that copy (the game plays from
        // it, the disc image is not needed), and the copy being made.
        gamedata::Status copy;
        bool fromCopy = false;
        GameCopyJob copyJob;
        std::string copyMessage; // why the last copy didn't finish
        GameSourceCheck check;
        TitleMetadata meta;
        LauncherAssets::Provider assets;
        SDL_Texture* icon = nullptr;
        ArtTexture logo;                   // the game's logo, cropped, from the player's disc once verified (game page)
        // Install state for the verified disc.
        std::optional<InstallKit> kit;
        InstalledGame installed;
        std::string ownRuntime;    // a runtime the player set up (FindRuntime): no install needed
        GameInstaller installer;
        KitDownloader kitDownload;  // the first step of an install when there is no kit yet
        std::string kitError;       // why the downloaded kit can't be used
        bool kitUsed = false;       // this install started with that download
        std::string kitTag;         // the release whose kit that download fetches
        bool skipUpdate = false;    // "Play without updating", until the launcher is closed
        InstallProgress::Step kitStep;
        std::string installSha;    // the disc being installed
        std::string installKitVersion;
        bool installView = false;  // the progress card replaces the logo
        bool installLog = false;   // "Show details"
        InstallProgress installPast; // an install of an earlier launcher session, read back from its install.log
        uint32_t installPastPid = 0; // its SSABuild, if that one may still be running
        uint64_t installPastCheck = 0; // when to look again whether it still runs

        // Game settings > Saves: the saves' backups (save_backup.h), read when the tab opens and after a restore.
        std::vector<savebackup::Entry> saveBackups;
        bool saveBackupsLoaded = false;
        // Game settings > Game: what delete and uninstall would free, read when the tab opens and after an action.
        GameDataSizes dataSizes;
        bool dataSizesLoaded = false;

        // The collection page and its state.
        std::unique_ptr<CollectionPage> collection;
    };

    class App
    {
    public:
        App(const GameInfo& game, const std::string& configPath, LauncherConfig& config, const LauncherOptions& options = {});
        ~App() { Shutdown(); }
        std::string Run();
        const GameInfo& ChosenGame() const { return m_s->game; } // the game that was shown when Run returned
        const std::string& ChosenRuntime() const { return m_s->chosenRuntime; }
        int TestFailures() const { return m_testFailures; } // SSA_LAUNCHER_TEST_SWITCH: the checks that failed
        // What a game is doing, in a few words, for the sidebar and Home: the same state the game page draws, read from
        // the game's own slot (nullptr: not opened this session). `progress` is 0..1 while it installs or updates.
        struct SlotLine
        {
            std::string text;
            unsigned dot;
            float progress = -1;
            SlotState state = SlotState::Closed;
            const char* button = "";   // the game page's main button: its label
            bool buttonEnabled = true; // and whether it can be pressed
        };
        SlotLine SlotStatus(GameSlot* slot);
        void DrawPortalStatus(); // "Portal: <model>" or "Portal: not detected", with its tooltip; nothing when HID is unavailable
        // The width of a PillButton: `width`, or wider when the label (a translation may be longer) needs it; `chevron` != 0
        // makes room for the drawn chevron.
        float PillWidth(const char* label, float width, bool bold = false, int chevron = 0);

    private:
        bool Init();
        void Shutdown();
        bool LoadFonts();
        void HandleEvent(const SDL_Event& event);
        int IdleWait(); // how long the loop may sleep before the next frame, in ms (0: draw now)
        void Frame();
        void DrawBackdrop();
        void DrawSidebar();
        void DrawHome();
        void DrawPlay();
        void DrawSettings();     // the launcher's settings (the gear): Controls, Launcher, Support, About
        void DrawControls();
        void DrawLauncherTab();
        void DrawLauncherLanguage(); // Settings > Launcher > Language
        void UpdateLanguage();       // each frame: the text follows [launcher] language, or the game's language
        void DrawSupport();      // support_page.cpp: logs and the problem report
        // A game's own settings (game_settings_page.cpp), opened from the Settings button on its Play page.
        void DrawGameSettings();
        void DrawGameVideo();
        void DrawGameLanguage();
        void DrawGameDisc();
        void DrawGameData();     // game_data_page.cpp: the Disc tab's "Game data" section
        void StartCopy();
        void FinishCopy();
        void RemoveCopy();
        void DrawSaveBackups();
        void RefreshSaveBackups();
        void RestoreSaveBackup(const std::string& stamp);
        void DrawGameTab();
        void DrawDataActions();
        void CreateShortcut();
        void ForgetDisc();       // "Remove the disc": the game forgets its image; the built game and the files stay
        void RefreshDataSizes();
        void RunDataAction(const DataResult& result, const char* done); // the toast, and the lists read again
        void RunUninstall(const DataResult& result);
        // "Uninstall everything" (uninstall_page.cpp), in Settings > Launcher: the list, the tick, the hold, the result.
        void DrawUninstallSection();
        void DrawUninstall();
        void OpenUninstall();
        void RunUninstallEverything();
        void ExportFiguresFirst(); // collection_manage.cpp: the existing export, of every figure
        void TestUninstallScript(uint64_t now);
        // The tabs of a settings page: words on one line, an accent line under the selected one; LB / RB change tab.
        // True when the tab changed (by a click or a bumper).
        bool DrawTabBar(const char* const* labels, int count, int* current);
        void DrawAbout();        // update_page.cpp: About
        void DrawUpdates();      // update_page.cpp: check for updates, and the automatic check; in Settings > Launcher
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
        void DrawQuitPrompt();

        // Pages and focus. The sidebar and the page are separate windows, which the D-pad doesn't cross: B / Escape
        // moves the focus to the sidebar (or leaves Settings and Collection), A on a row moves it into the page.
        void GoTo(Page page);
        void Back();
        void TakeFocus(FocusTo where); // after the item that takes the focus when `where` is asked for
        void RequestFocus(FocusTo where) { m_focusTo = where; m_focusShow = ImGui::GetIO().NavVisible; }

        // The collection (collection_page.h): its page draws with the widgets below.
        friend class CollectionPage;

        // The game page in the game's own look (game_look.cpp, docs/launcher_ui.md "Each game's look").
        // The band's action button: the main action (PLAY, INSTALL...) and the game's settings (a gear) as one split
        // button in the game's colours, PlayButtonW + PlayGearW by PlayButtonH at `at`. Takes the page's focus.
        struct MainAction
        {
            const char* label;
            bool enabled = true;
            float progress = -1; // 0..1 while busy (checking the disc, installing); < 0 otherwise
        };
        enum class MainActionHit { None, Main, Settings };
        MainActionHit DrawMainAction(ImVec2 at, const MainAction& action);
        // A logo inside `size` at `at` (centred, aspect kept), with a soft shadow; `alpha` 0..1.
        void DrawLogo(ImDrawList* draw, const ArtTexture& logo, ImVec2 at, ImVec2 size, float alpha = 1, bool shadow = true);
        // The logo's arrival on the game page: 0 when it starts falling, 1 once landed (always 1 with reduced motion).
        float LogoDrop();
        // The game page's logo inside `size` at `at`, falling in on arrival (LogoDrop), with its shadow.
        void DrawHeroLogo(ImDrawList* draw, const ArtTexture& logo, ImVec2 at, ImVec2 size);
        // A game's cached picture (its logo, cropped; its background), at most `maxWidth` pixels wide, loaded once; nullptr
        // before the game's disc was ever read. The sidebar and Home use them for games not opened this session too.
        const ArtTexture* CachedArt(const char* game, LauncherAssets::Role role, int maxWidth);
        void ForgetCachedArt(const char* game); // its disc was read again
        bool ReduceMotion() const;

        // Widgets in the launcher's style.
        bool BigButton(const char* label, ImVec2 size, unsigned color, unsigned dark, unsigned ink, bool enabled = true);
        bool PillButton(const char* label, bool primary = false, float width = 0, bool bold = false, unsigned fill = PanelRaised,
            unsigned fillHover = PanelRaisedHover, unsigned fillActive = PanelRaisedActive, int chevron = 0);
        // A chevron drawn with two lines (no font glyph): down when `dir` > 0, up when < 0; `size` is its width.
        static void DrawChevron(ImDrawList* draw, ImVec2 centre, float size, unsigned color, int dir);
        // Draws the label of the last item (a button whose own text was made transparent) with a chevron after it.
        void DrawLabelChevron(const char* label, unsigned color, int dir, bool bold);
        void BoldText(const char* text, unsigned color);
        bool Toggle(const char* id, bool* value);
        // Acts once held for a second (HoldConfirm); `hint` is its tooltip, which says so. Red: for what deletes or overwrites.
        bool HoldButton(const char* label, float width, const char* hint);
        // The keyboard and pad focus around the last item, for the widgets drawn over an InvisibleButton; `gap` < 0 draws
        // it inside (rows that touch the window's edges).
        void FocusRing(float rounding, float gap = 4, ImDrawFlags flags = 0);
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
        bool NeedsInstall(const GameSlot& slot) const;
        bool Installing() const { return m_s->installer.Running() || m_s->kitDownload.Running(); }
        void StartInstaller();
        void StartKitDownload();
        bool KitNeedsFetch(const GameSlot& slot) const; // no kit, one from an older release, or one that can't build the game
        bool UpdateAvailable() const;                   // the release has a newer version of this game than the one built (docs/updates.md)
        bool UpdateAvailable(const GameSlot& slot) const;
        bool AnyGameUpdate() const;
        const GameUpdate* PendingUpdate(const GameSlot& slot) const; // the game's entry in the release's games.json, if any
        std::string WantedKitTag(const GameSlot& slot) const;        // the release whose kit INSTALL / UPDATE must be using
        std::string GameVersion(const GameSlot& slot) const;         // "0.8.2": the built game's, else the kit's; "" when unknown
        void StartInstall();
        void PollInstall();
        void DrawInstall(ImVec2 at, ImVec2 size);
        InstallProgress InstallSnapshot(size_t logTail = 0); // this session's install, else the one read back from its log
        InstallProgress InstallSnapshot(GameSlot& slot, size_t logTail = 0); // the same, for any game's slot
        void SetInstallState(const std::string& sha, const char* state); // [install.<sha>] state=, for a launcher restart

        void UpdateUiScale(bool resize);
        GameSlot& SlotFor(const GameInfo& game);
        void SelectGame(const GameInfo& game); // the sidebar's switch: the window stays, its content is that game's
        void PollSlots();
        bool AnyInstalling() const;
        bool AnyVerifying() const;
        void StartVerify(const std::string& path);
        void StartGameDisc(); // the remembered disc image, or the game's copy when it is complete (game_data_page.cpp)
        void FinishVerify();
        void UseDiscForOtherGame(); // the disc check found the other supported game's disc: switch to it and set the disc
        void Browse();
        bool DiscStillThere(); // false, and the disc's line says so, when the verified image is gone
        void Set(const char* section, const char* key, const std::string& value);
        void Toast(std::string text, bool error = false); // an error stays until it is closed; the rest go by themselves
        void RefreshGamepads();
        std::vector<std::pair<std::string, std::string>> DeviceOptions() const; // value, label
        std::string CurrentDevice() const;
        std::string DeviceLabel() const;
        void SetDevice(const std::string& value);
        void Screenshot(const char* name);
        void TestScript();
        void TestInstallScript(uint64_t now);
        void TestUpdateScript(uint64_t now);
        void TestNavScript(uint64_t now);
        void TestSwitchScript(uint64_t now);
        void TestCopyScript(uint64_t now);
        void TestSidebarScript(uint64_t now);
        void TestKitScript(uint64_t now);
        void TestUpdatesScript(uint64_t now);
        void TestSeatXScript(uint64_t now);
        void TestButtonsScript(uint64_t now);
        void PlayNow(); // PLAY on the shown game: its disc and built game are what the launcher returns
        void AutoPlay(); // --play: PLAY as soon as the shown game's disc is checked, if it can

        std::string m_configPath;
        LauncherConfig& m_config;
        bool m_running = true;
        bool m_quitPrompt = false; // a quit asked during an install: confirm it
        int m_busyFrames = 0; // frames still drawn after an input, while ImGui settles (hover, focus, menus)

        SDL_Window* m_window = nullptr;
        float m_uiScale = 1.0f; // launcher points per window unit (UpdateUiScale)
        bool m_uiScaleLogged = false;
        SDL_Renderer* m_renderer = nullptr;
        std::vector<uint8_t> m_fontData[3];
        ImFont* m_textFont = nullptr;
        ImFont* m_titleFont = nullptr;
        ImFont* m_monoFont = nullptr;
        ImFont* m_boldFont = nullptr; // Outfit SemiBold

        Page m_page = Page::Home; // the launcher opens on its menu; the player picks a game in the sidebar
        Page m_backPage = Page::Home; // where Settings and Collection go back to
        FocusTo m_focusTo = FocusTo::Sidebar;
        bool m_focusShow = false;    // the focus request came from the keyboard or the pad: show the ring
        bool m_backTaken = false;    // last frame, something else answered B / Escape (a menu, a dialog, a field)
        SettingsTab m_tab = SettingsTab::Controls;
        GameTab m_gameTab = GameTab::Video;
        bool m_autoPlay = false; // --play: start the game as soon as it is ready (a desktop shortcut)
        ProbeJob m_probe;
        launcher_portal::Watcher m_portal;
        bool m_reportOpen = false;      // "Report a problem" shown (support_page.cpp)
        std::string m_reportText;

        // Updates (update_page.cpp).
        UpdateManager m_updates;
        bool m_updateManual = false;     // the running check was asked for: say the answer, even "up to date"
        bool m_updateLater = false;      // the player closed the banner for this session
        bool m_testPlayWithout = false;        // tests: press "Play without updating" on the next frame
        std::vector<GameUpdate> m_gameUpdates; // the games.json of the last answered check
        bool m_whatsNewOpen = false;
        bool m_updateWasBusy = false;
        std::string m_updateDoneNote;    // the last answer of a manual check, shown in About
        float m_bannerH = 0;             // the height the banner takes at the top of the page, this frame
        SDL_Texture* m_github = nullptr;
        bool m_dragOver = false;

        std::vector<SDL_JoystickID> m_gamepads;
        const KeyAction* m_capturing = nullptr;

        // The games opened this session (one slot each), and the one shown. Every slot's jobs are polled; the shown
        // one's state is what the pages draw (m_s).
        std::vector<std::unique_ptr<GameSlot>> m_slots;
        GameSlot* m_s = nullptr;

        HoldConfirm m_hold;

        enum class UninstallStage { Closed, List, Result };
        struct UninstallState
        {
            UninstallStage stage = UninstallStage::Closed;
            uninstall::Environment env;
            uninstall::Plan plan;
            bool tick = false; // "Also delete my saves and my figures"
            uninstall::Result result;
            std::vector<uninstall::Left> notices; // what stays, as the plan knew it, for the result message
            bool launcherScheduled = false;
            uint32_t holdId = 0; // the hold button's ImGui ID, for the screenshot test
        } m_un;
        bool m_uninstalled = false;    // done: the window only shows the result and quits
        bool m_testUninstall = false;  // SSA_LAUNCHER_TEST_UNINSTALL: the uninstall screens
        bool m_testScrollEnd = false;  // the uninstall screenshots: scroll the settings column to its end
        uint64_t m_testHoldAt = 0;     // when the test pressed the hold button (it holds it down for the screenshots)

        // The game page's logo drop: when it began, and what was shown last frame (a new arrival starts it again).
        uint64_t m_logoDropAt = 0;
        const GameSlot* m_logoDropSlot = nullptr;
        bool m_logoDropHad = false; // the logo was there (it may arrive after the page, once the disc is read)
        Page m_lastPage = Page::Home;
        std::map<std::string, ArtTexture> m_cachedArt; // CachedArt's, by "<game> <role> <width>"

        struct ToastItem
        {
            std::string text;
            uint64_t until; // 0: an error, kept until closed
        };
        std::vector<ToastItem> m_toasts; // newest last; drawn from the top right, down
        std::vector<ImVec4> m_stars; // x, y (0..1), radius, phase

        // Screenshot test (SSA_LAUNCHER_SHOTS, SSA_LAUNCHER_TEST_DROP): drives the screens without a player.
        std::string m_shots, m_testDrop;
        bool m_testUpdate = false;  // SSA_LAUNCHER_TEST_UPDATE: the update screens (SKYLAUNCHER_UPDATE_URL names a fake release)
        bool m_testReal = false;    // SSA_LAUNCHER_TEST_INSTALL=real: the real kit, a screenshot every SSA_LAUNCHER_TEST_EVERY seconds
        bool m_testInstall = false; // SSA_LAUNCHER_TEST_INSTALL: the install screens instead
        bool m_testCollection = false; // SSA_LAUNCHER_TEST_COLLECTION: the collection's screens instead
        bool m_testSeatX = false;      // SSA_LAUNCHER_TEST_SEATX: the X on every seat, by mouse, keyboard and pad
        std::vector<std::string> m_testSeats; // the seats the SEATX script restores before each try
        std::string m_testBuilt;                    // the sidebar test's built game, put back between the states it sets by hand
        std::map<std::string, std::string> m_drawn; // while testing: what the sidebar, Home and the main button drew ("sidebar:ssa", "home:ssa", "button")
        bool m_testSidebar = false;    // SSA_LAUNCHER_TEST_SIDEBAR: the sidebar's line through an install and an update (fake kit)
        bool m_testCopy = false;       // SSA_LAUNCHER_TEST_COPY: copying the game data, playing from it, the wrong-disc button
        std::string m_testKit;         // SSA_LAUNCHER_TEST_KIT: "old", "giants", "updates" or "offline": the kit that follows the launcher, the game updates (fake release)
        bool m_testSwitch = false;     // SSA_LAUNCHER_TEST_SWITCH: both games' slots, a switch back and forth, PLAY on each
        int m_testFailures = 0;
        uint64_t m_testBegan = 0;
        std::string m_testSha, m_testPath, m_testRuntime[2];
        bool m_testNav = false;        // SSA_LAUNCHER_TEST_NAV: keyboard and pad navigation, keys sent to ImGui
        bool m_testControllers = false; // open the controller menu (its screenshot)
        bool m_testButtons = false;     // SSA_LAUNCHER_TEST_BUTTONS: the action button's states, both games
        bool m_testButtonsOff = false;  // its "disabled" shot: the main button drawn disabled
        std::string m_testShot;         // the next shot's name, for scripts that name their own
        float m_testUiScale = 0;        // > 0: the UI scale, whatever the display (the button test)
        ImVec2 m_testMainAt, m_testGearAt; // the action button's two halves' centres, last drawn (where the script points)
        int m_testStep = 0;
        uint64_t m_testAt = 0;
    };

    template<size_t N>
    bool App::Combo(const char* label, const char* section, const char* key, const Choice (&choices)[N], int fallback, float width)
    {
        int index = ChoiceIndex(choices, SettingGet(m_config, section, key), fallback);
        bool changed = false;
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo(label, Tr(choices[index].label)))
        {
            for (size_t i = 0; i < N; i++)
                if (ImGui::Selectable(Tr(choices[i].label), int(i) == index))
                {
                    Set(section, key, choices[i].value);
                    changed = true;
                }
            ImGui::EndCombo();
        }
        return changed;
    }
}
