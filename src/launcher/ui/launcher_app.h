#pragma once

// The launcher window's internals, shared by its page files: launcher_ui.cpp (window, sidebar, Play, Settings, widgets)
// and collection_page.cpp (the figure collection). Nothing outside launcher/ui includes this; launcher_ui.h is the
// public entry point.
#include "assets/launcher_assets.h"
#include "launcher/game_copy.h"
#include "launcher/game_install.h"
#include "launcher/game_progress.h"
#include "launcher/game_keys.h"
#include "launcher/game_run.h"
#include "launcher/game_settings.h"
#include "launcher/i18n.h"
#include "launcher/updater.h"
#include "launcher/launcher.h"
#include "launcher/ui/launcher_ui.h"
#include "launcher/portal_status.h"
#include "launcher/uninstall.h"
#include "launcher/ui/theme/launcher_theme.h"
#include "launcher/ui/collection_page.h"
#include "launcher/ui/components/components.h"
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

    struct Choice
    {
        const char* value;
        const char* label;
        const char* aliases = nullptr; // other values of the settings file that show as this choice (space separated)
    };

    // "display": the display's refresh rate, up to 60 for now (the runtime reads it as 60); above 60 the runtime paces the frames itself.
    inline constexpr Choice FpsChoices[] = { { "display", N_("Display refresh rate (up to 60)") }, { "30", N_("30 (original)") },
        { "60", N_("60 (experimental)") }, { "120", N_("120 (experimental)") }, { "144", N_("144 (experimental)") }, { "240", N_("240 (experimental)") },
        { "0", N_("Unlimited (experimental)") } };
    // The frame rates a game is offered: the list up to 60 when the game is capped there (MaxFrameRate).
    inline std::span<const Choice> FrameRateChoices(std::string_view gameId)
    {
        return MaxFrameRate(gameId) == 60 ? std::span<const Choice>(FpsChoices, 3) : std::span<const Choice>(FpsChoices);
    }
    inline constexpr Choice VolumeChoices[] = { { "0", "0 %" }, { "10", "10 %" }, { "20", "20 %" }, { "30", "30 %" }, { "40", "40 %" },
        { "50", "50 %" }, { "60", "60 %" }, { "70", "70 %" }, { "80", "80 %" }, { "90", "90 %" }, { "100", "100 %" } };
    inline constexpr Choice RumbleChoices[] = { { "off", N_("Off") }, { "low", N_("Low") }, { "normal", N_("Normal") }, { "strong", N_("Strong") } };

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
    // Swap Force's disc has English and French (USA, Australia) or Spanish and Italian (Europe).
    inline constexpr Choice SwapForceLanguageChoices[] = { { "en", "English" }, { "fr", "Français" }, { "es", "Español" }, { "it", "Italiano" } };
    inline constexpr Choice GiantsVoiceChoices[] = { { "", N_("Same as text") }, { "en", "English" }, { "fr", "Français" }, { "de", "Deutsch" },
        { "es", "Español" }, { "es-419", "Español (Latinoamérica)" }, { "it", "Italiano" }, { "nl", "Nederlands" }, { "pt", "Português" },
        { "sv", "Svenska" }, { "no", "Norsk" }, { "da", "Dansk" }, { "fi", "Suomi" } };

    inline int ChoiceIndex(std::span<const Choice> choices, const std::string& value, int fallback)
    {
        for (size_t i = 0; i < choices.size(); i++)
            if (value == choices[i].value)
                return int(i);
        for (size_t i = 0; i < choices.size(); i++)
            if (choices[i].aliases != nullptr && (" " + std::string(choices[i].aliases) + " ").find(" " + value + " ") != std::string::npos)
                return int(i);
        return fallback;
    }

    // The language choices a disc holds (OffersLanguage, launcher.h), in their order; every choice when the list is empty (not known).
    inline std::vector<Choice> ChoicesOnDisc(std::span<const Choice> choices, const std::vector<std::string>& onDisc)
    {
        std::vector<Choice> shown;
        for (const Choice& choice : choices)
            if (OffersLanguage(onDisc, choice.value))
                shown.push_back(choice);
        return shown;
    }

    // A game's text (`voice` false) or voice language choices: those its disc holds (stored at the last disc check), else today's full
    // list (a game checked before this was stored; Swap Force's was English and French). The choice shown when the saved one is not
    // among them is the first, the game's default.
    inline std::vector<Choice> GameLanguageChoices(const LauncherConfig& config, std::string_view game, bool voice)
    {
        art::DiscLanguages onDisc = StoredDiscLanguages(config, game);
        if (game == "swapforce" && onDisc.text.empty())
            onDisc.text = onDisc.voice = { "en", "fr" };
        std::span<const Choice> all = LanguageChoices;
        if (game == "swapforce")
            all = SwapForceLanguageChoices;
        else if (game == "giants")
            all = voice ? std::span<const Choice>(GiantsVoiceChoices) : std::span<const Choice>(GiantsLanguageChoices);
        return ChoicesOnDisc(all, voice ? onDisc.voice : onDisc.text);
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
    enum class Page { Home, Play, Collection, Settings, GameSettings, Achievements };
    enum class SettingsTab { Controls, Launcher, Support, About };
    enum class GameTab { Video, Keyboard, Controller, Language, Disc, Saves, Game };
    enum class DiscState { None, Verifying, Ready, Failed };
    // Everything a game can be in, read from its slot by App::SlotStatus: the one place that decides (docs/launcher_ui.md).
    enum class SlotState
    {
        NoDisc, Checking, DiscGone, DiscUnreadable, DiscWrong, DiscUnknown, DiscNotGame, DataDamaged,
        DownloadingTools, Installing, Updating, Finishing, Failed, Interrupted, ReadyToInstall, UpdateAvailable, Ready, Running
    };
    // The shape and colour that say a game's state: never the colour alone (the sidebar, Home's cards, the page). IconOf: the
    // state's own icon (a disc, an update, a download...), else the status's.
    ui::Status StatusOf(SlotState state);
    ui::Icon IconOf(SlotState state);
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
    using ui::GradientRect; // a rounded rectangle filled with a vertical gradient (components/core.h)
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
        bool discQueued = false; // it has a remembered disc, not checked yet (its line says so)
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
        int TestFailures() const { return m_testFailures; } // SKY_LAUNCHER_TEST_SWITCH: the checks that failed
        // What a game is doing, in a few words, for the sidebar and Home: the same state the game page draws, read from
        // the game's own slot. `progress` is 0..1 while it installs or updates.
        struct SlotLine
        {
            std::string text;
            unsigned dot;
            float progress = -1;
            SlotState state = SlotState::NoDisc;
            std::string button;        // the game page's main button: its label
            bool buttonEnabled = true; // and whether it can be pressed
        };
        SlotLine SlotStatus(GameSlot* slot);
        SlotLine SlotLineOf(GameSlot* slot); // the same, as if no game were running
        float PlayBandH() const; // space reserved only for the pad legend on the game page
        void DrawPortalStatus(); // "Portal: <model>" or "Portal: not detected", with its tooltip; nothing when HID is unavailable

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
        void SetExperimentalTrapTeam(bool enabled);
        void DrawHeroInstall(ImVec2 at, float width);
        void DrawHeroSteps(ImVec2 at, ImVec2 size);
        void TestHeroScript(uint64_t now);
        void TestHeroNavigation();
        void DrawSettings();     // the launcher's settings (the gear): Controls, Launcher, Support, About
        void DrawControls();
        void DrawLauncherTab();
        void DrawLauncherLanguage(); // Settings > Launcher > Language
        void UpdateLanguage();       // each frame: the text follows [launcher] language, never the shown game's
        void DrawSupport();      // support_page.cpp: logs and the problem report
        // A game's own settings (game_settings_page.cpp), opened from the Settings button on its Play page.
        void DrawGameSettings();
        // A game's achievements and play time (achievements_page.cpp), opened from its Play page.
        void DrawAchievements();
        void RefreshProgress(bool force = false);
        const ArtTexture* AchievementIcon(const std::filesystem::path& image);
        void DrawGameVideo();
        void DrawGameKeyboard();
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
        void DrawGameController();
        void DrawUpdateAll(ImVec2 right);
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
        // A settings page's head: the centred column, the title (Display) and, when `back`, the Back button at its right.
        // EndSettingsPage closes the column.
        void BeginSettingsPage(const char* id, const char* title, bool back);
        void EndSettingsPage() { EndPageColumn(); }
        void DrawAbout();        // update_page.cpp: About
        void DrawUpdates();      // update_page.cpp: check for updates, and the automatic check; in Settings > Launcher
        void StartUpdateCheck(bool manual);
        void PollUpdates();
        float BannerHeight();
        void DrawUpdateBanner(float height);
        void DrawWhatsNew();
        void DrawPortalTest();   // support_page.cpp: the "Test the portal" row, inside an open settings table
        void DrawPortalResult(); // and what the test found, under the table
        void DrawLogs();
        void OpenProblemReport();
        void DrawProblemReport();
        void DrawToast();
        void DrawQuitPrompt();
        // SKY_LAUNCHER_TEST_GALLERY (gallery.cpp): every component in every state, page by page, in place of the window's pages.
        void DrawGallery();
        void TestGalleryScript(uint64_t now);

        // Pages and focus. The sidebar and the page are separate windows, which the D-pad doesn't cross: B / Escape
        // moves the focus to the sidebar (or leaves Settings and Collection), A on a row moves it into the page.
        void GoTo(Page page);
        void Back();
        void TakeFocus(FocusTo where); // after the item that takes the focus when `where` is asked for
        void RequestFocus(FocusTo where) { m_focusTo = where; m_focusShow = ImGui::GetIO().NavVisible; }

        // The collection (collection_page.h): its page draws with the widgets below.
        friend class CollectionPage;

        // The game page in the game's own look (game_look.cpp, docs/launcher_ui.md "Each game's look").
        // The hero's main action and separate settings button. Takes the page's first available focus stop.
        struct MainAction
        {
            const char* label;
            bool enabled = true;
            float progress = -1; // 0..1 while busy (checking the disc, installing); < 0 otherwise
            bool indeterminate = false; // busy with no known end: a sweep in place of the percentage
            ui::Icon icon = ui::Icon::None;
            bool danger = false;
            float width = ui::height::MainWidth;
        };
        enum class MainActionHit { None, Main, Settings };
        MainActionHit DrawMainAction(ImVec2 at, const MainAction& action);
        // A logo inside `size` at `at` (centred, aspect kept), with a soft shadow; `alpha` 0..1.
        void DrawLogo(ImDrawList* draw, const ArtTexture& logo, ImVec2 at, ImVec2 size, float alpha = 1, bool shadow = true);
        // A game's mark in a sidebar row: its logo once its disc was read, else its initial on its colour.
        void DrawGameMark(ImDrawList* draw, const LauncherGameEntry& entry, ImVec2 at, ImVec2 size);
        // The logo's arrival on the game page: 0 when it starts falling, 1 once landed (always 1 with reduced motion).
        float LogoDrop();
        // The game page's logo inside `size` at `at`, falling in on arrival (LogoDrop), with its shadow.
        void DrawHeroLogo(ImDrawList* draw, const ArtTexture& logo, ImVec2 at, ImVec2 size);
        // A game's cached picture (its logo, cropped; its background), at most `maxWidth` pixels wide, loaded once; nullptr
        // before the game's disc was ever read. The sidebar and Home use them for games not yet shown too.
        const ArtTexture* CachedArt(const char* game, LauncherAssets::Role role, int maxWidth);
        void ForgetCachedArt(const char* game); // its disc was read again
        bool ReduceMotion() const;

        // A page's centred column (the cursor is at its left): it reaches a few points past each side and over the page's
        // bottom padding, so the clip rect neither shaves the borders at its edges nor cuts the scrolling content short of the window.
        float m_columnBelow = 0;
        void BeginPageColumn(const char* id, float width, bool alwaysScrollbar = false);
        void EndPageColumn();
        // A settings row's select: the choices' labels, the value in [section] key; the control column's width.
        bool SettingSelect(const char* id, const char* section, const char* key, std::span<const Choice> choices, int fallback);
        void BeginCard(const char* id, ImVec2 size, ImGuiChildFlags flags = ImGuiChildFlags_None);
        void EndCard();

        // Installing the game: the hero progress and its detailed checklist.
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
        const GameUpdate* PendingUpdate(const GameSlot& slot) const; // the game's entry in the release's games line, if any
        std::string WantedKitTag(const GameSlot& slot) const;        // the release whose kit INSTALL / UPDATE must be using
        std::string GameVersion(const GameSlot& slot) const;         // "0.8.2": the built game's, else the kit's; "" when unknown
        void StartInstall();
        void PollInstall();
        // "Update all" (Home): the games with an update, one after the other (docs/updates.md).
        int GamesToUpdate() const;
        void StartUpdateAll();
        void CancelUpdateAll();
        void PollUpdateAll();
        bool UpdatingAll() const { return !m_updateAll.queue.empty() || !m_updateAll.current.empty(); }
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
        // At start-up, the other games' discs are checked too, one after the other in the background (each as its first
        // visit would), so every game shows its real state and art at once.
        std::vector<std::string> m_discQueue;
        void CheckQueuedDiscs();
        void StartVerify(const std::string& path);
        void StartGameDisc(); // the remembered disc image, or the game's copy when it is complete (game_data_page.cpp)
        void FinishVerify();
        void UseDiscForOtherGame(); // the disc check found the other supported game's disc: switch to it and set the disc
        void Browse();
        bool DiscStillThere(); // false, and the disc's line says so, when the verified image is gone
        void Set(const char* section, const char* key, const std::string& value);
        // A message at the bottom right: an Error stays until it is closed; Info and Success go by themselves.
        void Toast(std::string text, ui::ToastKind kind = ui::ToastKind::Info);
        void RefreshGamepads();
        PadFamily PadFamilyNow() const; // whose button names the Keyboard tab shows (game_keys.h)
        ui::Glyphs PadGlyphs() const;   // the same family as the pictures of its buttons (never the keyboard)
        std::vector<std::pair<std::string, std::string>> DeviceOptions() const; // value, label
        std::string CurrentDevice() const;
        std::string DeviceLabel() const;
        void SetDevice(const std::string& value);
        void Screenshot(const char* name);
        void TestScript();
        void TestScreensScript(uint64_t now);
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
        void TestRunningScript(uint64_t now);
        void PlayNow(); // PLAY on the shown game: starts it beside the window (m_startGames), else returns its disc and build
        void AutoPlay(); // --play: PLAY as soon as the shown game's disc is checked, if it can
        void PollGame(); // the game running: its end (a crash is told), or one found running

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
        bool m_startGames = false; // PLAY starts the game and the window stays (LauncherOptions)
        GameSession m_session;     // the game running: one at a time (game_run.h)
        uint64_t m_sessionStart = 0; // SDL_GetTicks when the game started: the time on the game page
        std::string m_sessionIso;  // the disc image it was started with (the crash report names it)
        ProbeJob m_probe;
        launcher_portal::Watcher m_portal;
        bool m_reportOpen = false;      // "Report a problem" shown (support_page.cpp)
        std::string m_reportText;

        // Updates (update_page.cpp).
        UpdateManager m_updates;
        bool m_updateManual = false;     // the running check was asked for: say the answer, even "up to date"
        bool m_updateLater = false;      // the player closed the banner for this session
        bool m_testPlayWithout = false;        // tests: press "Play without updating" on the next frame
        std::vector<GameUpdate> m_gameUpdates; // the games line of the last answered check
        struct UpdateAllRun
        {
            std::vector<std::string> queue; // games still to start
            std::string current;            // the game being updated
            int total = 0, started = 0, failed = 0;
        } m_updateAll;
        bool m_whatsNewOpen = false;
        bool m_updateWasBusy = false;
        std::string m_updateDoneNote;    // the last answer of a manual check, shown in About
        float m_bannerH = 0;             // the height the banner takes at the top of the page, this frame
        SDL_Texture* m_github = nullptr;
        bool m_dragOver = false;
        ui::NavigationColumns m_heroNavigation;

        std::vector<SDL_JoystickID> m_gamepads;
        SDL_GamepadType m_lastPadType = SDL_GAMEPAD_TYPE_UNKNOWN; // the last pad connected
        const GameKey* m_capturing = nullptr; // the key row of the shown game waiting for a key press

        // The games opened this session (one slot each), and the one shown. Every slot's jobs are polled; the shown
        // one's state is what the pages draw (m_s).
        std::vector<std::unique_ptr<GameSlot>> m_slots;
        GameSlot* m_s = nullptr;

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
        bool m_testUninstall = false;  // SKY_LAUNCHER_TEST_UNINSTALL: the uninstall screens
        bool m_testKeys = false;       // SKY_LAUNCHER_TEST_KEYS: each game's Keyboard tab, one shot each
        int m_testScreens = 0;         // SKY_LAUNCHER_TEST_SCREENS: Home, the game page and its Keyboard tab; 1 with the mouse, 2 with a pad
        bool m_testPadInput = false;   // SKY_LAUNCHER_TEST_INPUT=pad (or SCREENS=pad): any script's shots as with a pad (its legends, the ring)
        int m_testFocusView = -1;      // the page and tab the last pad focus was given on
        bool m_testPad = false;        // SKY_LAUNCHER_TEST_PAD: each game's Controller tab, and Swap Force's Game tab
        bool m_testRunning = false;      // SKY_LAUNCHER_TEST_RUNNING: one game at a time (TestRunningScript)
        bool m_testHero = false;         // SKY_LAUNCHER_TEST_HERO: four display fixtures, no game or install started
        int m_testHeroState = -1;        // ready, needs-disc, installing, running
        bool m_testHeroNoArt = false;
        std::vector<uint8_t> m_testHeroArtPixels;
        InstallProgress m_testHeroProgress;
        std::vector<uint32_t> m_testHeroControls, m_heroNavExpected, m_heroNavSeen;
        std::map<std::pair<uint32_t, int>, uint32_t> m_heroNavEdges;
        int m_heroNavCase = 0, m_heroNavPhase = -1, m_heroNavWarmup = 0, m_heroNavMoves = 0, m_heroNavDirection = 0;
        uint32_t m_heroNavFrom = 0;
        bool m_testAchievements = false; // SKY_LAUNCHER_TEST_ACHIEVEMENTS: the shown game's page with its play time, then its achievements
        bool m_testLogo = false;       // SKY_LAUNCHER_TEST_LOGO: each game's page with its logo (from the art already read), one shot each
        bool m_testScrollEnd = false;  // the uninstall screenshots: scroll the settings column to its end
        uint64_t m_testHoldAt = 0;     // when the test pressed the hold button (it holds it down for the screenshots)

        // The game page's logo drop: when it began, and what was shown last frame (a new arrival starts it again).
        uint64_t m_logoDropAt = 0;
        const GameSlot* m_logoDropSlot = nullptr;
        bool m_logoDropHad = false; // the logo was there (it may arrive after the page, once the disc is read)
        Page m_lastPage = Page::Home;
        GameProgress m_progress;           // the shown game's achievements and play time (RefreshProgress)
        const GameSlot* m_progressSlot = nullptr;
        std::string m_progressLanguage;
        uint64_t m_progressAt = 0;
        std::map<std::string, ArtTexture> m_achievementIcons; // by file
        std::map<std::string, ArtTexture> m_cachedArt; // CachedArt's, by "<game> <role> <width>"

        ui::Toasts m_toasts; // newest last; drawn at the bottom right, upwards
        std::vector<ImVec4> m_stars; // x, y (0..1), radius, phase

        // Screenshot test (SKY_LAUNCHER_SHOTS, SKY_LAUNCHER_TEST_DROP): drives the screens without a player.
        std::string m_shots, m_testDrop;
        bool m_testUpdate = false;  // SKY_LAUNCHER_TEST_UPDATE: the update screens (SKY_LAUNCHER_UPDATE_URL names a fake release)
        bool m_testReal = false;    // SKY_LAUNCHER_TEST_INSTALL=real: the real kit, a screenshot every SKY_LAUNCHER_TEST_EVERY seconds
        bool m_testInstall = false; // SKY_LAUNCHER_TEST_INSTALL: the install screens instead
        bool m_testCollection = false; // SKY_LAUNCHER_TEST_COLLECTION: the collection's screens instead
        bool m_testSeatX = false;      // SKY_LAUNCHER_TEST_SEATX: the X on every seat, by mouse, keyboard and pad
        std::vector<std::string> m_testSeats; // the seats the SEATX script restores before each try
        std::string m_testBuilt;                    // the sidebar test's built game, put back between the states it sets by hand
        std::map<std::string, std::string> m_drawn; // while testing: what the sidebar, Home and the main button drew ("sidebar:ssa", "home:ssa", "button")
        bool m_testExperimental = false; // SKY_LAUNCHER_TEST_TRAPTEAM: visibility and settings, without a disc
        bool m_testSidebar = false;    // SKY_LAUNCHER_TEST_SIDEBAR: the sidebar's line through an install and an update (fake kit)
        bool m_testCopy = false;       // SKY_LAUNCHER_TEST_COPY: copying the game data, playing from it, the wrong-disc button
        std::string m_testKit;         // SKY_LAUNCHER_TEST_KIT: "old", "giants", "updates" or "offline": the kit that follows the launcher, the game updates (fake release)
        bool m_testSwitch = false;     // SKY_LAUNCHER_TEST_SWITCH: both games' slots, a switch back and forth, PLAY on each
        int m_testFailures = 0;
        uint64_t m_testBegan = 0;
        std::string m_testSha, m_testPath, m_testRuntime[2];
        bool m_testNav = false;        // SKY_LAUNCHER_TEST_NAV: keyboard and pad navigation, keys sent to ImGui
        bool m_testControllers = false; // open the controller menu (its screenshot)
        bool m_testButtons = false;     // SKY_LAUNCHER_TEST_BUTTONS: the action button's states, both games
        bool m_testButtonsOff = false;  // its "disabled" shot: the main button drawn disabled
        bool m_testGallery = false;     // SKY_LAUNCHER_TEST_GALLERY: the component gallery (gallery.cpp)
        int m_galleryPage = 0;          // the gallery's page shown
        std::string m_testShot;         // the next shot's name, for scripts that name their own
        float m_testUiScale = 0;        // > 0: the UI scale, whatever the display (the button test)
        ImVec2 m_testMainAt, m_testGearAt; // the action button's two halves' centres, last drawn (where the script points)
        int m_testStep = 0;
        uint64_t m_testAt = 0;
    };

    inline bool App::SettingSelect(const char* id, const char* section, const char* key, std::span<const Choice> choices, int fallback)
    {
        const int index = ChoiceIndex(choices, SettingGet(m_config, section, key), fallback);
        bool changed = false;
        if (ui::BeginSelect(id, Tr(choices[size_t(index)].label)))
        {
            for (size_t i = 0; i < choices.size(); i++)
                if (ui::SelectOption(Tr(choices[i].label), int(i) == index))
                {
                    Set(section, key, choices[i].value);
                    changed = true;
                }
            ui::EndSelect();
        }
        return changed;
    }

}
