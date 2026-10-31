// The launcher window (Dear ImGui on SDL_Renderer): a sidebar of games, then the Home, Play and Settings pages and the
// launcher's widgets. The collection's pages are in collection_page.cpp; the class is declared in launcher_app.h.
// Every choice is written to launcher.ini, which the game runtime reads (docs/settings.md lists the keys).
#include "launcher/ui/launcher_ui.h"
#include "launcher/env.h"
#include "launcher/ui/text_fit.h"
#include "launcher/install_checklist.h"
#include "launcher/problem_report.h"
#include "launcher/ui/launcher_app.h"
#include "shared/collection/datadir.h"
#include "shared/embedded_fonts.h"
#include "assets/launcher_assets.h"
#include "launcher/ui/theme/launcher_theme.h"
#include "shared/vfs.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h> // ImGui::FocusItem
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <mutex>
#include <stb_image.h>
#include <stb_image_write.h>
#include <thread>
#include <vector>

namespace launcher_ui
{

    namespace
    {
        // The window's words that changed in the polish pass (the rest are still inline, for #4).
        namespace strings
        {
            constexpr const char* ChooseDisc = N_("Choose disc");
            constexpr const char* PlayWith = N_("PLAY WITH");
            constexpr const char* DiscGone = N_("The disc image isn't there. Choose it again.");
            constexpr const char* LauncherLanguageHint = N_("\"System default\" follows your system.");
            constexpr const char* DataFolderHint = N_("Settings, saves, figures and builds.");
            constexpr const char* GameSettings = N_("Game settings");
            constexpr const char* NoGamepad = N_("No gamepad connected.");
            constexpr const char* OneGamepad = N_("1 gamepad connected.");
            constexpr const char* Gamepads = N_("{} gamepads connected.");
            constexpr const char* PortalTip = N_("Without a real portal, the game uses its virtual one (the Toy Box).");
            constexpr const char* QuitTitle = N_("The game is still installing");
            constexpr const char* QuitText = N_("The install stops and can resume later.");
            constexpr const char* KeepInstalling = N_("Keep installing");
            constexpr const char* Quit = N_("Quit");
        }
    }

    // ---- Settings file ------------------------------------------------------------------------------------------

    std::string SettingGet(const LauncherConfig& config, std::string_view section, std::string_view key, std::string fallback)
    {
        auto s = config.find(std::string(section));
        if (s == config.end())
            return fallback;
        auto k = s->second.find(std::string(key));
        return k == s->second.end() || k->second.empty() ? fallback : k->second;
    }

    int SettingInt(const LauncherConfig& config, std::string_view section, std::string_view key, int fallback)
    {
        std::string value = SettingGet(config, section, key);
        return value.empty() ? fallback : std::atoi(value.c_str());
    }

    bool ParseBool(std::string_view value)
    {
        std::string lower(value);
        for (char& c : lower)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
    }

    // ---- Files and images --------------------------------------------------------------------------------------

    bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out)
    {
        std::ifstream file(datadir::FromText(path), std::ios::binary | std::ios::ate); // UTF-8: accents in a Windows user name
        if (!file.good())
            return false;
        std::streamoff size = file.tellg();
        if (size <= 0)
            return false;
        out.resize(size_t(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(out.data()), size);
        return file.good();
    }


    SDL_Texture* LoadPng(SDL_Renderer* renderer, std::span<const uint8_t> png)
    {
        int width, height, channels;
        stbi_uc* pixels = stbi_load_from_memory(png.data(), int(png.size()), &width, &height, &channels, 4);
        if (pixels == nullptr)
            return nullptr;
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
        if (texture != nullptr)
        {
            SDL_UpdateTexture(texture, nullptr, pixels, width * 4);
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        }
        stbi_image_free(pixels);
        return texture;
    }

    SDL_Texture* LoadPngFile(SDL_Renderer* renderer, const std::string& path)
    {
        std::vector<uint8_t> png;
        return ReadWholeFile(path, png) ? LoadPng(renderer, png) : nullptr;
    }

    SDL_Texture* LoadRgba(SDL_Renderer* renderer, const std::vector<uint8_t>& rgba, int width, int height)
    {
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
        if (texture != nullptr)
        {
            SDL_UpdateTexture(texture, nullptr, rgba.data(), width * 4);
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        }
        return texture;
    }

    ImTextureRef Ref(SDL_Texture* texture)
    {
        return ImTextureRef(ImTextureID(intptr_t(texture)));
    }

    std::string ChooseIsoDialog(SDL_Window* window, const GameInfo& game, const std::string& previous)
    {
        static const SDL_DialogFileFilter filters[] = { { "Xbox 360 disc image (.iso)", "iso" } };
        struct Result { std::atomic<bool> done{ false }; std::string path; };
        Result result;
        SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, const_cast<SDL_DialogFileFilter*>(filters));
        SDL_SetNumberProperty(properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
        std::string title = Tf("Choose your {} disc image", game.name);
        SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
        SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
        if (!previous.empty())
            SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_LOCATION_STRING, previous.c_str());
        SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, [](void* userdata, const char* const* files, int) {
            auto* r = static_cast<Result*>(userdata);
            if (files != nullptr && files[0] != nullptr)
                r->path = files[0];
            r->done = true;
        }, &result, properties);
        SDL_DestroyProperties(properties);
        while (!result.done)
        {
            SDL_PumpEvents();
            SDL_Delay(10);
        }
        return result.path;
    }

    // The GitHub mark (Primer Octicons "mark-github", MIT), flattened to an outline on a 16x16 grid.
    const ImVec2 GitHubMark[] = {
        { 8.00f, 0.00f }, { 4.89f, 0.63f }, { 2.34f, 2.34f }, { 0.63f, 4.89f }, { 0.00f, 8.00f }, { 0.41f, 10.53f }, { 1.54f, 12.73f },
        { 3.27f, 14.45f }, { 5.47f, 15.59f }, { 5.72f, 15.59f }, { 5.89f, 15.51f }, { 5.99f, 15.37f }, { 6.02f, 15.21f }, { 6.02f,
        14.99f }, { 6.01f, 14.64f }, { 6.01f, 14.21f }, { 6.01f, 13.72f }, { 4.76f, 13.79f }, { 3.97f, 13.56f }, { 3.53f, 13.17f }, {
        3.32f, 12.78f }, { 3.20f, 12.53f }, { 3.00f, 12.20f }, { 2.76f, 11.87f }, { 2.50f, 11.65f }, { 2.29f, 11.51f }, { 2.14f, 11.33f
        }, { 2.17f, 11.19f }, { 2.49f, 11.12f }, { 2.93f, 11.21f }, { 3.29f, 11.44f }, { 3.55f, 11.71f }, { 3.72f, 11.94f }, { 4.32f,
        12.58f }, { 4.98f, 12.80f }, { 5.59f, 12.76f }, { 6.05f, 12.60f }, { 6.13f, 12.24f }, { 6.25f, 11.95f }, { 6.39f, 11.71f }, {
        6.56f, 11.53f }, { 5.24f, 11.26f }, { 4.07f, 10.63f }, { 3.24f, 9.46f }, { 2.92f, 7.58f }, { 2.98f, 6.96f }, { 3.14f, 6.39f }, {
        3.40f, 5.88f }, { 3.74f, 5.43f }, { 3.66f, 5.17f }, { 3.58f, 4.71f }, { 3.61f, 4.08f }, { 3.82f, 3.31f }, { 3.95f, 3.29f }, {
        4.35f, 3.33f }, { 5.03f, 3.57f }, { 6.02f, 4.13f }, { 6.51f, 4.01f }, { 7.01f, 3.93f }, { 7.51f, 3.88f }, { 8.02f, 3.86f }, {
        8.53f, 3.88f }, { 9.03f, 3.93f }, { 9.53f, 4.01f }, { 10.02f, 4.13f }, { 11.01f, 3.56f }, { 11.69f, 3.33f }, { 12.09f, 3.29f },
        { 12.22f, 3.31f }, { 12.43f, 4.08f }, { 12.46f, 4.71f }, { 12.38f, 5.17f }, { 12.30f, 5.43f }, { 12.64f, 5.88f }, { 12.90f,
        6.38f }, { 13.06f, 6.95f }, { 13.12f, 7.58f }, { 12.80f, 9.46f }, { 11.96f, 10.63f }, { 10.79f, 11.26f }, { 9.47f, 11.53f }, {
        9.68f, 11.76f }, { 9.85f, 12.08f }, { 9.97f, 12.50f }, { 10.01f, 13.01f }, { 10.01f, 13.77f }, { 10.00f, 14.41f }, { 10.00f,
        14.90f }, { 10.00f, 15.21f }, { 10.03f, 15.37f }, { 10.12f, 15.51f }, { 10.30f, 15.59f }, { 10.55f, 15.59f }, { 11.32f, 15.28f
        }, { 12.06f, 14.89f }, { 12.75f, 14.44f }, { 13.39f, 13.91f }, { 13.98f, 13.32f }, { 14.50f, 12.67f }, { 14.94f, 11.97f }, {
        15.32f, 11.23f }, { 15.37f, 4.89f }, { 13.66f, 2.34f }, { 11.11f, 0.63f }
    };

    // The mark as a white texture whose alpha is its coverage, rasterised once (even-odd, 4x4 samples per pixel):
    // ImGui's concave fill can't triangulate this outline.
    SDL_Texture* CreateGitHubMarkTexture(SDL_Renderer* renderer, int size)
    {
        constexpr int Samples = 4;
        constexpr size_t Count = std::size(GitHubMark);
        std::vector<uint8_t> rgba(size_t(size) * size * 4, 255);
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++)
            {
                int covered = 0;
                for (int sy = 0; sy < Samples; sy++)
                    for (int sx = 0; sx < Samples; sx++)
                    {
                        float px = (x + (sx + 0.5f) / Samples) * 16 / size, py = (y + (sy + 0.5f) / Samples) * 16 / size;
                        bool inside = false;
                        for (size_t i = 0, j = Count - 1; i < Count; j = i++)
                        {
                            const ImVec2 &a = GitHubMark[i], &b = GitHubMark[j];
                            if ((a.y > py) != (b.y > py) && px < (b.x - a.x) * (py - a.y) / (b.y - a.y) + a.x)
                                inside = !inside;
                        }
                        covered += inside;
                    }
                rgba[(size_t(y) * size + x) * 4 + 3] = uint8_t(covered * 255 / (Samples * Samples));
            }
        return LoadRgba(renderer, rgba, size, size);
    }

    // ---- Disc verification on a worker thread --------------------------------------------------------------------

    void VerifyJob::Start(const std::string& image, const GameInfo& game)
    {
        Join();
        done = false;
        progress = 0;
        { std::lock_guard lock(mutex); stage = Tr("Opening the disc image"); check = {}; meta = {}; assets = {}; }
        thread = std::thread([this, image, &game] {
            auto report = [this](float fraction, std::string_view text) {
                progress = fraction;
                std::lock_guard lock(mutex);
                stage.assign(text);
            };
            GameSourceCheck result = CheckGameSource(image, game, report);
            TitleMetadata metadata;
            LauncherAssets::Provider art;
            if (result.status == GameSourceStatus::Ok)
            {
                report(0.96f, Tr("Reading the title and icon"));
                DiscFileSystem disc;
                if (disc.Open(image.c_str()))
                {
                    if (std::vector<uint8_t> xex = disc.ReadAll("default.xex"); !xex.empty())
                    {
                        metadata = ReadTitleMetadata(xex);
                        // The achievements: the list, its texts and pictures, cached once for this default.xex (game_progress.h).
                        if (std::filesystem::path folder = AchievementCacheFolder(game.id); !folder.empty() && !achievements::CacheIsCurrent(folder, result.xexSha256))
                            achievements::WriteCache(folder, result.xexSha256, xex);
                    }
                    // The game's own art and its toys' portraits, decoded from the player's disc into the game's data
                    // folder (never shipped); a second time, only what is missing.
                    report(0.98f, Tr("Reading the game's pictures"));
                    art.Init(game.id);
                    art.ExtractFromDisc(disc, metadata);
                }
            }
            { std::lock_guard lock(mutex); check = std::move(result); meta = std::move(metadata); assets = std::move(art); }
            progress = 1;
            done = true;
        });
    }

    // ---- Setup ---------------------------------------------------------------------------------------------------

    bool App::LoadFonts()
    {
        std::vector<std::string> directories;
        if (const char* directory = LauncherEnv("SKY_LAUNCHER_FONT_DIR"))
            directories.emplace_back(directory);
        if (const char* base = SDL_GetBasePath())
        {
            directories.emplace_back(std::string(base) + "fonts");
            directories.emplace_back(std::string(base) + "../Resources/fonts");
        }
#ifdef SSA_SOURCE_FONT_DIR
        directories.emplace_back(SSA_SOURCE_FONT_DIR);
#endif
        bool found = false;
        for (const std::string& directory : directories)
            if (ReadWholeFile(directory + "/Outfit-Regular.ttf", m_fontData[0]) && ReadWholeFile(directory + "/Bungee-Regular.ttf", m_fontData[1]) &&
            ReadWholeFile(directory + "/Outfit-SemiBold.ttf", m_fontData[2]))
            {
                found = true;
                break;
            }
        if (!found)
        {
            m_fontData[0].assign(g_embeddedOutfit, g_embeddedOutfit + g_embeddedOutfitSize);
            m_fontData[1].assign(g_embeddedBungee, g_embeddedBungee + g_embeddedBungeeSize);
            m_fontData[2].assign(g_embeddedOutfitBold, g_embeddedOutfitBold + g_embeddedOutfitBoldSize);
        }

        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false; // the vectors outlive the atlas
        // Glyphs are rasterized at the screen's density (DisplayFramebufferScale), so no extra oversampling: crisper stems.
        config.OversampleH = config.OversampleV = 1;
        config.PixelSnapH = true;
        m_textFont = io.Fonts->AddFontFromMemoryTTF(m_fontData[0].data(), int(m_fontData[0].size()), TextSize, &config);
        m_boldFont = io.Fonts->AddFontFromMemoryTTF(m_fontData[2].data(), int(m_fontData[2].size()), TextSize, &config);
        m_titleFont = io.Fonts->AddFontFromMemoryTTF(m_fontData[1].data(), int(m_fontData[1].size()), TitleSize, &config);
        m_monoFont = io.Fonts->AddFontDefaultVector(); // logs: Dear ImGui's own monospace font
        // The icons (Lucide, ui::DrawIcon): always the built-in subset.
        ImFont* icons = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(g_embeddedLucide), int(g_embeddedLucideSize), TextSize, &config);
        ui::Ctx().fonts = { m_textFont, m_boldFont, m_titleFont, m_monoFont, icons }; // the components' text styles
        return m_textFont != nullptr && m_boldFont != nullptr && m_titleFont != nullptr && m_monoFont != nullptr && icons != nullptr;
    }

    bool App::Init()
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
            return false;
        // The offscreen driver (screenshot test) has no GPU: no graphics context for the window, software drawing.
        const char* driver = SDL_GetCurrentVideoDriver();
        bool offscreen = driver != nullptr && (std::string_view(driver) == "offscreen" || std::string_view(driver) == "dummy");
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
        if (ParseBool(SettingGet(m_config, "launcher", "fullscreen")))
            flags |= SDL_WINDOW_FULLSCREEN;
        SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Arkeyan Launcher");
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, int(WindowW));
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, int(WindowH));
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, Sint64(flags));
        if (offscreen)
            SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
        m_window = SDL_CreateWindowWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (m_window == nullptr)
            return false;
        // The window's icon (taskbar, Alt-Tab, the Dock).
        {
            int iw, ih, channels;
            if (stbi_uc* pixels = stbi_load_from_memory(g_embeddedIcon, int(g_embeddedIconSize), &iw, &ih, &channels, 4))
            {
                if (SDL_Surface* icon = SDL_CreateSurfaceFrom(iw, ih, SDL_PIXELFORMAT_RGBA32, pixels, iw * 4))
                {
                    SDL_SetWindowIcon(m_window, icon);
                    SDL_DestroySurface(icon);
                }
                stbi_image_free(pixels);
            }
        }
        // Sized once the window knows its display's scale, then shown.
        UpdateUiScale(true);
        SDL_ShowWindow(m_window);
        m_renderer = SDL_CreateRenderer(m_window, offscreen ? SDL_SOFTWARE_RENDERER : nullptr);
        if (m_renderer == nullptr)
            m_renderer = SDL_CreateRenderer(m_window, SDL_SOFTWARE_RENDERER);
        if (m_renderer == nullptr)
            return false;
        SDL_SetRenderVSync(m_renderer, 1);

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigDebugHighlightIdConflicts = false;
#ifdef NDEBUG
        io.ConfigErrorRecoveryEnableTooltip = false; // a player never sees Dear ImGui's own panel
#endif
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        Apply(ImGui::GetStyle());
        if (!LoadFonts())
            return false;
        ImGui_ImplSDL3_InitForSDLRenderer(m_window, m_renderer);
        ImGui_ImplSDLRenderer3_Init(m_renderer);

        SDL_srand(7);
        for (int i = 0; i < 140; i++)
            m_stars.push_back(ImVec4(SDL_randf(), SDL_randf(), 0.6f + 1.4f * SDL_randf(), 6.28f * SDL_randf()));

        if (const char* shots = LauncherEnv("SKY_LAUNCHER_SHOTS"))
        {
            m_shots = shots;
            std::error_code error;
            std::filesystem::create_directories(m_shots, error);
            if (const char* drop = LauncherEnv("SKY_LAUNCHER_TEST_DROP"))
                m_testDrop = drop;
            m_testInstall = LauncherEnv("SKY_LAUNCHER_TEST_INSTALL") != nullptr;
            m_testReal = m_testInstall && std::string_view(LauncherEnv("SKY_LAUNCHER_TEST_INSTALL")) == "real";
            m_testUpdate = LauncherEnv("SKY_LAUNCHER_TEST_UPDATE") != nullptr;
            m_testCollection = LauncherEnv("SKY_LAUNCHER_TEST_COLLECTION") != nullptr;
            m_testNav = LauncherEnv("SKY_LAUNCHER_TEST_NAV") != nullptr;
            m_testSwitch = LauncherEnv("SKY_LAUNCHER_TEST_SWITCH") != nullptr;
            m_testExperimental = LauncherEnv("SKY_LAUNCHER_TEST_TRAPTEAM") != nullptr;
            m_testSidebar = LauncherEnv("SKY_LAUNCHER_TEST_SIDEBAR") != nullptr;
            if (const char* kit = LauncherEnv("SKY_LAUNCHER_TEST_KIT"))
                m_testKit = kit;
            m_testCopy = LauncherEnv("SKY_LAUNCHER_TEST_COPY") != nullptr;
            m_testSeatX = LauncherEnv("SKY_LAUNCHER_TEST_SEATX") != nullptr;
            m_testButtons = LauncherEnv("SKY_LAUNCHER_TEST_BUTTONS") != nullptr;
            m_testUninstall = LauncherEnv("SKY_LAUNCHER_TEST_UNINSTALL") != nullptr;
            m_testLogo = LauncherEnv("SKY_LAUNCHER_TEST_LOGO") != nullptr;
            m_testKeys = LauncherEnv("SKY_LAUNCHER_TEST_KEYS") != nullptr;
            if (const char* screens = LauncherEnv("SKY_LAUNCHER_TEST_SCREENS"))
                m_testScreens = std::string_view(screens) == "pad" ? 2 : 1;
            const char* input = LauncherEnv("SKY_LAUNCHER_TEST_INPUT");
            m_testPadInput = m_testScreens == 2 || (input != nullptr && std::string_view(input) == "pad");
            m_testGallery = LauncherEnv("SKY_LAUNCHER_TEST_GALLERY") != nullptr;
            m_testPad = LauncherEnv("SKY_LAUNCHER_TEST_PAD") != nullptr;
            m_testAchievements = LauncherEnv("SKY_LAUNCHER_TEST_ACHIEVEMENTS") != nullptr;
            m_testRunning = LauncherEnv("SKY_LAUNCHER_TEST_RUNNING") != nullptr;
            m_testHero = LauncherEnv("SKY_LAUNCHER_TEST_HERO") != nullptr;
        }
        RefreshGamepads();
        m_github = CreateGitHubMarkTexture(m_renderer, 128);
        m_portal.Start();
        return true;
    }

    namespace
    {
        std::vector<std::string> GameIds()
        {
            std::vector<std::string> ids;
            for (const GameInfo& each : g_games)
                ids.emplace_back(each.id);
            return ids;
        }
    }

    App::App(const GameInfo& game, const std::string& configPath, LauncherConfig& config, const LauncherOptions& options)
        : m_configPath(configPath), m_config(config), m_session(GameIds())
    {
        m_s = &SlotFor(FindEnabledGame(game.id, config) != nullptr ? game : *FindGame("ssa"));
        SetActiveGame(m_s->game.id);
        m_autoPlay = options.play;
        m_startGames = options.startGames;
        if (options.openGame)
            m_page = Page::Play; // a shortcut opens on the game, not on the launcher's menu
        for (const GameInfo& each : g_games)
        {
            if (FindEnabledGame(each.id, m_config) == nullptr)
                continue;
            SeedGameVideo(m_config, each.id); // each game starts from the video settings the player already had
            SeedGameInput(m_config, each.id); // and from the keys
        }
    }

    // The slot of `game`, made the first time it is asked for.
    GameSlot& App::SlotFor(const GameInfo& game)
    {
        for (auto& slot : m_slots)
            if (&slot->game == &game)
                return *slot;
        m_slots.push_back(std::make_unique<GameSlot>(game, *this));
        m_slots.back()->collection->SelectGame(game.id);
        // Before its turn in the start-up checks the game reads as it will: a remembered disc is still to check, none is "Needs your disc".
        GameSourcePick pick = PickGameSource(game, SettingGet(m_config, game.id, "iso"));
        m_slots.back()->copy = pick.copy;
        m_slots.back()->discQueued = !pick.path.empty();
        return *m_slots.back();
    }

    // The sidebar's switch. Nothing closes: the pages now draw this game's state. Its disc check starts on the first
    // visit and runs in the background; a game seen before is shown as it was left.
    void App::SelectGame(const GameInfo& game)
    {
        if (FindEnabledGame(game.id, m_config) == nullptr)
            return;
        m_s = &SlotFor(game);
        SeedGameVideo(m_config, game.id);
        SeedGameInput(m_config, game.id);
        SetActiveGame(game.id);
        Set("launcher", "game", game.id);
        if (!m_s->started)
        {
            m_s->started = true;
            StartGameDisc();
        }
        m_s->saveBackupsLoaded = false;
        m_busyFrames = 2;
    }

    bool App::AnyInstalling() const
    {
        for (const auto& slot : m_slots)
            if (slot->installer.Running() || slot->kitDownload.Running())
                return true;
        return false;
    }

    bool App::AnyVerifying() const
    {
        for (const auto& slot : m_slots)
            if (slot->disc == DiscState::Verifying)
                return true;
        return false;
    }

    // Every opened game's jobs go on while another is shown: each is polled with its own slot (and its game's folders)
    // in place, then the shown one comes back.
    void App::CheckQueuedDiscs()
    {
        // One check at a time: the next starts when no disc is being read (the shown game's first).
        if (m_discQueue.empty() || AnyVerifying())
            return;
        const GameInfo* game = FindGame(m_discQueue.front());
        m_discQueue.erase(m_discQueue.begin());
        if (game == nullptr || FindEnabledGame(game->id, m_config) == nullptr)
            return;
        GameSlot& slot = SlotFor(*game);
        if (slot.started)
            return;
        GameSlot* shown = m_s;
        m_s = &slot;
        SetActiveGame(game->id);
        slot.started = true;
        StartGameDisc();
        m_s = shown;
        SetActiveGame(m_s->game.id);
    }

    void App::PollSlots()
    {
        GameSlot* shown = m_s;
        for (auto& slot : m_slots)
        {
            m_s = slot.get();
            SetActiveGame(m_s->game.id);
            if (m_s->disc == DiscState::Verifying && m_s->verify.done)
                FinishVerify();
            if (m_s->copyJob.Done())
                FinishCopy();
            PollInstall();
        }
        m_s = shown;
        SetActiveGame(m_s->game.id);
    }

    void App::Shutdown()
    {
        m_portal.Stop();
        for (auto& slot : m_slots)
            slot->verify.Join();
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui_ImplSDLRenderer3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
        }
        for (auto& slot : m_slots)
        {
            slot->collection->ClearIcons();
            if (slot->icon != nullptr)
                SDL_DestroyTexture(slot->icon);
            slot->icon = nullptr;
            slot->logo.Destroy();
        }
        for (auto& [key, art] : m_cachedArt)
            art.Destroy();
        m_cachedArt.clear();
        if (m_github != nullptr)
            SDL_DestroyTexture(m_github);
        m_github = nullptr;
        if (m_renderer != nullptr)
            SDL_DestroyRenderer(m_renderer);
        if (m_window != nullptr)
            SDL_DestroyWindow(m_window);
        m_renderer = nullptr;
        m_window = nullptr;
    }

    std::string App::Run()
    {
        if (!Init())
        {
            fprintf(stderr, "launcher: cannot open the window: %s\n", SDL_GetError());
            return {};
        }
        if (m_shots.empty())
        {
            m_s->started = true;
            StartGameDisc();
            for (const LauncherGameEntry& entry : LauncherGameCatalog(m_config))
                if (entry.enabled && FindGame(entry.id) != nullptr && std::string_view(entry.id) != m_s->game.id)
                    m_discQueue.push_back(entry.id);
        }
        if (m_shots.empty() && LauncherEnv("SKY_LAUNCHER_NO_UPDATE_CHECK") == nullptr)
        {
            CleanupOldUpdate(LauncherSelf()); // the .old a previous update left
            // At every start (the launcher's release and the games' versions), if the player left it on; offline it is silent.
            if (ParseBool(SettingGet(m_config, "update", "auto_check", "1")))
                StartUpdateCheck(false);
        }
        while (m_running)
        {
            SDL_Event event;
            // Nothing moves on screen: sleep until an input comes, or the wait ends (jobs, the portal and the collection
            // are polled then). The launcher doesn't redraw at the display's rate for nothing.
            if (int wait = IdleWait(); wait > 0 && SDL_WaitEventTimeout(&event, wait))
                HandleEvent(event);
            while (SDL_PollEvent(&event))
                HandleEvent(event);
            PollSlots();
            PollGame();
            CheckQueuedDiscs();
            PollUpdateAll();
            PollUpdates();
            AutoPlay();
            Frame();
            TestScript();
        }
        return m_s->chosen;
    }

    // ---- Events and actions --------------------------------------------------------------------------------------

    int App::IdleWait()
    {
        if (!m_shots.empty()) // the screenshot test runs flat out
            return 0;
        if (m_busyFrames > 0)
        {
            m_busyFrames--;
            return 0;
        }
        // Something moves: a check, an install's spinner, an update's bar, a held button, a text cursor, a drag, a component's
        // own motion (a spinner, a sliding toggle).
        if (AnyVerifying() || AnyInstalling() || m_updates.Busy() || ImGui::IsAnyItemActive() || ui::Ctx().animating)
            return 0;
        if (m_page == Page::Play && m_logoDropAt != 0 && SDL_GetTicks() < m_logoDropAt + uint64_t(LogoDropMs) + 20) // the logo is falling
            return 0;
        constexpr int Idle = 500;
        int wait = Idle;
        uint64_t now = SDL_GetTicks();
        if (int toast = m_toasts.NextWakeMs(now); toast >= 0) // wake to take it down
            wait = std::min(wait, toast);
        // The night sky's stars twinkle, at 30 fps, while the window is seen.
        bool seen = !(SDL_GetWindowFlags(m_window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED | SDL_WINDOW_HIDDEN));
        if (seen && m_page == Page::Play && CachedArt(m_s->game.id, LauncherAssets::Role::HeroBackground, BackdropArtW) == nullptr)
            wait = std::min(wait, 33);
        return wait;
    }

    void App::HandleEvent(const SDL_Event& event)
    {
        m_busyFrames = 3;
        // A key binding being captured takes the next key press for itself.
        if (m_capturing != nullptr && event.type == SDL_EVENT_KEY_DOWN)
        {
            if (event.key.key != SDLK_ESCAPE)
                Set(GameInputSection(m_s->game.id).c_str(), (std::string("key_") + m_capturing->action).c_str(),
                    CapturedKeyName(event.key.scancode).c_str()); // the key's place: the game reads a scancode
            m_capturing = nullptr;
            return;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0 &&
            (event.key.key == SDLK_EQUALS || event.key.key == SDLK_PLUS || event.key.key == SDLK_KP_PLUS ||
             event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS || event.key.key == SDLK_0 || event.key.key == SDLK_KP_0))
        {
            // Ctrl (Cmd) + and - zoom the launcher, 0 goes back to the display's scale; remembered as [launcher] ui_scale.
            bool reset = event.key.key == SDLK_0 || event.key.key == SDLK_KP_0;
            bool bigger = event.key.key == SDLK_EQUALS || event.key.key == SDLK_PLUS || event.key.key == SDLK_KP_PLUS;
            float scale = std::clamp(std::round((m_uiScale + (bigger ? 0.25f : -0.25f)) * 4) / 4, MinUiScale, MaxUiScale);
            Set("launcher", "ui_scale", reset ? "auto" : fmt::format("{:g}", scale));
            UpdateUiScale(false);
            Toast(reset ? Tr("Zoom: automatic") : Tf("Zoom: {:g}%", m_uiScale * 100));
            return;
        }
        // ImGui works in the launcher's points: the window's coordinates divided by the UI scale.
        SDL_Event scaled = event;
        if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            scaled.motion.x /= m_uiScale;
            scaled.motion.y /= m_uiScale;
        }
        ImGui_ImplSDL3_ProcessEvent(&scaled);
        // The focus ring is the keyboard's and the pad's: the pointer moving, pressing or scrolling hides it (ImGui shows it
        // again on the next key or button that moves the focus).
        if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_WHEEL)
            ImGui::SetNavCursorVisible(false);
        switch (event.type)
        {
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            UpdateUiScale(false);
            break;
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            // Cmd+Q, Alt+F4, the window's close: during an install, ask first (it would stop the build).
            if (AnyInstalling())
                m_quitPrompt = true;
            else
                m_running = false;
            break;
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_POSITION:
            m_dragOver = true;
            break;
        case SDL_EVENT_DROP_COMPLETE:
            m_dragOver = false;
            break;
        case SDL_EVENT_DROP_FILE:
            m_dragOver = false;
            // Figure dumps (or, on the collection page, folders of them) join the collection.
            if (event.drop.data != nullptr && (collection::IsDumpName(std::filesystem::path(event.drop.data).filename().string()) ||
                (m_page == Page::Collection && std::filesystem::is_directory(event.drop.data))) && m_s->collection->Drop(event.drop.data))
                m_page = Page::Collection;
            else if (event.drop.data != nullptr && Installing())
                Toast(Tr("Wait for the installation to finish"));
            else if (event.drop.data != nullptr)
            {
                m_page = Page::Play;
                StartVerify(event.drop.data);
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            m_lastPadType = SDL_GetGamepadTypeForID(event.gdevice.which);
            RefreshGamepads();
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            RefreshGamepads();
            break;
        }
    }

    // The UI scale: [launcher] ui_scale when it is a number, else the display's scale beyond its pixel density (Windows'
    // 150 %, 200 %...; 1 on a Mac, whose window units are already points). `resize` sizes the window for it, within the
    // display's usable area.
    void App::UpdateUiScale(bool resize)
    {
        float scale = std::strtof(SettingGet(m_config, "launcher", "ui_scale", "auto").c_str(), nullptr);
        if (!std::isfinite(scale) || scale <= 0) // "auto", or nonsense such as nan
        {
            float density = SDL_GetWindowPixelDensity(m_window), display = SDL_GetWindowDisplayScale(m_window);
            scale = density > 0 && display > 0 ? display / density : 1.0f;
        }
        // Never so large that the launcher's smallest layout leaves the display's usable area (40: the title bar).
        SDL_Rect usable{};
        bool bounded = SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(m_window), &usable) && usable.w > 0 && usable.h > 40;
        if (bounded)
            scale = std::min({ scale, usable.w / WindowMinW, (usable.h - 40) / WindowMinH });
        if (m_testUiScale > 0) // the button test's 150 % and 200 % (the offscreen display is small)
            scale = m_testUiScale;
        float previous = m_uiScale;
        m_uiScale = std::clamp(scale, MinUiScale, MaxUiScale);
        if (m_uiScale != previous || !m_uiScaleLogged) // once, then only when it changes
        {
            m_uiScaleLogged = true;
            fprintf(stderr, "launcher: ui scale %.2f (display scale %.2f, pixel density %.2f, usable %dx%d)\n", m_uiScale,
                SDL_GetWindowDisplayScale(m_window), SDL_GetWindowPixelDensity(m_window), usable.w, usable.h);
        }

        SDL_SetWindowMinimumSize(m_window, int(WindowMinW * m_uiScale), int(WindowMinH * m_uiScale));
        if (resize && !(SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN))
        {
            int w = int(WindowW * m_uiScale), h = int(WindowH * m_uiScale);
            if (bounded)
            {
                w = std::min(w, usable.w);
                h = std::min(h, usable.h - 40);
            }
            if (const char* size = LauncherEnv("SKY_LAUNCHER_TEST_SIZE")) // screenshot test: "1024x680", the smallest window
                std::sscanf(size, "%dx%d", &w, &h);
            SDL_SetWindowSize(m_window, w, h);
            SDL_DisplayID display = SDL_GetDisplayForWindow(m_window);
            SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        }
    }

    void App::Set(const char* section, const char* key, const std::string& value)
    {
        m_config[section][key] = value;
        if (!SaveLauncherConfig(m_configPath, m_config))
            Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
    }

    void App::StartVerify(const std::string& path)
    {
        m_s->disc = DiscState::Verifying;
        m_s->discPath = path;
        m_s->discSize = 0;
        m_s->fromCopy = m_s->copy.state == gamedata::State::Complete && path == datadir::Text(gamedata::Folder(m_s->game.id));
        SDL_PathInfo info;
        if (m_s->fromCopy)
            m_s->discSize = m_s->copy.total;
        else if (SDL_GetPathInfo(path.c_str(), &info))
            m_s->discSize = info.size;
        if (m_s->icon != nullptr)
            SDL_DestroyTexture(m_s->icon);
        m_s->icon = nullptr;
        m_s->logo.Destroy();
        m_s->verify.Start(path, m_s->game);
    }

    void App::FinishVerify()
    {
        m_s->verify.Join();
        m_s->check = m_s->verify.check;
        if (m_s->check.status != GameSourceStatus::Ok && m_s->fromCopy)
        {
            // The copy is not this game's disc, or can't be read: the disc image takes over, if the player has one.
            m_s->copy.state = gamedata::State::Damaged;
            m_s->copy.problem = m_s->check.message;
            m_s->fromCopy = false;
            if (std::string iso = SettingGet(m_config, m_s->game.id, "iso"); !iso.empty())
            {
                StartVerify(iso);
                return;
            }
            m_s->disc = DiscState::Failed;
            return;
        }
        if (m_s->check.status != GameSourceStatus::Ok)
        {
            m_s->disc = DiscState::Failed;
            return;
        }
        m_s->meta = m_s->verify.meta;
        if (!m_s->meta.iconPng.empty())
            m_s->icon = LoadPng(m_renderer, m_s->meta.iconPng);
        m_s->assets = m_s->verify.assets; // the worker cached the disc's art
        m_s->collection->ClearIcons(); // the collection's pictures now come from the disc
        ForgetCachedArt(m_s->game.id);  // the sidebar's and Home's copies too
        if (LauncherAssets::Asset logo = m_s->assets.Get(LauncherAssets::Role::Logo); logo.valid)
            m_s->logo = LoadArt(m_renderer, logo.path, true);
        if (!m_s->fromCopy)
            Set(m_s->game.id, "iso", m_s->discPath); // remembered even if the player quits without playing
        StoreDiscLanguages(m_config, m_s->game.id, m_s->check.languages);
        if (!SaveLauncherConfig(m_configPath, m_config))
            Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
        m_s->disc = DiscState::Ready;
        RefreshInstall();
    }

    // PLAY and INSTALL check the image first: it may have been moved, or its drive unplugged, since it was verified.
    bool App::DiscStillThere()
    {
        if (m_s->fromCopy)
        {
            // The copy is looked at again (sizes only): it may have been damaged since. The disc image takes over then.
            m_s->copy = gamedata::Inspect(gamedata::Folder(m_s->game.id));
            if (m_s->copy.state == gamedata::State::Complete)
                return true;
            m_s->fromCopy = false;
            m_s->installView = m_s->kitUsed = false;
            m_page = Page::Play;
            if (std::string iso = SettingGet(m_config, m_s->game.id, "iso"); !iso.empty())
            {
                Toast(Tr("Game data damaged: using the disc image"), ui::ToastKind::Error);
                StartVerify(iso);
            }
            else
            {
                m_s->disc = DiscState::Failed;
                m_s->check.status = GameSourceStatus::Damaged;
                m_s->check.message = Tf("Game data damaged: {}", m_s->copy.problem);
            }
            return false;
        }
        SDL_PathInfo info;
        if (SDL_GetPathInfo(m_s->discPath.c_str(), &info) && info.type == SDL_PATHTYPE_FILE)
            return true;
        m_s->disc = DiscState::Failed;
        m_s->check.status = GameSourceStatus::NotFound;
        m_s->check.message = Tr(strings::DiscGone);
        m_s->installView = m_s->kitUsed = false;
        m_page = Page::Play;
        return false;
    }

    void App::PlayNow()
    {
        if (!DiscStillThere())
            return;
        if (m_startGames)
        {
            // The game starts beside the window, which stays: one game at a time (GameSession refuses a second one).
            if (!m_s->fromCopy)
                Set(m_s->game.id, "iso", m_s->discPath);
            const GameLaunch launch = PrepareGameLaunch(m_s->game, m_config, m_configPath, m_s->installed.runtime);
            std::string error;
            if (launch.runtime.empty())
                Toast(Tr("Couldn't start the game: the file is missing"), ui::ToastKind::Error);
            else if (!m_session.Start(m_s->game.id, launch, m_s->discPath, error))
                Toast(error, ui::ToastKind::Error);
            else
                m_sessionIso = m_s->discPath;
            return;
        }
        m_s->chosen = m_s->discPath;
        m_s->chosenRuntime = m_s->installed.runtime;
        if (!m_s->fromCopy)
            Set(m_s->game.id, "iso", m_s->chosen);
        m_running = false;
    }

    void App::PollGame()
    {
        const std::optional<GameSession::Ended> ended = m_session.Poll(SDL_GetTicks());
        if (!ended)
            return;
        m_progressAt = 0; // its play time and unlocks are read again
        const GameInfo* game = FindGame(ended->game);
        if (!ended->ours || ended->stopped || ended->exitCode == 0 || game == nullptr)
            return;
        if (!m_shots.empty()) // a screenshot script: no dialog that waits for a click
            m_drawn["stopped"] = fmt::format("{} {}", ended->game, ended->exitCode);
        else
            ShowGameStopped(*game, ended->exitCode, m_configPath, m_config, m_sessionIso, m_window);
    }

    // --play (a desktop shortcut): PLAY as soon as the shown game's disc is checked and the game is built. If it can't
    // be played (no disc, a disc that fails, nothing built), the window stays as it is and says why on its page.
    void App::AutoPlay()
    {
        if (!m_autoPlay || m_s->disc == DiscState::Verifying)
            return;
        m_autoPlay = false;
        if (m_s->disc == DiscState::Ready && !NeedsInstall() && !Installing())
            PlayNow();
        else
            Toast(m_s->disc == DiscState::Ready ? Tr("Press INSTALL first") : Tr("Choose the disc image first"));
    }

    void App::Browse()
    {
        std::string path = ChooseIsoDialog(m_window, m_s->game, m_s->discPath.empty() ? SettingGet(m_config, m_s->game.id, "iso") : m_s->discPath);
        if (!path.empty())
            StartVerify(path);
    }

    void App::RefreshGamepads()
    {
        m_gamepads.clear();
        int count = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&count))
        {
            m_gamepads.assign(ids, ids + count);
            SDL_free(ids);
        }
    }

    // The pad whose button names the Keyboard tab shows: a fixed glyph style, else the chosen pad when it is connected,
    // else the last one connected, else the first one there, else an Xbox 360 pad (the games' own names).
    PadFamily App::PadFamilyNow() const
    {
        SDL_GamepadType type = m_lastPadType;
        std::string chosen = SettingGet(m_config, "input", "device") == "gamepad" ? SettingGet(m_config, "input", "gamepad") : "";
        for (SDL_JoystickID id : m_gamepads)
        {
            char text[64];
            SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), text, sizeof(text));
            if (!chosen.empty() && chosen == text)
            {
                type = SDL_GetGamepadTypeForID(id);
                break;
            }
            if (type == SDL_GAMEPAD_TYPE_UNKNOWN)
                type = SDL_GetGamepadTypeForID(id);
        }
        return PadFamilyFor(SettingGet(m_config, "input", "glyphs", "auto"), PadFamilyOf(type));
    }

    // The device the game listens to: "auto", "keyboard" or "gamepad:<SDL GUID>" ([input] device= and gamepad=).
    std::vector<std::pair<std::string, std::string>> App::DeviceOptions() const
    {
        std::vector<std::pair<std::string, std::string>> options = { { "auto", Tr("Automatic") }, { "keyboard", Tr("Keyboard") } };
        for (SDL_JoystickID id : m_gamepads)
        {
            char text[64];
            SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), text, sizeof(text));
            const char* name = SDL_GetGamepadNameForID(id);
            options.emplace_back(std::string("gamepad:") + text, name != nullptr ? name : Tr("Gamepad"));
        }
        return options;
    }

    std::string App::CurrentDevice() const
    {
        std::string device = SettingGet(m_config, "input", "device", "auto");
        return device == "gamepad" ? "gamepad:" + SettingGet(m_config, "input", "gamepad") : device;
    }

    std::string App::DeviceLabel() const
    {
        std::string current = CurrentDevice();
        for (const auto& [value, label] : DeviceOptions())
            if (value == current)
                return label;
        return current.starts_with("gamepad:") ? Tr("Saved gamepad (not connected)") : Tr("Automatic");
    }

    void App::SetDevice(const std::string& value)
    {
        if (value.starts_with("gamepad:"))
        {
            Set("input", "device", "gamepad");
            Set("input", "gamepad", value.substr(8));
        }
        else
            Set("input", "device", value);
    }

    // ---- Widgets -------------------------------------------------------------------------------------------------

    void App::BeginPageColumn(const char* id, float width, bool alwaysScrollbar)
    {
        // The column has room round its content (ui::BeginRoomyChild): a ring, or a button raised onto a title's capitals, at
        // its edge is not cut. Its foot reaches over the page's bottom padding (to the window's edge).
        constexpr float Bleed = ui::space::M;
        const float below = ImGui::GetStyle().WindowPadding.y;
        ui::BeginRoomyChild(id, ImVec2(width, ImGui::GetContentRegionAvail().y + below - Bleed), ImGuiChildFlags_NavFlattened,
            ImGuiWindowFlags_NoBackground | (alwaysScrollbar ? ImGuiWindowFlags_AlwaysVerticalScrollbar : 0), Bleed);
        ImGui::SetScrollX(0.0f);
        m_columnBelow = below;
    }

    void App::EndPageColumn()
    {
        ImGui::Dummy(ImVec2(0, m_columnBelow));
        ui::EndRoomyChild();
    }

    void App::BeginCard(const char* id, ImVec2 size, ImGuiChildFlags flags)
    {
        ui::CardOptions options;
        options.panel = true; // the settings pages' big cards
        options.flags = flags;
        ui::BeginCard(id, size, options);
    }

    void App::EndCard() { ui::EndCard(); }

    // ---- Screens -------------------------------------------------------------------------------------------------

    void App::DrawBackdrop()
    {
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        ImVec2 size = ImGui::GetIO().DisplaySize;
        // The game's art belongs to the game's page; the launcher's own pages get a plain colour.
        if (m_page != Page::Play)
        {
            draw->AddRectFilledMultiColor(ImVec2(0, 0), size, Rgb(PageTop), Rgb(PageTop), Rgb(PageBottom), Rgb(PageBottom));
            return;
        }
        draw->AddRectFilledMultiColor(ImVec2(0, 0), size, Rgb(SkyTop), Rgb(SkyTop), Rgb(SkyBottom), Rgb(SkyBottom));
        // The game's own painting (its menus' backdrop, from the player's disc: this session's or an earlier one's),
        // cropped to fill the page, instead of the drawn stars.
        const ArtTexture* art = m_testHeroNoArt ? nullptr : CachedArt(m_s->game.id, LauncherAssets::Role::HeroBackground, BackdropArtW);
        SDL_Texture* background = art != nullptr ? art->texture : nullptr;
        if (background != nullptr)
        {
            float w, h;
            SDL_GetTextureSize(background, &w, &h);
            ImVec2 page(SidebarW, 0), pageSize(size.x - SidebarW, size.y);
            float scale = std::max(pageSize.x / w, pageSize.y / h), u = pageSize.x / (w * scale), v = pageSize.y / (h * scale);
            // The software renderer overflows its fixed-point texture coordinates on a page-sized triangle.
            // Small quads keep the same crop and work at every window size and display density.
            const ImVec2 density = ImGui::GetIO().DisplayFramebufferScale;
            const float tileW = 128 / std::max(1.0f, density.x), tileH = 128 / std::max(1.0f, density.y);
            auto uv = [&](float x, float y) {
                return ImVec2((1 - u) / 2 + u * (x - page.x) / pageSize.x, (1 - v) / 2 + v * y / pageSize.y);
            };
            for (float y = 0; y < size.y; y += tileH)
                for (float x = page.x; x < size.x; x += tileW)
                {
                    const float right = std::min(x + tileW, size.x), bottom = std::min(y + tileH, size.y);
                    draw->AddImage(Ref(background), ImVec2(x, y), ImVec2(right, bottom), uv(x, y), uv(right, bottom));
                }
            // Continuous shades across the painting: the title's left and the controls' lower edge.
            draw->AddRectFilledMultiColor(page, size, ui::Col(ui::color::Scrim, 0.9f), ui::Col(ui::color::Scrim, 0.12f),
                ui::Col(ui::color::Scrim, 0.12f), ui::Col(ui::color::Scrim, 0.9f));
            draw->AddRectFilledMultiColor(page, size, ui::Col(ui::color::Scrim, 0), ui::Col(ui::color::Scrim, 0),
                ui::Col(ui::color::Scrim, 0.9f), ui::Col(ui::color::Scrim, 0.9f));
            return;
        }
        // The portal's glow, low on the right, and twinkling stars.
        ImVec2 portal(size.x * 0.78f, size.y * 1.02f);
        for (int ring = 6; ring > 0; ring--)
            draw->AddCircleFilled(portal, size.y * 0.09f * ring, Rgb(PortalGlow, 10), 64);
        float time = float(SDL_GetTicks()) / 1000.0f;
        for (const ImVec4& star : m_stars)
        {
            float twinkle = 0.45f + 0.55f * (0.5f + 0.5f * std::sin(time * 1.3f + star.w));
            draw->AddCircleFilled(ImVec2(star.x * size.x, star.y * size.y * 0.85f), star.z, IM_COL32(255, 255, 255, int(150 * twinkle)), 8);
        }
        const ImVec2 page(SidebarW, 0);
        draw->AddRectFilledMultiColor(page, size, ui::Col(ui::color::Scrim, 0.5f), ui::Col(ui::color::Scrim, 0),
            ui::Col(ui::color::Scrim, 0), ui::Col(ui::color::Scrim, 0.5f));
        draw->AddRectFilledMultiColor(page, size, ui::Col(ui::color::Scrim, 0), ui::Col(ui::color::Scrim, 0),
            ui::Col(ui::color::Scrim, 0.8f), ui::Col(ui::color::Scrim, 0.8f));
    }

    // A game's state in a few words, for the sidebar and Home, and the label of the game page's main button: the one place
    // that decides, read from the game's own slot. The page's other lines (the disc's
    // status, the install card) say more about the same state. The table is in docs/launcher_ui.md.
    App::SlotLine App::SlotStatus(GameSlot* slot)
    {
        using S = SlotState;
        auto line = [](S state, const char* text, unsigned dot, const char* button, bool enabled = true, float progress = -1) {
            return SlotLine{ text, dot, progress, state, button, enabled };
        };
        if (m_testHero && slot == m_s && m_testHeroState >= 0)
        {
            switch (m_testHeroState)
            {
            case 0: return line(S::Ready, Tr("Ready to play"), Ready, Tr("PLAY"), true);
            case 1: return line(S::NoDisc, Tr("Needs your disc"), Accent, Tr(strings::ChooseDisc), true);
            case 2: return line(S::Installing, Tr("Installing"), Accent, Tr("INSTALLING..."), false);
            case 3: return line(S::Running, Tr("Running"), Ready, Tr("Running"), false);
            }
        }
        // One game at a time: the one running says so; the others' buttons wait for it (Running, after the rest).
        const std::string& running = m_session.Running();
        if (running == slot->game.id)
            return line(S::Running, Tr("Running"), Ready, Tr("Running"), false);
        SlotLine result = SlotLineOf(slot);
        if (!running.empty() && result.buttonEnabled)
        {
            result.button = Tf("{} is running", GameName(running, GameNameForm::Short));
            result.buttonEnabled = false;
        }
        return result;
    }

    // A game's state as if none were running (SlotStatus).
    App::SlotLine App::SlotLineOf(GameSlot* slot)
    {
        using S = SlotState;
        auto line = [](S state, const char* text, unsigned dot, const char* button, bool enabled = true, float progress = -1) {
            return SlotLine{ text, dot, progress, state, button, enabled };
        };
        const bool built = !slot->installed.runtime.empty();
        if (slot->installer.Running() || slot->kitDownload.Running())
        {
            InstallProgress snapshot = InstallSnapshot(*slot);
            InstallChecklist list = MakeInstallChecklist(snapshot, SDL_GetTicks());
            bool kit = slot->kitDownload.Running();
            bool finishing = list.current >= 0 && list.rows[size_t(list.current)].id == "finish";
            S state = kit ? S::DownloadingTools : finishing ? S::Finishing : built ? S::Updating : S::Installing;
            const char* verb = Tr(kit ? N_("Downloading tools") : finishing ? N_("Finishing") : built ? N_("Updating") : N_("Installing"));
            SlotLine result = line(state, "", Accent, built ? Tr("UPDATING...") : Tr("INSTALLING..."), false, list.overall);
            result.text = std::string(verb) + " " + ui::FormatPercent(std::clamp(list.overall, 0.0f, 1.0f));
            return result;
        }
        if (slot->disc == DiscState::Verifying || (!slot->started && slot->discQueued))
            return line(S::Checking, Tr("Checking your disc"), Accent, Tr("CHECKING..."), false, -1);
        if (slot->disc == DiscState::None)
        {
            if (slot->copy.state == gamedata::State::Damaged)
                return line(S::DataDamaged, Tr("Game data damaged"), Danger, Tr(strings::ChooseDisc));
            return line(S::NoDisc, Tr("Needs your disc"), Accent, Tr(strings::ChooseDisc));
        }
        if (slot->disc == DiscState::Failed)
        {
            if (slot->copy.state == gamedata::State::Damaged && slot->discPath == datadir::Text(gamedata::Folder(slot->game.id)))
                return line(S::DataDamaged, Tr("Game data damaged"), Danger, Tr(strings::ChooseDisc));
            switch (slot->check.status)
            {
            case GameSourceStatus::NotFound: return line(S::DiscGone, Tr("Disc not found"), Danger, Tr(strings::ChooseDisc));
            case GameSourceStatus::Unreadable: return line(S::DiscUnreadable, Tr("Can't read the disc"), Danger, Tr(strings::ChooseDisc));
            case GameSourceStatus::WrongGame: return line(S::DiscWrong, Tr("Wrong disc"), Danger, Tr(strings::ChooseDisc));
            case GameSourceStatus::WrongVersion: return line(S::DiscUnknown, Tr("Unknown release"), Danger, Tr(strings::ChooseDisc));
            default: return line(S::DiscNotGame, Tr("Not a game disc"), Danger, Tr(strings::ChooseDisc));
            }
        }
        // A verified disc. The page's button is INSTALL until the game is built, PLAY after.
        const char* button = NeedsInstall(*slot) ? Tr("INSTALL") : UpdateAvailable(*slot) ? Tr("UPDATE") : Tr("PLAY");
        if (slot->installView)
        {
            InstallProgress::State state = InstallSnapshot(*slot).state;
            if (state == InstallProgress::State::Failed)
                return line(S::Failed, built ? Tr("Update failed") : Tr("Install failed"), Danger, button);
            if (state == InstallProgress::State::Interrupted)
                return line(S::Interrupted, Tr("Interrupted"), Accent, button);
        }
        if (NeedsInstall(*slot))
            return line(S::ReadyToInstall, Tr("Ready to install"), Accent, button);
        if (UpdateAvailable(*slot))
            return line(S::UpdateAvailable, Tr("Update available"), Accent, button);
        return line(S::Ready, Tr("Ready to play"), Ready, button);
    }

    ui::Status StatusOf(SlotState state)
    {
        using S = SlotState;
        switch (state)
        {
        case S::Checking: case S::DownloadingTools: case S::Installing: case S::Updating: case S::Finishing: return ui::Status::Busy;
        case S::NoDisc: case S::Interrupted: case S::ReadyToInstall: case S::UpdateAvailable: return ui::Status::Info;
        case S::Ready: case S::Running: return ui::Status::Success;
        default: return ui::Status::Error; // the disc's problems, a damaged copy, a failed build
        }
    }

    ui::Icon IconOf(SlotState state)
    {
        using S = SlotState;
        switch (state)
        {
        case S::NoDisc: return ui::Icon::Disc;
        case S::ReadyToInstall: return ui::Icon::Download;
        case S::UpdateAvailable: return ui::Icon::Update;
        case S::Interrupted: return ui::Icon::Pause;
        case S::Running: return ui::Icon::Play;
        default: return ui::IconOf(StatusOf(state));
        }
    }

    // The game's logo (with "Skylanders" on it) once its disc has been read, else its initials on its colour (at most
    // height::Normal across); centred in the box.
    void App::DrawGameMark(ImDrawList* draw, const LauncherGameEntry& entry, ImVec2 at, ImVec2 size)
    {
        const ArtTexture* logo = entry.enabled ? CachedArt(entry.id, LauncherAssets::Role::Logo, int(size.x * 2.5f)) : nullptr;
        if (logo != nullptr)
        {
            const float h = std::min(size.y, size.x * GameLogoH / GameLogoW);
            DrawLogo(draw, *logo, ImVec2(at.x, at.y + (size.y - h) / 2), ImVec2(size.x, h), 1, false);
            return;
        }
        const ImVec2 centre(at.x + size.x / 2, at.y + size.y / 2);
        const float radius = std::min(size.y, ui::height::Normal) / 2;
        draw->AddCircleFilled(centre, radius, ui::Col(entry.accent, entry.enabled ? 1.0f : 0.35f));
        ImGui::PushFont(ui::Ctx().fonts.title, radius);
        // The first letters of its words, two at most: SA, G, SF.
        char letters[3] = {};
        for (const char* c = entry.name; *c != 0 && letters[1] == 0; c++)
            if (c == entry.name || c[-1] == ' ')
                letters[letters[0] == 0 ? 0 : 1] = *c;
        const ImVec2 extent = ImGui::CalcTextSize(letters);
        draw->AddText(ImVec2(centre.x - extent.x / 2, centre.y - extent.y / 2), Rgb(BadgeInk), letters);
        ImGui::PopFont();
    }

    void App::DrawSidebar()
    {
        // A fixed column (SidebarW wide, never resized or scrolled): the games, then the collection and the settings at the bottom.
        ImVec2 size = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(SidebarW, size.y));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Rgb(Sidebar));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::space::S, ui::space::L));
        ImGui::Begin("##sidebar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushStyleColor(ImGuiCol_NavCursor, 0); // the rows draw their own ring (FocusRing), not ImGui's as well
        ImDrawList* draw = ImGui::GetWindowDrawList();

        // The title: a link to Home for the pointer, not a stop for the keyboard or the pad (no ring: Home is the start page,
        // the games are the rows below). The launcher's version beside it, on its baseline; everything on the rows' left edge.
        const float left = ImGui::GetCursorScreenPos().x + ui::space::S;
        {
            const ImVec2 titleAt(left, ImGui::GetCursorScreenPos().y);
            const std::string version = "v" + LauncherVersion();
            const float versionW = ui::TextSize(ui::TextStyle::Caption, version.c_str(), nullptr, true).x;
            const float room = SidebarW - 2 * ui::space::S - 2 * ui::space::S - versionW - ui::space::XS;
            const float titleSize = ui::FitSize(ui::Ctx().fonts.title, ui::type::Title, "ARKEYAN LAUNCHER", room);
            ImGui::PushFont(ui::Ctx().fonts.title, titleSize);
            const ImVec2 titleExtent = ImGui::CalcTextSize("ARKEYAN LAUNCHER");
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(titleAt);
            if (ImGui::InvisibleButton("##home", titleExtent)) // no ImGuiButtonFlags_EnableNav: the pointer's only
                GoTo(Page::Home);
            const bool hovered = ImGui::IsItemHovered();
            if (hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PushFont(ui::Ctx().fonts.title, titleSize);
            draw->AddText(titleAt, ui::Col(hovered ? 0xFFFFFF : SidebarInk), "ARKEYAN LAUNCHER");
            ImGui::PopFont();
            const float baseline = titleAt.y + ui::Baseline(ui::TextStyle::Title, false, titleSize);
            ui::PushText(ui::TextStyle::Caption, true);
            ui::DrawText(draw, ImVec2(titleAt.x + titleExtent.x + ui::space::XS, baseline - ui::Baseline(ui::TextStyle::Caption, true)), ui::Col(ui::color::Dim), version.c_str());
            ui::PopText();
            // Under it, the tagline; a section's air (L), then the list's overline and the rows.
            float y = titleAt.y + titleExtent.y + ui::space::XXS;
            ui::PushText(ui::TextStyle::Caption);
            ui::DrawText(draw, ImVec2(left, y), ui::Col(ui::color::Dim), Tr("Native PC ports"));
            ui::PopText();
            y += ui::type::Caption + ui::space::L;
            ui::PushText(ui::TextStyle::Caption, true);
            ui::DrawText(draw, ImVec2(left, y), ui::Col(ui::color::Dim), Tr("YOUR GAMES"));
            ui::PopText();
            ImGui::SetCursorScreenPos(ImVec2(left - ui::space::S, y + ui::type::Caption + ui::space::XS));
        }

        // The games: only those with a runtime can be selected. The ones not out yet fold into one row under them.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ui::space::XXS));
        std::vector<const LauncherGameEntry*> later;
        for (const LauncherGameEntry& entry : LauncherGameCatalog(m_config))
        {
            if (!entry.enabled || FindGame(entry.id) == nullptr)
            {
                later.push_back(&entry);
                continue;
            }
            ImGui::PushID(entry.id);
            // Every game has its slot: its icon and disc state show here, not just the shown one's.
            GameSlot* slot = &SlotFor(*FindGame(entry.id));
            // Selected only once the player has picked it: Play is that game's page.
            const bool selected = (m_page == Page::Play || m_page == Page::GameSettings || m_page == Page::Achievements) && std::string_view(entry.id) == m_s->game.id;
            const std::string version = GameVersion(*slot);
            SlotLine line = SlotStatus(slot);
            const ui::Status status = StatusOf(line.state);
            // Fits the column at the smallest window too: the row cuts its lines with "..." rather than run over the edge.
            // Under the name, the state: its icon and its words on one line, on the name's left edge.
            ui::ListRowOptions options;
            options.subtitle = line.text.c_str();
            options.status = &status;
            options.statusIcon = IconOf(line.state);
            options.leadWidth = GameLogoW;
            options.note = version.empty() ? nullptr : version.c_str();
            options.selected = selected;
            options.progress = line.progress;
            options.progressColor = StyleOf(entry.id).top;
            options.imageWidth = GameLogoW;
            options.image = [&](ImDrawList* list, ImVec2 at, ImVec2 box) { DrawGameMark(list, entry, at, box); };
            if (ui::ListRow(entry.name, options) && !m_uninstalled)
            {
                if (std::string_view(entry.id) != m_s->game.id)
                    SelectGame(*FindGame(entry.id));
                GoTo(Page::Play);
            }
            if (std::string_view(entry.id) == m_s->game.id && (m_page == Page::Play || m_page == Page::Home || m_page == Page::GameSettings || m_page == Page::Achievements))
                TakeFocus(FocusTo::Sidebar);
            if (m_testSidebar || m_testRunning)
                m_drawn[std::string("sidebar:") + entry.id] = line.text;
            ImGui::PopID();
        }
        if (!later.empty())
        {
            // One compact row; opened, the games to come are listed under it, quietly.
            ImGuiStorage* storage = ImGui::GetStateStorage();
            const ImGuiID openKey = ImGui::GetID("##coming-later");
            const bool open = storage->GetBool(openKey, m_testExperimental && m_testStep == 0);
            const std::string count = std::to_string(later.size());
            ui::ListRowOptions options;
            options.trailing = count.c_str();
            options.icon = open ? ui::Icon::ChevronDown : ui::Icon::ChevronRight;
            options.leadWidth = GameLogoW;
            options.dim = true;
            if (ui::ListRow(Tr("Coming later"), options))
                storage->SetBool(openKey, !open);
            if (open)
                for (const LauncherGameEntry* entry : later)
                {
                    ui::ListRowOptions game;
                    game.disabled = true;
                    game.dim = true;
                    game.imageWidth = ui::height::Small;
                    game.leadWidth = GameLogoW;
                    game.image = [&](ImDrawList* list, ImVec2 at, ImVec2 box) { DrawGameMark(list, *entry, at, ImVec2(ui::height::Small, std::min(box.y, ui::height::Small))); };
                    ui::ListRow(entry->name, game);
                }
        }

        // The collection, then Settings, at the bottom of the sidebar; selected while their page is shown.
        const float footer = 2 * ui::height::Normal + ui::space::XXS;
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ui::space::L - footer));
        {
            ui::ListRowOptions options;
            options.icon = ui::Icon::Star;
            options.leadWidth = GameLogoW;
            options.selected = m_page == Page::Collection;
            if (ui::ListRow(Tr("Collection"), options))
                GoTo(options.selected ? m_backPage : Page::Collection);
            if (options.selected)
                TakeFocus(FocusTo::Sidebar);
        }
        {
            ui::ListRowOptions options;
            options.icon = ui::Icon::Gear;
            options.leadWidth = GameLogoW;
            options.selected = m_page == Page::Settings;
            if (ui::ListRow(Tr("Settings"), options))
                GoTo(options.selected ? m_backPage : Page::Settings);
            if (options.selected)
                TakeFocus(FocusTo::Sidebar);
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    // The launcher's menu, opened by the ARKEYAN LAUNCHER title: the games as a library (a card each, with its art from the
    // player's disc), the games still to come under them, the project's links bottom right.
    // "Update all" at the right of the Home title when two or more games have an update: one click updates them one after the
    // other. While it runs the game and its progress show in its place, with Cancel.
    void App::DrawUpdateAll(ImVec2 right)
    {
        // On the title's line (its capitals' middle), at the page's right edge.
        const ImVec2 back = ImGui::GetCursorScreenPos();
        const float y = std::round(right.y + ui::CapMiddle(ui::TextStyle::Display) - ui::height::Normal / 2);
        if (UpdatingAll())
        {
            std::string text;
            float progress = -1;
            for (const auto& slot : m_slots)
                if (std::string_view(slot->game.id) == m_updateAll.current)
                {
                    SlotLine line = SlotStatus(slot.get());
                    text = Tf("{}: {} ({} of {})", GameName(slot->game.id, GameNameForm::Short), line.text, m_updateAll.started, m_updateAll.total);
                    progress = line.progress;
                }
            // Which game and how far (its words over a bar, centred on the button's height), then Cancel.
            const float cancelW = ui::ButtonWidth(Tr("Cancel"));
            ImGui::SetCursorScreenPos(ImVec2(right.x - cancelW, y));
            if (ui::Button(Tr("Cancel")))
                CancelUpdateAll();
            const float barH = ui::space::XS, blockH = ui::type::Caption + ui::space::XS + barH;
            const float textW = std::max(ui::TextSize(ui::TextStyle::Caption, text.c_str()).x, 160.0f);
            const ImVec2 at(right.x - cancelW - ui::space::M - textW, std::round(y + (ui::height::Normal - blockH) / 2));
            ui::PushText(ui::TextStyle::Caption);
            ui::DrawText(ImGui::GetWindowDrawList(), at, ui::Col(ui::color::InkSoft), text.c_str());
            ui::PopText();
            ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + ui::type::Caption + ui::space::XS));
            if (progress >= 0)
                ui::ProgressBar(progress, textW);
            else
                ui::IndeterminateBar(textW);
        }
        else if (GamesToUpdate() >= 2)
        {
            ui::ButtonOptions options;
            options.kind = ui::ButtonKind::Primary;
            options.icon = ui::Icon::Download;
            const std::string tooltip = Tf("Updates {} games, one after the other", GamesToUpdate());
            options.tooltip = tooltip.c_str();
            options.disabled = !m_session.Running().empty(); // a game is running: its build can't be replaced
            ImGui::SetCursorScreenPos(ImVec2(right.x - ui::ButtonWidth(Tr("Update all"), options), y));
            if (ui::Button(Tr("Update all"), options))
                StartUpdateAll();
        }
        ImGui::SetCursorScreenPos(back);
    }

    void App::DrawHome()
    {
        constexpr const char* Project = "https://github.com/mflr0/Skylauncher";
        const ImVec2 size = ImGui::GetIO().DisplaySize;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;

        // The page's title, its line under it (XS), the cards a section's air (L) below.
        float top = origin.y;
        ui::PushText(ui::TextStyle::Display);
        ui::DrawText(draw, origin, ui::Col(ui::color::Ink), Tr("Library"));
        ui::PopText();
        top += ui::type::Display + ui::space::XS;
        ui::PushText(ui::TextStyle::Body);
        ui::DrawText(draw, ImVec2(origin.x, top), ui::Col(ui::color::Dim), Tr("Game art comes from your own disc."));
        ui::PopText();
        top += ui::type::Body + ui::space::L;
        ImGui::SetCursorScreenPos(ImVec2(origin.x, top));
        DrawUpdateAll(ImVec2(origin.x + width, origin.y));

        // The playable games: a card each, the game's painting behind its logo, its state at the bottom.
        std::vector<const LauncherGameEntry*> playable, later;
        for (const LauncherGameEntry& entry : LauncherGameCatalog(m_config))
            (entry.enabled && FindGame(entry.id) != nullptr ? playable : later).push_back(&entry);
        const float gap = ui::space::L;
        const float cardW = (width - gap * float(playable.size() - 1)) / float(std::max<size_t>(playable.size(), 1));
        const float below = ui::space::XL + ui::SizeOf(ui::TextStyle::Caption) + 3 * ui::height::Normal; // the coming-later row and the links
        const float cardH = std::min(cardW * 0.85f, size.y - origin.y - below - ui::height::Large);
        const ImVec2 row = ImGui::GetCursorScreenPos();
        // Every card says the game's full name, all at one size: the largest at which the longest fits (never cut, never
        // the short name on one card and the full name on another). A card without the disc's logo shows the short name in
        // its place, like a logo's big word, at one size too.
        float nameSize = ui::type::Label, logoNameSize = ui::type::Display;
        for (const LauncherGameEntry* entry : playable)
        {
            nameSize = std::min(nameSize, ui::FitSize(ui::Ctx().fonts.bold, ui::type::Label, GameName(entry->id), cardW - 2 * ui::space::L));
            logoNameSize = std::min(logoNameSize, ui::FitSize(ui::Ctx().fonts.title, ui::type::Display, GameName(entry->id, GameNameForm::Short), cardW * 0.84f));
        }
        for (size_t i = 0; i < playable.size(); i++)
        {
            const LauncherGameEntry& entry = *playable[i];
            const ImVec2 at(row.x + float(i) * (cardW + gap), row.y), end(at.x + cardW, at.y + cardH);
            ImGui::SetCursorScreenPos(at);
            ImGui::PushID(entry.id);
            const bool pressed = ui::Hit("##card", ImVec2(cardW, cardH));
            const bool hovered = ImGui::IsItemHovered();
            const ui::State state = ui::ItemState();
            if (pressed)
            {
                if (std::string_view(entry.id) != m_s->game.id)
                    SelectGame(*FindGame(entry.id));
                GoTo(Page::Play);
            }
            if (std::string_view(entry.id) == m_s->game.id) // the page's first focus: the game shown last
                TakeFocus(FocusTo::Page);
            if (hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopID();

            const GameStyle& style = StyleOf(entry.id);
            ui::SoftShadow(draw, at, end, ui::radius::Panel, ui::space::XS, ui::space::S, ui::elevation::Raised.shadowAlpha);
            if (const ArtTexture* art = CachedArt(entry.id, LauncherAssets::Role::HeroBackground, 640))
            {
                float w, h;
                SDL_GetTextureSize(art->texture, &w, &h);
                float scale = std::max(cardW / w, cardH / h), u = cardW / (w * scale), v = cardH / (h * scale);
                draw->AddImageRounded(Ref(art->texture), at, end, ImVec2((1 - u) / 2, (1 - v) / 2), ImVec2((1 + u) / 2, (1 + v) / 2),
                    ui::Col(0xFFFFFF, hovered ? 1.0f : 0.88f), ui::radius::Panel);
            }
            else
            {
                // Not read yet: the game's colours, as its buttons have them.
                ui::GradientRect(draw, at, end, Rgb(style.bottom), Rgb(style.outline), ui::radius::Panel);
            }
            // The bottom fades to dark for the name; the logo sits in the upper part.
            ui::GradientRect(draw, ImVec2(at.x, at.y + cardH * 0.45f), end, ui::Col(ui::color::Scrim, 0), ui::Col(ui::color::Scrim, 0.92f), ui::radius::Panel,
                ImDrawFlags_RoundCornersBottom);
            // The logo above the name and the state: what the text takes at the foot is left out of its box.
            const float textH = ui::space::L + ui::PillHeight + ui::space::XS + nameSize + ui::space::S;
            const float logoTop = at.y + ui::space::S, logoH = std::max(0.0f, cardH - textH - ui::space::S);
            if (const ArtTexture* logo = CachedArt(entry.id, LauncherAssets::Role::Logo, 640))
                DrawLogo(draw, *logo, ImVec2(at.x + cardW * 0.12f, logoTop), ImVec2(cardW * 0.76f, logoH));
            else
            {
                // The short name in the title font, at the row's size.
                const char* word = GameName(entry.id, GameNameForm::Short);
                ImGui::PushFont(ui::Ctx().fonts.title, logoNameSize);
                const ImVec2 text = ImGui::CalcTextSize(word);
                draw->AddText(ImVec2(at.x + (cardW - text.x) / 2, logoTop + (logoH - text.y) / 2), ui::Col(ui::color::Ink), word);
                ImGui::PopFont();
            }
            GameSlot* slot = &SlotFor(*FindGame(entry.id));
            const SlotLine line = SlotStatus(slot);
            // The full name, at the row's size, and under it (XS) the state in a word and a shape; the card's padding (L) at
            // the left and the foot.
            const float pillTop = end.y - ui::space::L - ui::PillHeight;
            ImGui::PushFont(ui::Ctx().fonts.bold, nameSize);
            draw->AddText(ImVec2(at.x + ui::space::L, pillTop - ui::space::XS - nameSize), ui::Col(ui::color::Ink), GameName(entry.id));
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(at.x + ui::space::L, pillTop));
            ui::StatusPill(StatusOf(line.state), line.text.c_str(), IconOf(line.state));
            if (m_testSidebar)
                m_drawn[std::string("home:") + entry.id] = line.text;
            // Under the pointer: the game's own colour around the card.
            draw->AddRect(at, end, hovered ? Rgb(style.glow) : ui::Col(ui::color::Ink, 0.11f), ui::radius::Panel, 0, hovered ? ui::stroke::Selection : ui::stroke::Hairline);
            if (state.focused) // around the card (its state pill is the last item drawn)
                ui::FocusRing(at, end, ui::radius::Panel);
        }
        // The games still to come, a section's air (XL) below: their names, in their colours, quietly.
        const float laterTop = row.y + cardH + ui::space::XL;
        ui::PushText(ui::TextStyle::Caption, true);
        ui::DrawText(draw, ImVec2(row.x, laterTop), ui::Col(ui::color::Dim), Tr("COMING LATER"));
        ui::PopText();
        ImGui::SetCursorScreenPos(ImVec2(row.x, laterTop + ui::type::Caption + ui::space::XS));
        for (size_t i = 0; i < later.size(); i++)
        {
            const float w = ui::BadgeSize(later[i]->name).x;
            if (i > 0)
            {
                if (ImGui::GetCursorScreenPos().x + ui::space::XS + w > origin.x + width)
                    ImGui::Dummy(ImVec2(0, 0));
                else
                    ImGui::SameLine(0, ui::space::XS);
            }
            ui::Badge(later[i]->name, ui::Mix(ui::color::Surface, later[i]->accent, 0.35f), ui::color::Ink);
        }

        // The project's links, bottom right: Report a bug, and the GitHub mark.
        const float mark = ui::HeightOf(ui::Size::Normal);
        ui::ButtonOptions report;
        report.overArt = true;
        report.tooltip = Tr("Opens a new issue on GitHub");
        const float reportW = ui::ButtonWidth(Tr("Report a bug"), report);
        const ImVec2 iconAt(size.x - ui::space::XL - mark, size.y - ui::space::XL - mark);
        ImGui::SetCursorScreenPos(ImVec2(iconAt.x - ui::space::S - reportW, iconAt.y));
        if (ui::Button(Tr("Report a bug"), report))
            SDL_OpenURL((std::string(Project) + "/issues/new").c_str());
        ImGui::SetCursorScreenPos(iconAt);
        ui::IconButtonOptions github;
        github.overArt = true;
        github.image = m_github != nullptr ? Ref(m_github) : ImTextureRef();
        if (ui::IconButton("##github", ui::Icon::Info, Tr("Arkeyan Launcher on GitHub"), github))
            SDL_OpenURL(Project);
    }

    float App::PlayBandH() const
    {
        return ui::Ctx().glyphs != ui::Glyphs::Keyboard ? ui::height::Normal : 0.0f;
    }

    void App::DrawPlay()
    {
        RefreshProgress();
        if (m_testHero)
        {
            m_testHeroControls.clear();
            ui::Ctx().testNavigationItems = &m_testHeroControls;
        }
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        m_heroNavigation.Begin(m_focusTo == FocusTo::None);
        const float margin = ui::space::XL;
        const float left = position.x + margin;
        const float innerW = available.x - 2 * margin;
        const float panelW = std::clamp(innerW * 0.32f, ui::hero::PanelMinWidth, ui::hero::PanelWidth);
        const float leftW = innerW - panelW - ui::space::L;
        const float foot = position.y + available.y - margin - PlayBandH();
        const SlotLine state = SlotStatus(m_s);
        if (m_testSidebar)
            m_drawn["button"] = state.button + (state.buttonEnabled ? "" : " (off)");
        const bool running = state.state == SlotState::Running;
        const bool stopping = running && m_session.Stopping();
        const bool showGame = running && (m_testHero || m_session.CanShow());
        const bool installing = Installing() || (m_testHero && m_testHeroState == 2);
        const bool noDisc = state.state == SlotState::NoDisc;
        const bool ready = m_s->disc == DiscState::Ready;
        const bool verifying = state.state == SlotState::Checking;
        const bool install = ready && NeedsInstall();
        const bool update = ready && !install && UpdateAvailable();
        const bool detailed = m_s->installView && (!installing || m_s->installLog);
        const float panelH = panelW < ui::hero::PanelWidth ? ui::hero::CompactPanelHeight : ui::hero::PanelHeight;

        // One lower edge for the portal and the commands; the painting remains visible between the columns.
        float hintH = (running ? 4 : 2) * ui::type::Body + ui::space::XS;
        if (!m_testHero && m_s->disc == DiscState::Failed)
        {
            // The disc's message can run to several lines (the supported releases), and a button may follow it.
            ui::PushText(ui::TextStyle::Caption);
            hintH = std::max(hintH, ImGui::CalcTextSize(m_s->check.message.c_str(), nullptr, false, leftW).y + ui::height::Normal + ui::space::XS);
            ui::PopText();
        }
        const float pillY = foot - ui::height::Small - hintH - ui::space::M;
        const float buttonY = pillY - ui::space::M - (installing ? 2 * ui::height::Normal + ui::space::L : ui::height::Main);
        const float statusY = buttonY - ui::space::L - ui::type::Body;
        const ArtTexture* logo = m_testHeroNoArt ? nullptr : m_s->logo ? &m_s->logo : CachedArt(m_s->game.id, LauncherAssets::Role::Logo, 0);
        const float titleW = std::min(leftW, ui::hero::TitleWidth);
        const char* shortName = GameName(m_s->game.id, GameNameForm::Short);
        const float titleH = logo != nullptr ? ui::hero::LogoHeight : ui::HeroHeadingHeight(titleW, shortName);
        const ImVec2 titleAt(left, statusY - ui::space::M - titleH);
        if (!detailed)
        {
            if (logo != nullptr)
            {
                const float logoW = std::min(leftW, ui::hero::LogoHeight * logo->aspect);
                DrawHeroLogo(draw, *logo, titleAt, ImVec2(logoW, ui::hero::LogoHeight));
            }
            else
                ui::HeroHeading(draw, titleAt, ImVec2(titleW, titleH), "Skylanders", shortName);

            std::string status = noDisc ? Tr("Not installed yet") : running ? Tr("Playing now") : state.text;
            if (stopping) status = Tr("Stopping...");
            if (installing)
            {
                const InstallChecklist list = MakeInstallChecklist(m_testHero ? m_testHeroProgress : InstallSnapshot(), SDL_GetTicks());
                const std::string eta = InstallTimeLeft(list.etaMs);
                if (!eta.empty()) status = Tf("Installing · {}", eta);
            }
            if (state.state == SlotState::Ready && m_progress.play.totalSeconds > 0)
            {
                status = Tf("Ready · {} played", FormatPlayTime(m_progress.play.totalSeconds));
                if (m_progress.play.lastEnd > 0)
                    status += " · " + Tf("Last played: {}", FormatLastPlayed(m_progress.play.lastEnd, int64_t(std::time(nullptr))));
            }
            ImGui::SetCursorScreenPos(ImVec2(left, statusY));
            ui::StatusLine(noDisc ? ui::Status::Warning : StatusOf(state.state), status.c_str(), leftW);

            if (installing)
                DrawHeroInstall(ImVec2(left, buttonY), leftW);
            else
            {
                MainAction action{running ? Tr("Stop") : state.button.c_str(), state.buttonEnabled && !m_testButtonsOff};
                action.icon = running ? ui::Icon::Cross : noDisc ? ui::Icon::Disc : ui::Icon::Play;
                action.danger = running;
                action.width = std::min(ui::height::MainWidth, leftW - ui::height::MainGear - ui::space::S);
                if (running)
                {
                    action.enabled = (m_testHero || m_session.Ours()) && !stopping && !m_testButtonsOff;
                    if (showGame)
                        action.width = std::min(ui::height::MainWidth, std::max(ui::height::MainGear, leftW - ui::height::MainGear - 3 * ui::space::S
                            - ui::ButtonWidth(Tr("Show the game"), { .size = ui::Size::Large })));
                }
                if (verifying)
                {
                    action.progress = m_s->verify.progress;
                    action.indeterminate = m_s->verify.progress >= 0.97f;
                }
                const MainActionHit hit = DrawMainAction(ImVec2(left, buttonY), action);
                if (m_testRunning)
                {
                    m_drawn["hero-main"] = std::string(action.label) + (action.enabled ? "" : " (off)");
                    m_drawn["hero-show"] = showGame ? "shown" : "hidden";
                }
                if (hit == MainActionHit::Settings)
                    GoTo(Page::GameSettings);
                else if (hit == MainActionHit::Main && !m_testHero)
                {
                    if (running) m_session.Stop(SDL_GetTicks());
                    else if (!ready) Browse();
                    else if (install || update) StartInstall();
                    else PlayNow();
                }
                if (showGame)
                {
                    ImGui::SetCursorScreenPos(ImVec2(left + action.width + ui::space::S + ui::height::MainGear + ui::space::S, buttonY));
                    if (ui::Button(Tr("Show the game"), { .size = ui::Size::Large, .overArt = true }) && !m_testHero)
                        if (!m_session.Show()) Toast(Tr("Couldn't show the game window."), ui::ToastKind::Error);
                }
            }
        }
        else
            DrawInstall(ImVec2(left, position.y + margin), ImVec2(leftW, pillY - position.y - margin - ui::space::M));

        // These are the same device choices and portal settings as the previous page.
        const std::string device = CurrentDevice();
        const std::string deviceName = device == "auto" ? Tr("Automatic") : device == "keyboard" ? Tr("Keyboard") : LauncherMiddleEllipsis(DeviceLabel(), 13);
        const std::string pickerId = deviceName + "##controller";
        const std::string pickerTip = Tf("Controller: {}", DeviceLabel());
        ui::PillOptions picker;
        picker.icon = ui::Icon::Gamepad;
        picker.trailing = ui::Icon::ChevronDown;
        picker.overArt = true;
        picker.tooltip = pickerTip.c_str();
        const float pickerW = std::min(ui::PillWidth(pickerId.c_str(), picker), ControllerW);
        picker.width = pickerW;
        const launcher_portal::Status portal = m_portal.Current();
        const std::string portalId = std::string(portal.connected ? Tr("Portal") : Tr("Virtual portal")) + "##portal";
        const std::string portalTip = portal.Line() + "\n" + Tr(strings::PortalTip);
        ui::PillOptions portalPill;
        portalPill.icon = ui::IconOf(ui::Status::Success);
        portalPill.iconColor = ui::ColorOf(ui::Status::Success);
        portalPill.trailing = ui::Icon::ChevronDown;
        portalPill.overArt = true;
        portalPill.tooltip = portalTip.c_str();
        // The controller picker: what the game listens to, a menu of the choices above it (the pad icon says what it picks, the
        // tooltip says it in words).
        ImGui::SetCursorScreenPos(ImVec2(left, pillY));
        if (ui::Pill(pickerId.c_str(), picker) || m_testControllers)
        {
            m_testControllers = false;
            RefreshGamepads();
            ui::OpenMenu("##controllers", {.above = true});
        }
        // PLAY WITH: the choices, the one in use checked.
        if (ui::BeginMenu("##controllers"))
        {
            ui::Text(ui::TextStyle::Caption, Tr(strings::PlayWith), ui::color::Dim, -1, true);
            for (const auto& [value, name] : DeviceOptions())
            {
                ui::PushText(ui::TextStyle::Body);
                const std::string shown = ui::Ellipsize(name.c_str(), ControllerW + ui::space::XXL);
                ui::PopText();
                if (ui::MenuItem((shown + "##" + value).c_str(), { .checked = value == device }))
                    SetDevice(value);
            }
            ui::EndMenu();
        }
        // The portal's state, in a word and a shape: a real portal plugged in, or not (the game then has its virtual one).
        {
            ImGui::SetCursorScreenPos(ImVec2(left + pickerW + ui::space::S, pillY));
            if (ui::Pill(portalId.c_str(), portalPill))
            {
                m_tab = SettingsTab::Controls;
                GoTo(Page::Settings);
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(left, pillY + ui::height::Small + ui::space::M));
        if (noDisc)
            ui::Text(ui::TextStyle::Body, Tr("Or drop your .iso anywhere on this window. Your disc is read on this computer only."), ui::color::InkSoft, leftW);
        else if (installing)
            ui::Text(ui::TextStyle::Body, Tr("You can set up your portal while you wait."), ui::color::InkSoft, leftW);
        else if (running)
        {
            ui::Text(ui::TextStyle::Body, Tr("Toy Box in the game: F6, or View + Y on a controller."), ui::color::InkSoft, leftW);
            ImGui::SetCursorScreenPos(ImVec2(left, ImGui::GetCursorScreenPos().y));
            ui::Text(ui::TextStyle::Caption, Tr("Close or switch to the game in its own window."), ui::color::Dim, leftW);
        }
        else if (!m_testHero && m_s->disc == DiscState::Failed)
        {
            ui::Text(ui::TextStyle::Caption, m_s->check.message.c_str(), ui::color::Danger, leftW);
            if (const GameInfo* other = FindEnabledGame(m_s->check.otherGame, m_config); other != nullptr && m_s->check.status == GameSourceStatus::WrongGame)
            {
                ImGui::SetCursorScreenPos(ImVec2(left, ImGui::GetCursorScreenPos().y));
                if (ui::Button(Tf("Use it for {}", other->name).c_str(), { .kind = ui::ButtonKind::Primary }))
                    UseDiscForOtherGame();
            }
        }
        else if (update && !installing && state.buttonEnabled)
        {
            if (const GameUpdate* pending = PendingUpdate(*m_s))
            {
                std::string size;
                if (KitNeedsFetch(*m_s))
                {
                    const ReleaseInfo kit = m_updates.Snapshot().kit;
                    if (const ReleaseAsset* asset = FindAsset(kit, KitAssetName()); asset != nullptr && asset->size > 0)
                        size = LauncherFormatSize(asset->size);
                }
                const std::string offer = size.empty() ? Tf("{} · ~{} min", pending->version, InstallMinutesFor(m_s->game.id))
                    : Tf("{} · {} · ~{} min", pending->version, size, InstallMinutesFor(m_s->game.id));
                ui::Text(ui::TextStyle::Caption, offer.c_str(), ui::color::InkSoft);
            }
            ImGui::SetCursorScreenPos(ImVec2(left, ImGui::GetCursorScreenPos().y));
            if (ui::Button(Tr("Play without updating"), { .kind = ui::ButtonKind::Ghost, .size = ui::Size::Small, .overArt = true }) || m_testPlayWithout)
            {
                m_testPlayWithout = false;
                m_s->skipUpdate = true;
            }
        }

        if (ready && !installing && !running && !m_s->fromCopy)
        {
            ImGui::SetCursorScreenPos(ImVec2(left, position.y + margin));
            if (ui::Button(Tr("Change disc..."), { .kind = ui::ButtonKind::Ghost, .size = ui::Size::Small, .overArt = true }))
                Browse();
            ui::Tooltip(m_s->discPath.c_str());
        }
        if (ready && !installing && !running && install)
        {
            ImGui::SetCursorScreenPos(ImVec2(left, pillY + ui::height::Small + ui::space::M));
            ui::Text(ui::TextStyle::Caption, Tf("Builds the game on your PC: about {} min, {} GB, internet needed once.",
                InstallMinutesFor(m_s->game.id), InstallGigabytes).c_str(), ui::color::InkSoft, leftW);
        }
        if (ready && !m_s->fromCopy && m_s->copy.state == gamedata::State::Damaged)
        {
            ImGui::SetCursorScreenPos(ImVec2(left, position.y + margin + ui::height::Normal));
            ui::StatusPill(ui::Status::Error, Tr("Game data damaged: using the disc image."));
        }

        m_heroNavigation.Column(1);
        if (noDisc)
            DrawHeroSteps(ImVec2(left + leftW + ui::space::L, foot - panelH), ImVec2(panelW, panelH));
        else
            m_s->collection->DrawHeroPortal(ImVec2(left + leftW + ui::space::L, foot - panelH), ImVec2(panelW, panelH), running);

        m_heroNavigation.Column(0);
        // Achievements keep their entry point over the painting.
        if (!m_s->installView && m_progress.hasList)
        {
            const std::string count = fmt::format("{}/{}", m_progress.unlocked, m_progress.rows.size());
            ui::PillOptions progress;
            progress.icon = ui::Icon::Trophy;
            progress.overArt = true;
            progress.tooltip = Tr("Achievements");
            const std::string id = count + "##progress";
            ImGui::SetCursorScreenPos(ImVec2(position.x + available.x - margin - ui::PillWidth(id.c_str(), progress), position.y + margin));
            if (ui::Pill(id.c_str(), progress)) GoTo(Page::Achievements);
        }
        if (ui::Ctx().glyphs != ui::Glyphs::Keyboard)
        {
            const ui::PadHint hints[] = {
                { ui::PadButton::South, "Enter", Tr("Select") },
                { ui::PadButton::North, nullptr, Tr("Game settings") },
                { ui::PadButton::East, "Esc", Tr("Sidebar") },
            };
            ImGui::SetCursorScreenPos(ImVec2(position.x, position.y + available.y - ui::height::Normal));
            ui::PadLegend(hints, ui::Ctx().glyphs, available.x);
        }
        m_heroNavigation.End();
        ui::Ctx().testNavigationItems = nullptr;
    }

    void App::DrawPortalStatus()
    {
        const std::string line = m_portal.Current().Line();
        if (line.empty())
            return;
        ui::StatusPill(m_portal.Current().connected ? ui::Status::Success : ui::Status::Neutral, line.c_str());
        ui::Tooltip(Tr(strings::PortalTip));
    }

    void App::DrawControls()
    {
        const std::string current = CurrentDevice();
        const size_t gamepads = m_gamepads.size();
        const std::string connected = gamepads == 0 ? Tr(strings::NoGamepad) : gamepads == 1 ? Tr(strings::OneGamepad) : Tf(strings::Gamepads, gamepads);

        if (ui::BeginSettings("##controller"))
        {
            ui::SettingRow(Tr("Controller"), Tf("{} Xbox, PlayStation and Switch pads work.", connected).c_str());
            if (ui::BeginSelect("##device", DeviceLabel().c_str()))
            {
                for (const auto& [value, label] : DeviceOptions())
                    if (ui::SelectOption(label.c_str(), value == current))
                        SetDevice(value);
                ui::EndSelect();
            }
            ui::SettingRow(Tr("Button glyphs"), nullptr);
            SettingSelect("##glyphs", "input", "glyphs", GlyphChoices, 0);
            ui::EndSettings();
        }

        ui::SectionHeader(Tr("Portal of Power"));
        if (ui::BeginSettings("##portal"))
        {
            ui::SettingRow(Tr("Portal"), nullptr);
            SettingSelect("##portal", "portal", "mode", PortalChoices, 0);
            ui::SettingRow(Tr("Auto-open Toy Box"), nullptr);
            bool autoOpen = ParseBool(SettingGet(m_config, "portal", "toybox_auto_open", "1"));
            if (ui::Toggle("##toyboxautoopen", &autoOpen))
                Set("portal", "toybox_auto_open", autoOpen ? "1" : "0");
            DrawPortalTest();
            ui::EndSettings();
        }
        DrawPortalResult();

        ui::SectionHeader(Tr("Toy Box keys"));
        if (ui::BeginSettings("##toyboxkeys"))
        {
            // Each line: the keyboard's keys ("F6 / Tab": either), then the pad's buttons ("View + Y": together) in the pad's own
            // pictures (the pad in use or chosen, else Xbox's), a dash where the pad has none (F7 and F8 are the keyboard's
            // only). The pad's column ends on the control column's edge, the keys' column before it: two columns.
            struct Line
            {
                const char* label;
                std::vector<const char*> keys;
                std::vector<ui::PadButton> buttons;
            };
            const Line lines[] = {
                { Tr("Open the Toy Box"), { "F6", "Tab" }, { ui::PadButton::Back, ui::PadButton::North } },
                { Tr("Add a figure"), { "F7" }, {} },
                { Tr("Clear the figures"), { "F8" }, {} },
            };
            const ui::Glyphs glyphs = PadGlyphs();
            const float joint = ui::space::XS; // on each side of the "/" or the "+"
            auto separator = [](bool both) { return both ? "+" : "/"; };
            auto groupWidth = [&](const Line& line, bool pad) {
                const size_t count = pad ? line.buttons.size() : line.keys.size();
                float w = 0;
                for (size_t i = 0; i < count; i++)
                    w += (pad ? ui::KeyCapWidth(line.buttons[i], glyphs) : ui::KeyCapWidth(line.keys[i])) +
                        (i > 0 ? 2 * joint + ui::TextSize(ui::TextStyle::Caption, separator(pad)).x : 0);
                return w;
            };
            float padColumn = 0;
            for (const Line& line : lines)
                padColumn = std::max(padColumn, groupWidth(line, true));
            for (const Line& line : lines)
            {
                ui::SettingRow(line.label, nullptr);
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const ImVec2 start = ImGui::GetCursorScreenPos();
                const float right = start.x + ImGui::GetContentRegionAvail().x, padX = right - padColumn;
                const float capH = ui::KeyCapHeight(), middle = start.y + std::round(capH / 2);
                auto group = [&](float x, bool pad) {
                    const size_t count = pad ? line.buttons.size() : line.keys.size();
                    for (size_t i = 0; i < count; i++)
                    {
                        if (i > 0)
                        {
                            ui::PushText(ui::TextStyle::Caption);
                            ui::DrawText(draw, ImVec2(x + joint, std::round(middle - ui::CapMiddle(ui::TextStyle::Caption))), ui::Col(ui::color::Dim), separator(pad));
                            x += 2 * joint + ImGui::CalcTextSize(separator(pad)).x;
                            ui::PopText();
                        }
                        x += (pad ? ui::DrawKeyCap(draw, ImVec2(x, middle), line.buttons[i], glyphs) : ui::DrawKeyCap(draw, ImVec2(x, middle), line.keys[i])).x;
                    }
                };
                const float keysX = padX - (padColumn > 0 ? ui::space::L : 0) - groupWidth(line, false);
                group(keysX, false);
                if (!line.buttons.empty())
                    group(padX, true);
                else // no pad button for it: a quiet dash in the pad's column
                {
                    ui::PushText(ui::TextStyle::Body);
                    const char* none = "\xE2\x80\x94";
                    ui::DrawText(draw, ImVec2(std::round(padX + (padColumn - ImGui::CalcTextSize(none).x) / 2), std::round(middle - ui::CapMiddle(ui::TextStyle::Body))),
                        ui::Col(ui::color::Dim), none);
                    ui::PopText();
                }
                ImGui::SetCursorScreenPos(ImVec2(keysX, start.y));
                ImGui::Dummy(ImVec2(right - keysX, capH));
            }
            ui::EndSettings();
        }
    }

    // The pictures of the pad's buttons where the window names them whatever the input in use (the Toy Box keys): the pad in
    // use or the one chosen, else Xbox's.
    ui::Glyphs App::PadGlyphs() const
    {
        const PadFamily family = PadFamilyNow();
        return family == PadFamily::PlayStation || family == PadFamily::PlayStation3 ? ui::Glyphs::PlayStation
            : family == PadFamily::Nintendo ? ui::Glyphs::Nintendo : ui::Glyphs::Xbox;
    }

    // The tabs of a settings page: words on one line, an accent line under the selected one (like the sidebar's bar).
    // LB / RB change tab, as in the console launchers.
    bool App::DrawTabBar(const char* const* labels, int count, int* current)
    {
        std::vector<const char*> shown;
        for (int i = 0; i < count; i++)
            shown.push_back(Tr(labels[i]));
        ui::TabsOptions options;
        options.focusSelected = m_focusTo == FocusTo::Page;
        options.showFocus = m_focusShow;
        if (options.focusSelected)
            m_focusTo = FocusTo::None;
        ui::TabChange change = ui::Tabs("##tabs", shown, current, options);
        if (change == ui::TabChange::Bumper)
            RequestFocus(FocusTo::Page); // the ring follows to the new tab
        ImGui::Dummy(ImVec2(0, ui::space::XXS));
        if (change != ui::TabChange::None)
        {
            m_capturing = nullptr;
            m_s->saveBackupsLoaded = m_s->dataSizesLoaded = false; // the saves and the folders may have changed since the tab was open
        }
        return change != ui::TabChange::None;
    }

    // The launcher's settings, apart from any game's: the controller (all games read the same), the launcher
    // itself, support and About. Each game has its own settings page (game_settings_page.cpp).
    void App::BeginSettingsPage(const char* id, const char* title, bool back)
    {
        const float available = ImGui::GetContentRegionAvail().x;
        const float width = std::min(available, ui::width::Page);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) / 2);
        BeginPageColumn(id, width, true); // the scrollbar is always there: the card is as wide on every tab
        const float top = ImGui::GetCursorPosY();
        ui::Text(ui::TextStyle::Display, title, ui::color::Ink, -1);
        const float bottom = ImGui::GetCursorPosY();
        if (back)
        {
            ui::ButtonOptions options;
            options.kind = ui::ButtonKind::Ghost;
            options.icon = ui::Icon::ChevronLeft;
            ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - ui::ButtonWidth(Tr("Back"), options), // the column's right edge, scrollbar or not
                std::round(top + ui::CapMiddle(ui::TextStyle::Display) - ui::height::Normal / 2))); // on the title's capitals
            if (ui::Button(Tr("Back"), options))
                GoTo(Page::Play);
            ImGui::SetCursorPosY(std::max(bottom, ImGui::GetCursorPosY()));
        }
        ImGui::Dummy(ImVec2(0, ui::space::XS));
    }

    // The launcher's settings, apart from any game's: the controller (all games read the same), the launcher
    // itself, support and About. Each game has its own settings page (game_settings_page.cpp).
    void App::DrawSettings()
    {
        // A centred column: settings read badly when they stretch across a wide window.
        BeginSettingsPage("##settingscolumn", Tr("Launcher settings"), false);

        static const char* const Tabs[] = { N_("Controls"), N_("Launcher"), N_("Support"), N_("About") };
        int tab = int(m_tab);
        DrawTabBar(Tabs, int(std::size(Tabs)), &tab);
        m_tab = m_uninstalled ? SettingsTab::Launcher : SettingsTab(tab); // once uninstalled the window shows the result only
        BeginCard("##settings", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        switch (m_tab)
        {
        case SettingsTab::Controls: DrawControls(); break;
        case SettingsTab::Launcher: DrawLauncherTab(); break;
        case SettingsTab::Support: DrawSupport(); break;
        case SettingsTab::About: DrawAbout(); break;
        }
        EndCard();
        if (m_testUninstall && m_testScrollEnd)
            ImGui::SetScrollHereY(1.0f); // the screenshot test shows the bottom of the tab
        EndSettingsPage();
    }

    void App::DrawLauncherTab()
    {
        if (m_un.stage != UninstallStage::Closed)
        {
            DrawUninstall(); // the list and the result take the tab
            return;
        }
        DrawLauncherLanguage();
        if (ui::BeginSettings("##experimental"))
        {
            bool enabled = ExperimentalTrapTeam(m_config);
            ui::SettingRow(Tr("Trap Team (experimental)"), Tr("Show Trap Team in your games."));
            if (ui::Toggle("##trapteam", &enabled))
                SetExperimentalTrapTeam(enabled);
            ui::EndSettings();
        }
        ui::SectionHeader(Tr("Updates"));
        DrawUpdates();
        ui::SectionHeader(Tr("Data folder"), Tr(strings::DataFolderHint));
        const std::string dataPath = datadir::Text(LauncherDataFolder()); // in the tooltip, not in the text
        ui::ButtonOptions folder;
        folder.icon = ui::Icon::Folder;
        folder.tooltip = dataPath.c_str();
        if (ui::Button(Tr("Show in folder"), folder))
        {
            std::error_code error;
            std::filesystem::create_directories(LauncherDataFolder(), error);
            OpenFolder(LauncherDataFolder());
        }
        DrawUninstallSection();
    }

    void App::SetExperimentalTrapTeam(bool enabled)
    {
        Set("launcher", "experimental_trapteam", enabled ? "1" : "0");
        if (enabled)
        {
            SeedGameVideo(m_config, "trapteam");
            SeedGameInput(m_config, "trapteam");
            if (m_shots.empty())
                m_discQueue.emplace_back("trapteam");
        }
        else
        {
            std::erase(m_discQueue, "trapteam");
            if (std::string_view(m_s->game.id) == "trapteam")
            {
                SelectGame(*FindGame("ssa"));
                m_backPage = Page::Home;
            }
        }
    }

    namespace
    {
        // The system's languages as the launcher's codes, most wanted first ("nb" and "nn" are the disc's "no").
        std::vector<std::string> SystemLanguages()
        {
            std::vector<std::string> codes;
            int count = 0;
            if (SDL_Locale** locales = SDL_GetPreferredLocales(&count))
            {
                for (int i = 0; i < count; i++)
                {
                    std::string code = locales[i]->language != nullptr ? locales[i]->language : "";
                    codes.push_back(code == "nb" || code == "nn" ? "no" : code);
                }
                SDL_free(locales);
            }
            return codes;
        }
    }

    // The language of the launcher's text: SKY_LAUNCHER_LANGUAGE (tests, screenshots), else [launcher] language: a code is that
    // language, and nothing (the default) is the system's. Never the shown game's.
    void App::UpdateLanguage()
    {
        static const std::vector<std::string> system = SystemLanguages();
        const char* forced = LauncherEnv("SKY_LAUNCHER_LANGUAGE");
        std::string setting = forced != nullptr && *forced != 0 ? forced : SettingGet(m_config, "launcher", "language");
        i18n::SetLanguage(i18n::Resolve(setting, system));
    }

    void App::DrawLauncherLanguage()
    {
        const std::string setting = SettingGet(m_config, "launcher", "language");
        std::string shown = i18n::Current();
        for (const i18n::Language& language : i18n::Languages())
            if (language.code == i18n::Current())
                shown = language.name;
        if (setting.empty())
            shown = Tr("System default");
        if (!ui::BeginSettings("##launcherlanguage"))
            return;
        ui::SettingRow(Tr("Language"), Tr(strings::LauncherLanguageHint));
        if (ui::BeginSelect("##launcherlanguage", shown.c_str()))
        {
            if (ui::SelectOption(Tr("System default"), setting.empty()))
                Set("launcher", "language", "");
            for (const i18n::Language& language : i18n::Languages())
                if (ui::SelectOption(language.name.c_str(), language.code == setting))
                    Set("launcher", "language", language.code);
            ui::EndSelect();
        }
        ui::EndSettings();
    }

    void App::DrawSupport()
    {
        DrawLogs();
    }

    void App::GoTo(Page page)
    {
        if (m_uninstalled)
            return; // nothing else to do in a launcher that has removed itself (and nothing to write back)
        if ((page == Page::Settings || page == Page::Collection) && (m_page == Page::Home || m_page == Page::Play))
            m_backPage = m_page;
        else if (page == Page::GameSettings || page == Page::Achievements)
            m_backPage = Page::Play; // a game's settings and achievements are opened from its page, and close back to it
        else if (page == Page::Settings && (m_page == Page::GameSettings || m_page == Page::Achievements))
            m_backPage = Page::Play;
        else if (page == Page::Collection && (m_page == Page::GameSettings || m_page == Page::Achievements))
            m_backPage = Page::Play;
        m_page = page;
        m_capturing = nullptr;
        RequestFocus(FocusTo::Page);
    }

    // B / Escape when nothing else took it: out of Settings and Collection, else to the sidebar.
    void App::Back()
    {
        if (m_page == Page::Settings || m_page == Page::Collection || m_page == Page::GameSettings || m_page == Page::Achievements)
            GoTo(m_backPage);
        else
            RequestFocus(FocusTo::Sidebar);
        m_focusShow = true;
    }

    void App::TakeFocus(FocusTo where)
    {
        if (m_focusTo != where)
            return;
        ImGui::SetWindowFocus();
        ImGui::FocusItem();
        if (m_focusShow)
            ImGui::SetNavCursorVisible(true);
        m_focusTo = FocusTo::None;
    }

    constexpr float ToastWidth = 420; // the longest line of a message before it wraps

    void App::Toast(std::string text, ui::ToastKind kind) { m_toasts.Push(std::move(text), kind); }

    void App::DrawToast()
    {
        // Above the game page's band and Home's buttons, so no control is covered; a message closed with the keyboard or the
        // pad gives the focus back to the page.
        float wrap = std::min(ToastWidth, ImGui::GetIO().DisplaySize.x - SidebarW - 2 * Pad - 80);
        if (m_toasts.Draw(m_page == Page::Play ? PlayBandH() : m_page == Page::Home ? BottomBarH : 0.0f, wrap))
            RequestFocus(FocusTo::Page);
    }

    // Quitting during an install: keep installing (the default, B / Escape) or quit.
    void App::DrawQuitPrompt()
    {
        if (!m_quitPrompt)
            return;
        if (!AnyInstalling()) // it ended meanwhile: nothing to lose any more
        {
            m_running = false;
            return;
        }
        // Keep installing is the default (the focus starts there, B / Escape keep installing); Quit is the danger.
        bool open = true;
        const bool shown = ui::BeginModal("##quit", Tr(strings::QuitTitle), &open, ui::ModalSize::Medium); // its title on one line in French too
        if (shown)
        {
            ui::Text(ui::TextStyle::Body, Tr(strings::QuitText), ui::color::InkSoft);
            switch (ui::ModalActions(&open, Tr(strings::Quit), Tr(strings::KeepInstalling), ui::ModalKind::Danger))
            {
            case ui::ModalResult::Confirm:
                m_running = false;
                break;
            default:
                break;
            }
            ui::EndModal();
        }
        if (!open && m_running)
            m_quitPrompt = false;
    }

    void App::Frame()
    {
        UpdateLanguage();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        // The launcher's points are m_uiScale window units: a smaller page, drawn (and its fonts rasterized) denser.
        ImGuiIO& frameIo = ImGui::GetIO();
        frameIo.DisplaySize = ImVec2(frameIo.DisplaySize.x / m_uiScale, frameIo.DisplaySize.y / m_uiScale);
        frameIo.DisplayFramebufferScale = ImVec2(frameIo.DisplayFramebufferScale.x * m_uiScale, frameIo.DisplayFramebufferScale.y * m_uiScale);
        if (SDL_GetMouseFocus() == nullptr && !m_testSeatX) // the backend's outside-the-window position is in unscaled window units
            frameIo.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        if (m_testHero || m_testNav || m_testSeatX || m_testPadInput)
            frameIo.BackendFlags |= ImGuiBackendFlags_HasGamepad; // the test's pad keys count with no pad plugged in
        ImGui::NewFrame();
        // The components' context: the time, the focus ring in the shown game's colour on its pages, the pad family in use.
        ui::NewFrame(SDL_GetTicks());
        ui::Ctx().accent = m_page == Page::Play || m_page == Page::GameSettings ? StyleOf(m_s->game.id).glow : Accent;
        ui::Ctx().reduceMotion = ReduceMotion();
        if (const int view = int(m_page) * 100 + (m_page == Page::GameSettings ? int(m_gameTab) : m_page == Page::Settings ? int(m_tab) : 0);
            m_testPadInput && m_testScreens == 0 && view != m_testFocusView)
        {
            // A script's page (or tab) shown as a pad player arrives there: the focus on its first stop, the ring shown.
            m_testFocusView = view;
            m_focusTo = FocusTo::Page;
            m_focusShow = true;
            ImGui::SetNavCursorVisible(true);
        }
        if (m_gamepads.empty() && !m_testNav && !m_testPadInput && !CollectionPage::TestPad())
            ui::Ctx().glyphs = ui::Glyphs::Keyboard;
        else
            ui::Ctx().glyphs = PadGlyphs();
        if (m_testGallery)
        {
            DrawGallery();
            ImGui::Render();
            ImGuiIO& galleryIo = ImGui::GetIO();
            SDL_SetRenderScale(m_renderer, galleryIo.DisplayFramebufferScale.x, galleryIo.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColor(m_renderer, 30, 30, 36, 255);
            SDL_RenderClear(m_renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
            if (!m_shots.empty() && m_testAt != 0 && SDL_GetTicks() >= m_testAt)
            {
                Screenshot(m_testShot.c_str());
                m_testAt = 0;
                m_testStep++;
            }
            SDL_RenderPresent(m_renderer);
            return;
        }
        if (!m_backTaken && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
            Back();
        DrawBackdrop();
        DrawSidebar();

        ImVec2 size = ImGui::GetIO().DisplaySize;
        m_bannerH = BannerHeight(); // an update on offer, or being installed: a band above the page
        if (m_bannerH > 0)
            DrawUpdateBanner(m_bannerH);
        ImGui::SetNextWindowPos(ImVec2(SidebarW, m_bannerH));
        ImGui::SetNextWindowSize(ImVec2(size.x - SidebarW, size.y - m_bannerH));
        // Play fills the page to its edges; Settings sits inside the usual margins.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, m_page == Page::Play ? ImVec2(0, 0) : ImVec2(Pad + 8, Pad + 4));
        ImGui::Begin("##page", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        switch (m_page)
        {
        case Page::Home: DrawHome(); break;
        case Page::Play: DrawPlay(); break;
        case Page::Collection: m_s->collection->Draw(); break;
        case Page::Settings: DrawSettings(); break;
        case Page::GameSettings: DrawGameSettings(); break;
        case Page::Achievements: DrawAchievements(); break;
        }
        ImGui::End();
        ImGui::PopStyleVar();
        m_lastPage = m_page; // LogoDrop: a new arrival on the game page starts the logo's fall
        DrawWhatsNew();
        DrawProblemReport();
        DrawQuitPrompt();
        DrawToast();
        // Who answers B / Escape next frame: an open menu or dialog, a field being edited, the collection's search or View.
        m_backTaken = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || ImGui::IsAnyItemActive() ||
            m_toasts.HasFocus() || m_reportOpen || m_whatsNewOpen || m_quitPrompt || (m_page == Page::Collection && m_s->collection->TakesBack());
        m_focusTo = FocusTo::None; // a request no page answered (its item wasn't drawn) is dropped

        ImGui::Render();
        // ImGui works in window points: scale them to the output's pixels (Retina and other high-density displays).
        ImGuiIO& io = ImGui::GetIO();
        SDL_SetRenderScale(m_renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(m_renderer, 11, 15, 46, 255);
        SDL_RenderClear(m_renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
        if (!m_shots.empty() && m_testAt != 0 && SDL_GetTicks() >= m_testAt)
        {
            static const char* const Names[] = { "01-no-iso", "02-verified", "03-game-video", "04-controls", "05-collection", "06-home",
                "07-figure-sheet", "08-new-figure", "09-play-with", "10-language", "11-game-disc", "12-game-saves", "13-game-data",
                "14-launcher-settings", "15-disc-gone", "16-logo-drop",
                "17-launcher-support", "18-launcher-about", "19-problem-report" };
            static const char* const InstallNames[] = { "10-install", "11-installing", "12-quit-while-installing", "13-install-details",
                "14-install-failed", "15-installed", "16-game-data", "17-install-interrupted" };
            static const char* const UpdateNames[] = { "20-update-banner", "21-whats-new", "22-about", "23-about-checked", "24-update-failed" };
            static const char* const NavNames[] = { "40-nav-start", "41-nav-sidebar", "42-nav-down", "43-nav-collection", "44-nav-search",
                "45-nav-search-left", "46-nav-seat", "47-nav-choosing", "48-nav-card", "49-nav-placed", "50-nav-sheet",
                "51-nav-back-to-play", "52-nav-settings-controls", "53-nav-settings-back" };
            static const char* const UninstallNames[] = { "01-settings", "02-list", "03-ticked", "04-confirm", "05-result" };
            if (m_testExperimental || m_testHero || m_testButtons || m_testSidebar || m_testCopy || m_testLogo || m_testKeys || m_testPad || m_testAchievements || m_testRunning ||
                m_testScreens != 0 || !m_testKit.empty())
                Screenshot(m_testShot.c_str());
            else if (m_testUninstall)
                Screenshot(UninstallNames[std::min<int>(m_testStep, int(std::size(UninstallNames)) - 1)]);
            else if (m_testNav)
                Screenshot(NavNames[std::min<int>(m_testStep, int(std::size(NavNames)) - 1)]);
            else if (m_testSeatX)
                Screenshot(fmt::format("60-seat-x-{:02}", m_testStep).c_str());
            else if (m_testCollection)
                Screenshot(CollectionPage::TestShotName(m_testStep));
            else if (m_testReal)
                Screenshot(fmt::format("30-real-{:02}", m_testStep).c_str());
            else if (m_testUpdate)
                Screenshot(UpdateNames[std::min<int>(m_testStep, int(std::size(UpdateNames)) - 1)]);
            else if (m_testInstall)
                Screenshot(InstallNames[std::min<int>(m_testStep, int(std::size(InstallNames)) - 1)]);
            else
                Screenshot(Names[std::min<int>(m_testStep, int(std::size(Names)) - 1)]);
            m_testAt = 0;
            m_testStep++;
        }
        SDL_RenderPresent(m_renderer);
    }

    void App::Screenshot(const char* name)
    {
        SDL_Surface* surface = SDL_RenderReadPixels(m_renderer, nullptr);
        if (surface == nullptr)
            return;
        if (SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32))
        {
            if (m_testHero && m_testStep < 8)
            {
                // Compare the clear upper sky in each art/no-art pair, not just the loaded texture's existence.
                std::vector<uint8_t> samples;
                const float sidebar = SidebarW / ImGui::GetIO().DisplaySize.x * rgba->w;
                for (int y = 0; y < 6; y++)
                    for (int x = 0; x < 6; x++)
                    {
                        int px = int(sidebar + (rgba->w - sidebar) * (0.4f + 0.08f * x));
                        int py = int(rgba->h * (0.12f + 0.02f * y));
                        const auto* pixel = static_cast<const uint8_t*>(rgba->pixels) + py * rgba->pitch + px * 4;
                        samples.insert(samples.end(), pixel, pixel + 3);
                    }
                if (!m_testHeroNoArt)
                    m_testHeroArtPixels = std::move(samples);
                else
                {
                    int changed = 0;
                    if (samples.size() == m_testHeroArtPixels.size())
                        for (size_t i = 0; i < samples.size(); i += 3)
                        {
                            int difference = 0;
                            for (size_t c = 0; c < 3; c++) difference += std::abs(int(samples[i + c]) - int(m_testHeroArtPixels[i + c]));
                            if (difference > 24) changed++;
                        }
                    if (changed < 18)
                    {
                        fprintf(stderr, "FAIL hero background hidden: %d/36 sky samples differ\n", changed);
                        m_testFailures++;
                    }
                    else
                        fprintf(stderr, "hero background visible: %d/36 sky samples differ\n", changed);
                }
            }
            std::string path = m_shots + "/" + name + ".png";
            if (stbi_write_png(path.c_str(), rgba->w, rgba->h, 4, rgba->pixels, rgba->pitch))
                fprintf(stderr, "launcher: shot %s\n", path.c_str());
            SDL_DestroySurface(rgba);
        }
        SDL_DestroySurface(surface);
    }

    // The states the sidebar test puts on a built, verified game by hand: the sidebar's line, the main button's label.
    struct StateCase
    {
        const char* name;
        const char* text;
        const char* button; // as drawn: an unpressable one has " (off)"
        void (*setup)(GameSlot&);
    };
    static const StateCase StateCases[] = {
        { "no-disc", "Needs your disc", "Choose disc", [](GameSlot& slot) { slot.disc = DiscState::None; } },
        { "checking", "Checking your disc", "CHECKING... (off)", [](GameSlot& slot) { slot.disc = DiscState::Verifying; slot.verify.done = false; } },
        { "disc-gone", "Disc not found", "Choose disc", [](GameSlot& slot) { slot.disc = DiscState::Failed; slot.check.status = GameSourceStatus::NotFound; } },
        { "unreadable", "Can't read the disc", "Choose disc", [](GameSlot& slot) { slot.disc = DiscState::Failed; slot.check.status = GameSourceStatus::Unreadable; } },
        { "wrong-disc", "Wrong disc", "Choose disc", [](GameSlot& slot) { slot.disc = DiscState::Failed; slot.check.status = GameSourceStatus::WrongGame; } },
        { "unknown-release", "Unknown release", "Choose disc", [](GameSlot& slot) { slot.disc = DiscState::Failed; slot.check.status = GameSourceStatus::WrongVersion; } },
        { "not-a-game-disc", "Not a game disc", "Choose disc", [](GameSlot& slot) { slot.disc = DiscState::Failed; slot.check.status = GameSourceStatus::NotXboxDisc; } },
        { "ready-to-install", "Ready to install", "INSTALL", [](GameSlot& slot) { slot.installed.runtime.clear(); } },
        { "update-available", "Update available", "UPDATE", [](GameSlot& slot) { slot.installed.gameVersion = "0.0.1"; } },
        { "ready", "Ready to play", "PLAY", [](GameSlot&) {} },
    };

    // The kit follows the launcher (SKY_LAUNCHER_TEST_KIT, with SKY_LAUNCHER_BASE_PATH holding an old kit and SKY_LAUNCHER_UPDATE_URL a
    // fake release of the launcher's own tag, its kit asset being the fake kit "kit-new"; both games' iso= for "old" and "giants"):
    //  old       INSTALL with a kit next to the launcher from an older release: the kit of the launcher's release is fetched
    //            first, and it is the one that builds the game;
    //  giants    INSTALL of Giants with a kit that does not build it (no giants in games=): the right kit first;
    //  updates   the game updates (TestUpdatesScript): a release that bumps Giants only;
    //  offline   the same with no release to reach.
    void App::TestKitScript(uint64_t now)
    {
        const GameInfo& ssa = *FindGame("ssa");
        const GameInfo& giants = *FindGame("giants");
        if (m_testKit == "updates" || m_testKit == "offline")
        {
            TestUpdatesScript(now);
            return;
        }
        GameSlot& slot = SlotFor(m_testKit == "giants" ? giants : ssa);
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL kit step %d (%s): %s (line \"%s\")\n", m_testStep, m_testKit.c_str(), what, SlotStatus(&slot).text.c_str());
                m_testFailures++;
            }
        };
        const std::string tag = "v" + LauncherVersion();
        if (m_testBegan == 0)
            m_testBegan = now;
        if (now - m_testBegan > 120000)
        {
            fprintf(stderr, "kit script: install error \"%s\", kit error \"%s\"\n", InstallSnapshot(slot).error.c_str(), slot.kitError.c_str());
            expect(false, "timeout");
            m_running = false;
            return;
        }
        switch (m_testStep)
        {
        case 0: // the discs checked in the background
            m_page = Page::Play;
            if (m_testBegan == now)
            {
                SelectGame(giants);
                SelectGame(ssa);
                return;
            }
            if (SlotFor(ssa).disc == DiscState::Verifying || SlotFor(giants).disc == DiscState::Verifying)
                return;
            expect(slot.disc == DiscState::Ready, "the disc is ready");
            m_s = &slot;
            SetActiveGame(slot.game.id);
            RefreshInstall();
            expect(slot.kit && !slot.kit->chosen && slot.kit->tag.empty() && slot.kit->version == "old", "the kit next to the launcher is in use");
            expect(m_testKit == "old" ? KitOlderThanLauncher(*slot.kit, LauncherVersion()) : !slot.kit->Builds("giants"), "and it is not the one for this game");
            expect(KitNeedsFetch(slot), "so INSTALL fetches the right kit first");
            m_testStep++;
            break;
        case 1: // INSTALL: the download comes first, and the old kit never runs
            StartInstall();
            expect(slot.kitUsed && slot.kitDownload.Running() && !slot.installer.Running(), "the tools are downloaded before anything is built");
            expect(SlotStatus(&slot).text.rfind("Downloading tools", 0) == 0, "the sidebar says Downloading tools");
            m_testStep++;
            break;
        case 2:
            if (slot.installer.Running() || slot.kitDownload.Running() || slot.installView)
                return;
            expect(slot.kit && slot.kit->version == "kit-new" && slot.kit->tag == tag && slot.kit->Builds(slot.game.id), "the downloaded kit is the one in use");
            expect(slot.kit && FindInstalledGame(m_config, slot.check.xexSha256).kitVersion == "kit-new", "the game was built by it");
            expect(!slot.installed.runtime.empty(), "and is there");
            expect(SettingGet(m_config, "update", "kit_fetched") == tag, "the fetch is remembered");
            m_running = false;
            break;
        }
    }

    // The game updates (SKY_LAUNCHER_TEST_KIT=updates, SKY_LAUNCHER_BASE_PATH holding a kit that builds both games at SSA 0.9.0 and
    // Giants 0.8.0, SKY_LAUNCHER_UPDATE_URL a fake release whose notes bump Giants to 0.8.1 only, its kit being "kit-new"):
    // both games built from the kit at hand, then the check as made at every start: SSA stays PLAY, Giants shows UPDATE, nothing
    // is downloaded meanwhile, "Play without updating" gives PLAY back, UPDATE fetches the kit and rebuilds Giants only.
    // "offline" has no release to reach: the check fails without a word.
    void App::TestUpdatesScript(uint64_t now)
    {
        const GameInfo& ssa = *FindGame("ssa");
        const GameInfo& giants = *FindGame("giants");
        GameSlot& a = SlotFor(ssa);
        GameSlot& g = SlotFor(giants);
        const bool offline = m_testKit == "offline";
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL updates step %d (%s): %s (Giants \"%s\")\n", m_testStep, m_testKit.c_str(), what, SlotStatus(&g).text.c_str());
                m_testFailures++;
            }
        };
        auto shoot = [&](const char* name, uint64_t wait = 500) {
            m_toasts.Clear(); // the install's "Game installed" would sit on the banner
            m_testShot = fmt::format("{:02}-{}", m_testStep, name);
            m_testAt = now + wait;
        };
        auto busy = [](GameSlot& slot) { return slot.installer.Running() || slot.kitDownload.Running() || slot.installView; };
        auto toolsLine = [&](GameSlot& slot) { return SlotStatus(&slot).text.rfind("Downloading tools", 0) == 0; };
        const std::string next = [] {
            SemVer v = ParseSemVer(LauncherVersion());
            return fmt::format("v{}.{}.{}", v.major, v.minor, v.patch + 1);
        }();
        if (m_testBegan == 0)
            m_testBegan = now;
        if (now - m_testBegan > 240000)
        {
            expect(false, "timeout");
            m_running = false;
            return;
        }
        switch (m_testStep)
        {
        case 0: // the discs checked in the background
            m_page = Page::Play;
            if (m_testBegan == now)
            {
                if (!offline)
                    SelectGame(giants);
                SelectGame(ssa);
                return;
            }
            if (a.disc == DiscState::Verifying || (!offline && g.disc == DiscState::Verifying))
                return;
            expect(a.disc == DiscState::Ready && (offline || g.disc == DiscState::Ready), "the discs are ready");
            m_testStep = offline ? 20 : 1;
            break;
        case 1: // both games built from the kit at hand (no download: it is not older than the launcher)
            SelectGame(ssa);
            StartInstall();
            expect(!a.kitUsed && a.installer.Running(), "the kit at hand builds Spyro's Adventure");
            m_testStep++;
            break;
        case 2:
            if (busy(a))
                return;
            expect(!a.installed.runtime.empty() && a.installed.gameVersion == "0.9.0", "Spyro's Adventure is built, at 0.9.0");
            SelectGame(giants);
            StartInstall();
            expect(!g.kitUsed && g.installer.Running(), "and Giants");
            m_testStep++;
            break;
        case 3:
            if (busy(g))
                return;
            expect(!g.installed.runtime.empty() && g.installed.gameVersion == "0.8.0" && GameVersion(g) == "0.8.0", "Giants is built, at 0.8.0");
            expect(m_gameUpdates.empty() && !UpdateAvailable(g) && SlotStatus(&g).button == std::string("PLAY"), "nothing is offered before a check");
            SelectGame(giants);
            shoot("giants-built");
            break;
        case 4: // the check made at every start
            StartUpdateCheck(false);
            m_testStep++;
            break;
        case 5:
            if (m_updates.Busy() || m_updateWasBusy)
                return;
            expect(m_gameUpdates.size() == 2, "the release's games line was read");
            expect(UpdateAvailable(g) && !UpdateAvailable(a), "Giants only has an update");
            expect(SlotStatus(&g).button == std::string("UPDATE") && SlotStatus(&a).button == std::string("PLAY"), "UPDATE for Giants, PLAY for the other");
            expect(!toolsLine(g) && !toolsLine(a) && !std::filesystem::exists(DownloadedKitsFolder()), "no kit is downloaded in the background");
            expect(m_updates.Snapshot().launcherNewer, "the launcher's own update is still offered");
            SelectGame(giants);
            shoot("giants-update");
            break;
        case 6: // "Play without updating"
            m_testPlayWithout = true;
            m_testStep++;
            break;
        case 7:
            expect(g.skipUpdate && !UpdateAvailable(g) && SlotStatus(&g).button == std::string("PLAY"), "Play without updating gives PLAY back");
            shoot("giants-play-without");
            break;
        case 8:
            g.skipUpdate = false;
            SelectGame(ssa);
            shoot("ssa-up-to-date");
            break;
        case 9: // UPDATE: the kit first, then the rebuild
            SelectGame(giants);
            expect(UpdateAvailable(g) && KitNeedsFetch(g), "UPDATE needs the release's kit");
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_SSABUILD_DELAY", "0.15", true); // slow enough for a shot of the card
            StartInstall();
            expect(g.kitUsed && g.kitDownload.Running() && g.kitTag == next, "UPDATE fetches the kit of the release the game names");
            shoot("giants-updating", 1500);
            break;
        case 10:
            if (busy(g))
                return;
            expect(g.kit && g.kit->tag == next && SettingGet(m_config, "update", "kit_fetched") == next, "the new kit is in use");
            expect(g.installed.gameVersion == "0.8.1" && !UpdateAvailable(g) && SlotStatus(&g).button == std::string("PLAY"), "Giants is at 0.8.1 and PLAY again");
            expect(FindInstalledGame(m_config, a.check.xexSha256).kitVersion != "kit-new" && a.installed.gameVersion == "0.9.0", "Spyro's Adventure was not rebuilt");
            shoot("giants-updated");
            break;
        case 11: // "Update all" on Home: both games get an older build, the release names the versions the kit gives
        {
            for (GameSlot* slot : { &a, &g })
            {
                SelectGame(slot->game);
                RecordInstalledGame(m_config, slot->check.xexSha256, slot->installed.runtime, slot->installed.kitVersion, "0.1.0");
                RefreshInstall();
            }
            m_gameUpdates = { { ssa.id, "0.9.0", next }, { giants.id, "0.8.1", next } };
            expect(GamesToUpdate() == 2 && !UpdatingAll(), "two games have an update");
            m_page = Page::Home;
            shoot("home-update-all");
            break;
        }
        case 12: // one click: Spyro's Adventure first, then Giants; Home stays shown
            StartUpdateAll();
            expect(UpdatingAll() && m_updateAll.total == 2 && m_updateAll.current == ssa.id && a.installer.Running(), "Spyro's Adventure starts first");
            expect(!g.installer.Running() && m_page == Page::Home, "the second waits, the page stays");
            shoot("home-updating-all", 700);
            break;
        case 13:
            if (UpdatingAll() || busy(a) || busy(g))
                return;
            expect(a.installed.gameVersion == "0.9.0" && g.installed.gameVersion == "0.8.1" && GamesToUpdate() == 0, "both games are updated, one after the other");
            expect(m_updateAll.started == 2 && m_updateAll.failed == 0 && m_page == Page::Home, "none failed, still on Home");
            shoot("home-updated-all");
            break;
        case 14: // Cancel stops the running one and the one waiting
            for (GameSlot* slot : { &a, &g })
            {
                SelectGame(slot->game);
                RecordInstalledGame(m_config, slot->check.xexSha256, slot->installed.runtime, slot->installed.kitVersion, "0.1.0");
                RefreshInstall();
            }
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_SSABUILD_DELAY", "0.5", true);
            StartUpdateAll();
            expect(UpdatingAll() && a.installer.Running(), "a second run starts");
            shoot("home-updating-again", 1500); // the shot is taken once the first build has run a moment; the step moves on after it
            break;
        case 15:
            CancelUpdateAll();
            expect(m_updateAll.queue.empty(), "Cancel empties the queue");
            m_testStep++;
            break;
        case 16: // a cancelled build leaves its card (interrupted), which is not "busy": only the running jobs are waited for
            if (UpdatingAll() || a.installer.Running() || g.installer.Running() || a.kitDownload.Running() || g.kitDownload.Running())
                return;
            expect(GamesToUpdate() == 2 && !g.installer.Running() && g.installed.gameVersion == "0.1.0", "cancelled: nothing more starts, both still old");
            shoot("home-cancelled");
            break;
        case 17:
            m_running = false;
            break;
        case 20: // offline
            StartUpdateCheck(false);
            m_testStep++;
            break;
        case 21:
            if (m_updates.Busy() || m_updateWasBusy)
                return;
            expect(!m_updates.Snapshot().checked && m_gameUpdates.empty() && BannerHeight() == 0 && m_toasts.Items().empty(), "offline: nothing is said");
            expect(!UpdateAvailable(a) && !std::filesystem::exists(DownloadedKitsFolder()), "and nothing is offered or downloaded");
            shoot("offline");
            break;
        default:
            m_running = false;
            break;
        }
    }

    // The sidebar's line through an install and an update (SKY_LAUNCHER_TEST_SIDEBAR, with a [launcher] kit= that is the fake
    // kit and both games' iso=): Spyro's Adventure ready, installing at 0, 50 and 100 %, Giants shown meanwhile (the other
    // game's line), ready, an update available, updating at 0, 50 and 100 %, then a build that fails. Each shot "<nn>-<state>"
    // is checked against the line the sidebar draws (m_testFailures).
    void App::TestSidebarScript(uint64_t now)
    {
        const GameInfo& ssa = *FindGame("ssa");
        const GameInfo& giants = *FindGame("giants");
        GameSlot& a = SlotFor(ssa);
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL sidebar step %d: %s (line \"%s\")\n", m_testStep, what, SlotStatus(&a).text.c_str());
                m_testFailures++;
            }
        };
        auto line = [&] { return SlotStatus(&a).text; };
        auto starts = [&](const char* prefix) { return line().rfind(prefix, 0) == 0; };
        auto env = [](const char* name, const char* value) { SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value, true); };
        auto shoot = [&](const char* name, uint64_t wait = 300) {
            m_testShot = fmt::format("{:02}-{}", m_testStep, name);
            m_testAt = now + wait;
        };
        auto percent = [&] { return MakeInstallChecklist(InstallSnapshot(a), now).overall; };
        if (m_testBegan == 0)
            m_testBegan = now;
        if (now - m_testBegan > 300000)
        {
            expect(false, "timeout");
            m_running = false;
            return;
        }
        switch (m_testStep)
        {
        case 0: // both discs checked in the background
            m_page = Page::Play;
            if (m_testBegan == now)
            {
                SelectGame(giants);
                SelectGame(ssa);
                return;
            }
            if (a.disc == DiscState::Verifying || SlotFor(giants).disc == DiscState::Verifying)
                return;
            expect(a.disc == DiscState::Ready, "SSA's disc is ready");
            m_testSha = a.check.xexSha256;
            env("FAKE_SSABUILD_DELAY", "0.08");
            env("FAKE_SSABUILD_HOLD", "2");
            expect(line() == "Ready to install", "a verified disc with no build is not ready to play");
            shoot("before-install", 600);
            break;
        case 1: // INSTALL: the line follows the install from its first moment
            StartInstall();
            expect(starts("Installing") && SlotStatus(&a).progress >= 0 && !SlotStatus(&a).buttonEnabled &&
                SlotStatus(&a).button == std::string("INSTALLING..."), "Installing at the start");
            shoot("installing-0", 20);
            break;
        case 2:
            if (percent() < 0.5f)
                return;
            expect(starts("Installing") || starts("Finishing"), "Installing at 50 %");
            shoot("installing-50", 20);
            break;
        case 3: // the other game is shown: SSA's line goes on in the sidebar
            SelectGame(giants);
            expect(m_s != &a && (starts("Installing") || starts("Finishing")), "SSA's line while Giants is shown");
            shoot("installing-other-game", 300);
            break;
        case 4: // back on SSA, held at the end of the last step
            SelectGame(ssa);
            if (percent() < 0.99f)
                return;
            expect(starts("Finishing") && SlotStatus(&a).progress >= 0.99f, "Finishing at 100 %");
            shoot("installing-100", 20);
            break;
        case 5:
            if (a.installer.Running() || a.installView)
                return;
            expect(!a.installed.runtime.empty() && line() == "Ready to play", "Ready to play once built");
            shoot("ready", 4000);
            break;
        case 6: // the release publishes a newer version of the game than the one built: an update
            m_config["install." + m_testSha]["version"] = "0.0.1";
            m_gameUpdates = { { a.game.id, "99.0.0", "v99.0.0" } };
            RefreshInstall();
            expect(line() == "Update available", "Update available");
            shoot("update-available", 400);
            break;
        case 7:
            StartInstall();
            expect(starts("Updating") && SlotStatus(&a).progress >= 0 && SlotStatus(&a).button == std::string("UPDATING..."), "Updating at the start");
            shoot("updating-0", 20);
            break;
        case 8:
            if (percent() < 0.5f)
                return;
            expect(starts("Updating") || starts("Finishing"), "Updating at 50 %");
            shoot("updating-50", 20);
            break;
        case 9:
            if (percent() < 0.99f)
                return;
            expect(starts("Finishing") && SlotStatus(&a).progress >= 0.99f, "Finishing at 100 % of an update");
            shoot("updating-100", 20);
            break;
        case 10:
            if (a.installer.Running() || a.installView)
                return;
            expect(line() == "Ready to play", "Ready to play after the update");
            env("FAKE_SSABUILD_HOLD", "0");
            env("FAKE_SSABUILD_FAIL", "compile");
            m_config["install." + m_testSha]["kit"] = "0.0.0-old";
            RefreshInstall();
            StartInstall();
            m_testStep++;
            break;
        case 11:
            if (a.installer.Running())
                return;
            expect(line() == "Update failed" && SlotStatus(&a).button == std::string("PLAY"), "Update failed, the build still there");
            shoot("update-failed", 400);
            break;
        default:
        {
            // Every other state is put on the slot by hand, two steps each: the Play page (the sidebar's line and the main
            // button are checked against what was drawn), then Home (the card's line).
            constexpr int First = 12, Cases = int(std::size(StateCases)), Toasts = First + 2 * Cases;
            if (m_testStep < Toasts)
            {
                int n = (m_testStep - First) / 2;
                const StateCase& state = StateCases[n];
                if ((m_testStep - First) % 2 == 0)
                {
                    if (n > 0)
                        expect(m_drawn["home:ssa"] == StateCases[n - 1].text, "Home's card says the same");
                    if (n == 0)
                        m_testBuilt = a.installed.runtime;
                    a.disc = DiscState::Ready;
                    a.check.status = GameSourceStatus::Ok;
                    a.installView = false;
                    a.verify.done = true;
                    a.installed.runtime = m_testBuilt;
                    a.installed.kitVersion = a.kit ? a.kit->version : "";
                    // The release publishes version 99 of the game: only the state that built an older one is offered it.
                    m_gameUpdates = { { a.game.id, "99.0.0", "v99.0.0" } };
                    a.installed.gameVersion = "99.0.0";
                    state.setup(a);
                    m_page = Page::Play;
                    expect(line() == state.text, state.name);
                    shoot(fmt::format("{}-play", state.name).c_str(), 400);
                }
                else
                {
                    expect(m_drawn["sidebar:ssa"] == state.text, "the sidebar draws the state");
                    expect(m_drawn["button"] == state.button, "the main button draws the state");
                    m_page = Page::Home;
                    shoot(fmt::format("{}-home", state.name).c_str(), 400);
                }
                break;
            }
            if (m_testStep == Toasts)
            {
                expect(m_drawn["home:ssa"] == StateCases[Cases - 1].text, "Home's card says the same");
                a.verify.done = true;
                m_page = Page::Play;
                Toast(Tr("Game installed: press PLAY"), ui::ToastKind::Success);
                Toast(Tr("Log copied"), ui::ToastKind::Success);
                Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
                Toast(Tf("Saves restored from {}", "2026-10-26 09:30"), ui::ToastKind::Success);
                shoot("toasts-stacked", 400);
                break;
            }
            if (m_testStep == Toasts + 1)
            {
                expect(m_toasts.Items().size() == 4 && m_toasts.Items()[2].kind == ui::ToastKind::Error, "four messages, one an error");
                m_testAt = now + 3500; // the others have gone by themselves
                m_testShot = fmt::format("{:02}-toasts-error-kept", m_testStep);
                break;
            }
            if (m_testStep == Toasts + 2)
            {
                expect(m_toasts.Items().size() == 1 && m_toasts.Items()[0].kind == ui::ToastKind::Error, "only the error is left (it stays until closed)");
                m_toasts.Clear();
            }
            SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_SSABUILD_FAIL");
            m_running = false;
            break;
        }
        }
    }

    // Keyboard and pad navigation (SKY_LAUNCHER_TEST_NAV): keys go to ImGui as a player's would, a shot after each group.
    // Switching games in place: both games' discs are checked and their builds made (the fake kit) while the window
    // stays, the state of each is kept, and PLAY returns the shown game and its own build.
    void App::TestSwitchScript(uint64_t now)
    {
        const GameInfo& ssa = *FindGame("ssa");
        const GameInfo& giants = *FindGame("giants");
        GameSlot& a = SlotFor(ssa);
        GameSlot& g = SlotFor(giants);
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL switch step %d: %s\n", m_testStep, what);
                m_testFailures++;
            }
        };
        auto folderOf = [](const std::string& runtime) { return std::filesystem::path(runtime).parent_path().filename().string(); };
        if (m_testBegan == 0)
            m_testBegan = now;
        if (now - m_testBegan > 300000)
        {
            expect(false, "timeout");
            m_running = false;
            return;
        }
        switch (m_testStep)
        {
        case 0: // Spyro's Adventure first: its disc is checked in the background
            if (m_testBegan == now)
            {
                SelectGame(ssa);
                expect(a.started && m_s == &a && a.disc == DiscState::Verifying, "SSA's check started");
                return;
            }
            if (a.disc == DiscState::Verifying)
                return;
            expect(a.disc == DiscState::Ready && !a.check.xexSha256.empty(), "SSA's disc is ready");
            m_testSha = a.check.xexSha256;
            m_testPath = a.discPath;
            m_testStep++;
            break;
        case 1: // Giants: the window stays, its content is Giants', SSA's state is untouched
            SelectGame(giants);
            expect(m_s == &g && g.disc == DiscState::Verifying, "Giants shown at once, its check running");
            expect(a.disc == DiscState::Ready && a.check.xexSha256 == m_testSha && a.discPath == m_testPath, "SSA's state kept");
            expect(SettingGet(m_config, "launcher", "game") == "giants" && ActiveGame() == "giants", "Giants remembered");
            m_testStep++;
            break;
        case 2:
            if (g.disc == DiscState::Verifying)
                return;
            expect(g.disc == DiscState::Ready && !g.check.xexSha256.empty() && g.check.xexSha256 != m_testSha, "Giants' disc is its own");
            expect(g.discPath != m_testPath && a.assets.Get(LauncherAssets::Role::Logo).path != g.assets.Get(LauncherAssets::Role::Logo).path,
                "each game keeps its own disc and art");
            expect(NeedsInstall(), "Giants needs its install");
            StartInstall(); // Giants' build, in the background
            expect(Installing(), "Giants installing");
            m_testStep++;
            break;
        case 3: // back to SSA while Giants installs: SSA is not installing, its state is as left
            SelectGame(ssa);
            expect(m_s == &a && a.disc == DiscState::Ready && a.check.xexSha256 == m_testSha, "SSA back, as it was");
            expect(!Installing() && AnyInstalling() && NeedsInstall(), "the install belongs to Giants");
            m_testStep++;
            break;
        case 4: // (the progress card goes once the poll has recorded the build)
            if (AnyInstalling() || g.installView)
                return;
            expect(!g.installed.runtime.empty() && a.installed.runtime.empty(), "Giants built, SSA not");
            m_testRuntime[1] = g.installed.runtime;
            StartInstall(); // SSA's build
            m_testStep++;
            break;
        case 5:
            if (AnyInstalling() || a.installView)
                return;
            expect(!a.installed.runtime.empty() && a.installed.runtime != g.installed.runtime, "SSA built, in its own place");
            expect(folderOf(a.installed.runtime) == datadir::GameFolderName("ssa") && folderOf(g.installed.runtime) == datadir::GameFolderName("giants"),
                "each build is in its game's folder");
            m_testRuntime[0] = a.installed.runtime;
            m_testStep++;
            break;
        case 6: // PLAY on each game: its disc, its build, its id
            SelectGame(ssa);
            expect(!NeedsInstall() && !Installing(), "SSA ready to play");
            PlayNow();
            expect(!m_running && &ChosenGame() == &ssa && ChosenRuntime() == m_testRuntime[0] && a.chosen == m_testPath, "PLAY on SSA runs SSA");
            m_running = true;
            SelectGame(giants);
            PlayNow();
            expect(!m_running && &ChosenGame() == &giants && ChosenRuntime() == m_testRuntime[1] && g.chosen == g.discPath, "PLAY on Giants runs Giants");
            expect(a.chosen == m_testPath && a.chosenRuntime == m_testRuntime[0], "SSA's choice kept");
            m_testStep++;
            break;
        }
    }

    // The X on every seat (players, item, adventure), pressed three ways: the mouse on the X, Delete on the focused seat, and the
    // pad's X on it. Each try puts the seats back, presses, and checks that this seat alone is empty. Four steps per try.
    void App::TestSeatXScript(uint64_t now)
    {
        constexpr int Modes = 3, StepsPerTry = 4;
        std::vector<std::string> seats = m_s->collection->TestSeats();
        if (m_testStep == 0)
        {
            m_page = Page::Collection;
            m_s->collection->Seed();
            m_testAt = now + 600;
            return;
        }
        if (m_testStep == 1) // the seeded seats, once the page has loaded them
        {
            m_testSeats = seats;
            if (std::count_if(seats.begin(), seats.end(), [](const std::string& seat) { return !seat.empty(); }) != 4)
            {
                fprintf(stderr, "FAIL seat X: the seeded collection has no figure on every seat\n");
                m_testFailures++;
                m_running = false;
            }
            m_testAt = now + 300;
            return;
        }
        int tries = int(m_testSeats.size()) * Modes, step = m_testStep - 2, seat = step / StepsPerTry / Modes, mode = step / StepsPerTry % Modes;
        if (step / StepsPerTry >= tries)
        {
            m_running = false;
            return;
        }
        static const char* const ModeNames[Modes] = { "mouse", "keyboard", "pad" };
        ImGuiIO& io = ImGui::GetIO();
        switch (step % StepsPerTry)
        {
        case 0: // every seat occupied again
            m_s->collection->TestSetSeats(m_testSeats);
            break;
        case 1: // the pointer on the X, or the focus on the seat
            if (mode == 0)
            {
                ImVec2 at = m_s->collection->TestCrossAt(size_t(seat));
                if (at.x < 0)
                {
                    fprintf(stderr, "FAIL seat X: seat %d has no X on screen\n", seat);
                    m_testFailures++;
                }
                io.AddMousePosEvent(at.x, at.y);
            }
            else
                m_s->collection->TestFocusSeat(size_t(seat));
            break;
        case 2: // the press
            if (mode == 0)
            {
                io.AddMouseButtonEvent(0, true);
                io.AddMouseButtonEvent(0, false);
            }
            else
            {
                ImGuiKey key = mode == 1 ? ImGuiKey_Delete : ImGuiKey_GamepadFaceLeft;
                io.AddKeyEvent(key, true);
                io.AddKeyEvent(key, false);
            }
            break;
        case 3: // only this seat is empty
        {
            bool ok = seats.size() == m_testSeats.size();
            for (size_t i = 0; ok && i < seats.size(); i++)
                ok = int(i) == seat ? seats[i].empty() : seats[i] == m_testSeats[i];
            if (!ok)
            {
                fprintf(stderr, "FAIL seat X: seat %d by %s did not take its figure off\n", seat, ModeNames[mode]);
                m_testFailures++;
            }
            else
                printf("seat X: seat %d by %s: taken off\n", seat, ModeNames[mode]);
            break;
        }
        }
        m_testAt = now + 500;
    }

    void App::TestNavScript(uint64_t now)
    {
        // A key is pressed and released, unless Hold (down only) or Release (up only) wraps it.
        constexpr int Hold = 0x10000, Release = 0x20000;
        static const std::vector<int> Steps[] = {
            {},                                                        // 40: the launcher opens, the focus in the sidebar
            { ImGuiKey_Escape },                                       // 41: B: the ring on the game's row
            { ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow }, // 42: down: past Giants, Swap Force and Coming later
            { ImGuiKey_Space },                                        // 43: A: Collection, the ring on Add a figure
            { ImGuiKey_GamepadFaceUp },                                // 44: Y: the search box takes the keyboard
            { ImGuiKey_Escape },                                       // 45: B: out of the search
            { ImGuiKey_UpArrow },                                      // 46: up: to the seats, above the bar
            { ImGuiKey_Space },                                        // 47: A: choosing for that seat (lit, the bar says so)
            { ImGuiKey_DownArrow },                                    // 48: down: the shelf's first card
            { ImGuiKey_Space },                                        // 49: A: it goes on the seat, the choosing ends
            { ImGuiKey_GamepadFaceLeft },                              // 50: X: the focused card's sheet
            { ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadFaceRight },  // 51: B, B: closed, back to Play
            { ImGuiKey_Escape, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_Space,
              ImGuiKey_GamepadR1 },                                    // 52: B to the sidebar, down to Settings, A, RB: Settings > Launcher
            { ImGuiKey_Escape },                                       // 53: Escape: back to Play
        };
        // Where each step's keys must have led (checked when the next step begins): the sidebar's rows, the collection, the
        // settings; a step that went elsewhere would open what the next keys press (a file chooser: the run would wait).
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL nav step %d: %s\n", 39 + m_testStep, what);
                m_testFailures++;
            }
        };
        if (m_testBegan == 0)
            m_testBegan = now;
        if (now - m_testBegan > 120000)
        {
            expect(false, "timeout");
            m_running = false;
            return;
        }
        switch (m_testStep)
        {
        case 4: expect(m_page == Page::Collection, "A on the sidebar's Collection row opens the collection"); break;
        case 12: expect(m_page == Page::Play, "B, B close the sheet and the collection"); break;
        case 13: expect(m_page == Page::Settings && m_tab == SettingsTab::Launcher, "Settings, then RB: its Launcher tab"); break;
        case 14: expect(m_page == Page::Play, "Escape leaves Settings"); break;
        default: break;
        }
        if (m_testStep >= int(std::size(Steps)))
        {
            m_running = false;
            return;
        }
        if (m_testStep == 0)
        {
            m_page = Page::Play;
            m_s->collection->Seed();
            if (m_s->disc == DiscState::None && !m_testDrop.empty())
                StartVerify(m_testDrop);
            if (m_s->disc == DiscState::Ready || m_s->disc == DiscState::Failed || m_testDrop.empty())
                m_testAt = now + 500;
            return;
        }
        // ImGui's input queue spreads the events over frames, in order.
        ImGuiIO& io = ImGui::GetIO();
        bool holding = false;
        for (int step : Steps[m_testStep])
        {
            ImGuiKey key = ImGuiKey(step & 0xFFFF);
            if (!(step & Release))
                io.AddKeyEvent(key, true);
            if (!(step & Hold))
                io.AddKeyEvent(key, false);
            holding = holding || (step & Hold) != 0;
        }
        m_testAt = now + (holding ? 1500 : 600);
    }

    // The action button's states (SKY_LAUNCHER_TEST_BUTTONS, with a [launcher] kit= that is the fake kit and both games'
    // iso=): for each game INSTALL, under the pointer, pressed, focused, the settings under the
    // pointer, disabled, installing, then PLAY and PLAY under the pointer; then each game in the smallest window, at 150 %
    // and at 200 %. Shots "<game>-<step>-<state>".
    void App::TestButtonsScript(uint64_t now)
    {
        static const char* const Games[] = { "ssa", "giants" };
        constexpr int PerGame = 9, Extras = 3;
        ImGuiIO& io = ImGui::GetIO();
        int game = m_testStep / PerGame, step = m_testStep % PerGame;
        if (m_testStep >= 2 * PerGame)
        {
            // The window's extremes, PLAY on each game.
            int extra = (m_testStep - 2 * PerGame) / 2;
            game = (m_testStep - 2 * PerGame) % 2;
            if (extra >= Extras)
            {
                m_running = false;
                return;
            }
            static const char* const Names[] = { "small", "scale150", "scale200" };
            static const float Scales[] = { 1, 1.5f, 2 };
            SelectGame(*FindGame(Games[game]));
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            m_testUiScale = m_uiScale = Scales[extra];
            float w = extra == 0 ? WindowMinW : WindowW, h = extra == 0 ? WindowMinH : WindowH;
            SDL_SetWindowSize(m_window, int(w * m_uiScale), int(h * m_uiScale));
            m_testShot = fmt::format("{}-{:02}-{}", Games[game], PerGame + extra, Names[extra]);
            m_testAt = now + 1500;
            return;
        }
        GameSlot& slot = SlotFor(*FindGame(Games[game]));
        auto shoot = [&](const char* name, uint64_t wait = 300) {
            m_testShot = fmt::format("{}-{:02}-{}", Games[game], step, name);
            m_testAt = now + wait;
        };
        auto pointAt = [&](ImVec2 at) { io.AddMousePosEvent(at.x, at.y); };
        auto pointAway = [&] { io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); };
        switch (step)
        {
        case 0:
            if (m_s != &slot || !slot.started)
                SelectGame(slot.game); // its remembered disc checked
            m_page = Page::Play;
            pointAway();
            if (slot.disc == DiscState::Ready && !Installing() && !slot.installView)
                shoot("install", 600);
            break;
        case 1:
            pointAt(m_testMainAt);
            shoot("hover");
            break;
        case 2:
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            shoot("pressed");
            break;
        case 3:
            // Released away from the button (no click), then the focus as the keyboard or the pad puts it.
            pointAway();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            m_focusTo = FocusTo::Page;
            m_focusShow = true;
            shoot("focused");
            break;
        case 4:
            ImGui::SetNavCursorVisible(false); // as the mouse hides it
            pointAt(m_testGearAt);
            shoot("settings-hover");
            break;
        case 5:
            pointAway();
            m_testButtonsOff = true;
            shoot("disabled");
            break;
        case 6:
            m_testButtonsOff = false;
            if (!Installing() && !slot.installView)
            {
                SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_SSABUILD_DELAY", "0.03", true);
                StartInstall();
            }
            else if (Installing() && MakeInstallChecklist(InstallSnapshot(), now).overall >= 0.4f)
                shoot("installing", 50);
            break;
        case 7:
            if (!Installing() && !slot.installView)
                shoot("play", 4000); // after the "installed" toast
            break;
        case 8:
            pointAt(m_testMainAt);
            shoot("play-hover");
            m_testAt = now + 300;
            break;
        }
    }

    // Screens of the screenshot test, each captured once it has settled.
    // The three model screens (docs/polish_audit.md): Home, the game page and its Keyboard tab, as a player finds them: Spyro's
    // Adventure's disc (SKY_LAUNCHER_TEST_DROP) verified and its game built (by hand: the runtime is not run), Giants opened
    // with no disc, Swap Force not opened yet. With a pad (SKY_LAUNCHER_TEST_SCREENS=pad) the focus is on each page's first
    // stop, its ring shown; with the mouse, no ring. SKY_LAUNCHER_LANGUAGE and SKY_LAUNCHER_TEST_SIZE set the rest;
    // SKY_LAUNCHER_TEST_SCALE=2 draws them at twice the density (a Retina screen's), the same layout.
    void App::TestScreensScript(uint64_t now)
    {
        const bool pad = m_testScreens == 2;
        auto shoot = [&](const char* name, Page page, uint64_t wait) {
            m_page = page;
            m_gameTab = GameTab::Keyboard;
            m_focusTo = FocusTo::Page;
            m_focusShow = pad;
            ImGui::SetNavCursorVisible(pad);
            m_testShot = fmt::format("screen-{}", name);
            m_testAt = now + wait;
        };
        GameSlot& ssa = SlotFor(*FindGame("ssa"));
        switch (m_testStep)
        {
        case 0:
            if (const char* scale = LauncherEnv("SKY_LAUNCHER_TEST_SCALE"); scale != nullptr && std::atof(scale) > 0)
            {
                int w = int(WindowW), h = int(WindowH);
                if (const char* size = LauncherEnv("SKY_LAUNCHER_TEST_SIZE"))
                    std::sscanf(size, "%dx%d", &w, &h);
                m_testUiScale = m_uiScale = float(std::atof(scale));
                SDL_SetWindowSize(m_window, int(float(w) * m_uiScale), int(float(h) * m_uiScale));
            }
            SelectGame(*FindGame("giants"));
            SelectGame(*FindGame("ssa"));
            if (ssa.disc == DiscState::None && !m_testDrop.empty())
                StartVerify(m_testDrop);
            m_page = Page::Play;
            m_testStep = 1;
            return;
        case 1:
            if (ssa.disc == DiscState::Verifying)
                return;
            ssa.installed.runtime = "test-built";
            ssa.installed.gameVersion = "99.0.0";
            shoot("home", Page::Home, 600);
            break;
        case 2: shoot("game", Page::Play, 2200); break; // the logo has fallen
        case 3: shoot("keys", Page::GameSettings, 600); break;
        default: m_running = false; break;
        }
    }

    // One game at a time (SKY_LAUNCHER_TEST_RUNNING, with SKY_LAUNCHER_RUNTIME a stand-in game that runs a few seconds and exits
    // with 3, and SKY_LAUNCHER_TEST_DROP Spyro's Adventure's disc): PLAY starts it and the window stays; its button says Running,
    // a second PLAY starts nothing, another game's button waits for it; once it stops (a crash: its exit code is told) PLAY is
    // back. Shots: running-1-game, running-2-other, running-3-ended.
    void App::TestRunningScript(uint64_t now)
    {
        GameSlot& ssa = SlotFor(*FindGame("ssa"));
        GameSlot& giants = SlotFor(*FindGame("giants"));
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL running step %d: %s (running \"%s\")\n", m_testStep, what, m_session.Running().c_str());
                m_testFailures++;
            }
        };
        auto shoot = [&](const char* name) {
            m_page = Page::Play;
            m_testShot = fmt::format("running-{}", name);
            m_testAt = now + 600;
        };
        if (const char* mode = LauncherEnv("SKY_LAUNCHER_TEST_RUNNING"); mode != nullptr && std::string_view(mode) == "controls")
        {
            switch (m_testStep)
            {
            case 0:
                m_startGames = true;
                SelectGame(*FindGame("giants"));
                SelectGame(*FindGame("ssa"));
                ssa.disc = DiscState::Ready;
                ssa.discPath = "--fake-game";
                ssa.installed.runtime = LauncherEnv("SKY_LAUNCHER_RUNTIME");
                ssa.installed.gameVersion = "99.0.0";
                m_testBegan = now;
                {
                    std::string error;
                    m_session.Start("ssa", { ssa.installed.runtime, {}, {} }, "--fake-game", error);
                }
                expect(m_session.Ours() && m_session.Running() == "ssa", "the fake game starts");
                shoot("control-started");
                return;
            case 1:
                if (now - m_testBegan > 10000)
                {
                    expect(false, "the fake game's ready file appears");
                    m_running = false;
                    return;
                }
                if (!std::filesystem::exists(LauncherEnv("SKY_FAKE_GAME_READY"))) return;
                expect(m_drawn["sidebar:ssa"] == "Running" && m_drawn["hero-main"] == "Stop", "the sidebar runs and STOP is enabled");
                expect(m_drawn["hero-show"] == "hidden", "a windowless process has no Show button");
                expect(m_session.Stop(now), "STOP requests the owned process's end");
                shoot("control-stopping");
                return;
            case 2:
                if (m_session.Ours() && now - m_testBegan < 15000) return;
                expect(m_session.Running().empty(), "the fake game stops");
                expect(m_drawn["sidebar:ssa"] == "Ready to play" && m_drawn["hero-main"] == "PLAY", "the page and sidebar return to ready");
                expect(SlotStatus(&giants).buttonEnabled, "other games can start again");
                expect(!m_drawn.count("stopped"), "a requested stop has no crash dialog");
                shoot("control-ready");
                return;
            default: m_running = false; return;
            }
        }
        switch (m_testStep)
        {
        case 0:
            m_startGames = true;
            SelectGame(*FindGame("giants"));
            SelectGame(*FindGame("ssa"));
            if (ssa.disc == DiscState::None && !m_testDrop.empty())
                StartVerify(m_testDrop);
            m_page = Page::Play;
            m_testBegan = now;
            m_testStep = 1;
            return;
        case 1:
        {
            if (ssa.disc == DiscState::Verifying)
                return;
            expect(ssa.disc == DiscState::Ready, "the disc is checked");
            ssa.installed.runtime = "test-built"; // built by hand: SKY_LAUNCHER_RUNTIME is what runs
            PlayNow();
            PlayNow(); // a double click: still one game
            const SlotLine line = SlotStatus(&ssa);
            expect(m_session.Running() == "ssa" && m_session.Ours(), "PLAY starts the game, the window stays");
            expect(line.state == SlotState::Running && line.button == "Running" && !line.buttonEnabled && line.text == "Running", "its button says Running, off");
            expect(!m_toasts.Items().empty() && m_toasts.Items().back().text == "Spyro's Adventure is running", "the second PLAY is refused");
            m_toasts.Clear();
            shoot("1-game");
            return;
        }
        case 2:
        {
            SelectGame(*FindGame("giants"));
            const SlotLine line = SlotStatus(&giants);
            expect(line.button == "Spyro's Adventure is running" && !line.buttonEnabled, "another game waits for it");
            shoot("2-other");
            return;
        }
        case 3:
            if (m_session.Ours())
            {
                if (now - m_testBegan > 30000)
                {
                    expect(false, "the stand-in game stops");
                    m_running = false;
                }
                return;
            }
            expect(m_session.Running().empty(), "nothing runs once it stopped");
            expect(m_drawn["stopped"] == "ssa 3", "its crash is told");
            SelectGame(*FindGame("ssa"));
            expect(SlotStatus(&ssa).button == "PLAY" && SlotStatus(&ssa).buttonEnabled && SlotStatus(&giants).buttonEnabled, "PLAY is back");
            shoot("3-ended");
            return;
        default:
            m_running = false;
            return;
        }
    }

    void App::TestScript()
    {
        if (m_shots.empty() || m_testAt != 0)
            return;
        uint64_t now = SDL_GetTicks();
        if (m_testExperimental)
        {
            switch (m_testStep)
            {
            case 0:
                SetExperimentalTrapTeam(false);
                m_page = Page::Home;
                m_testShot = "trapteam-01-sidebar-off";
                break;
            case 1:
                m_page = Page::Settings;
                m_tab = SettingsTab::Launcher;
                m_testShot = "trapteam-02-settings-off";
                break;
            case 2:
                SetExperimentalTrapTeam(true);
                m_testShot = "trapteam-03-settings-on";
                break;
            case 3:
                SelectGame(*FindGame("trapteam"));
                m_page = Page::Play;
                m_testShot = "trapteam-04-sidebar-on";
                break;
            case 4:
                SetExperimentalTrapTeam(false);
                m_page = Page::Home;
                m_testShot = "trapteam-05-sidebar-off-again";
                break;
            default:
                m_running = false;
                return;
            }
            m_testAt = now + 400;
            return;
        }
        if (m_testHero)
        {
            TestHeroScript(now);
            return;
        }
        if (m_testInstall)
        {
            TestInstallScript(now);
            return;
        }
        if (m_testUpdate)
        {
            TestUpdateScript(now);
            return;
        }
        if (m_testUninstall)
        {
            TestUninstallScript(now);
            return;
        }
        if (m_testScreens != 0)
        {
            TestScreensScript(now);
            return;
        }
        if (m_testRunning)
        {
            TestRunningScript(now);
            return;
        }
        if (m_testPad)
        {
            // Each game's Controller tab (SKY_LAUNCHER_TEST_PAD): with a few buttons given other jobs, to see the rows; Swap Force's
            // Game tab has the Nightmare option (a second shot).
            static const char* const Games[] = { "ssa", "giants", "swapforce" };
            if (m_testStep >= int(std::size(Games)) + 1)
            {
                m_running = false;
                return;
            }
            if (m_testStep < int(std::size(Games)))
            {
                SelectGame(*FindGame(Games[m_testStep]));
                SetPadTarget(m_config, Games[m_testStep], 5, 1);   // RB does B's job
                SetPadTarget(m_config, Games[m_testStep], 6, -1);  // LT is off
                m_config[GameInputSection(Games[m_testStep])]["rumble_p2"] = "low";
                m_page = Page::GameSettings;
                m_gameTab = GameTab::Controller;
                m_testShot = fmt::format("pad-{}", Games[m_testStep]);
            }
            else
            {
                SelectGame(*FindGame("swapforce"));
                m_config["swapforce"]["nightmare_from_start"] = "1";
                m_page = Page::GameSettings;
                m_gameTab = GameTab::Game;
                m_testShot = "game-swapforce";
            }
            m_testAt = now + 600;
            return;
        }
        if (m_testKeys)
        {
            // Each game's Keyboard tab (set SKY_LAUNCHER_LANGUAGE for the language of the shots).
            static const char* const Games[] = { "ssa", "giants", "swapforce" };
            if (m_testStep >= int(std::size(Games)))
            {
                m_running = false;
                return;
            }
            SelectGame(*FindGame(Games[m_testStep]));
            m_page = Page::GameSettings;
            m_gameTab = GameTab::Keyboard;
            m_testShot = fmt::format("keys-{}", Games[m_testStep]);
            m_testAt = now + 600;
            return;
        }
        if (m_testAchievements)
        {
            // The shown game's page with its disc verified and some progress, then its achievements (SKY_LAUNCHER_TEST_DROP names the disc;
            // SKYLANDERS_DATA a scratch folder: the progress is written there).
            switch (m_testStep)
            {
            case 0:
                m_page = Page::Play;
                m_testShot = "achievements-0-checking";
                m_testAt = now + 300;
                break;
            case 1:
                if (m_s->disc == DiscState::None && !m_testDrop.empty())
                    StartVerify(m_testDrop);
                if (m_s->disc == DiscState::Ready || m_s->disc == DiscState::Failed || m_testDrop.empty())
                {
                    // Six unlocks on different days, 12 h 34 of play, last played a moment ago.
                    achievements::List list = achievements::ReadCache(AchievementCacheFolder(m_s->game.id), "en");
                    const int64_t nowTime = int64_t(std::time(nullptr));
                    for (size_t i = 0; i < list.items.size() && i < 6; i++)
                        progress::WriteUnlock(progress::Folder(m_s->game.id), list.items[i * 2 % list.items.size()].id, nowTime - int64_t(i) * 86400 * 3 - 600);
                    progress::WritePlayTime(progress::Folder(m_s->game.id), { 12 * 3600 + 34 * 60 + 20, nowTime - 90, 9 });
                    RefreshProgress(true);
                    m_testShot = "achievements-1-game";
                    m_testAt = now + 1500;
                }
                break;
            case 2:
                GoTo(Page::Achievements);
                m_testShot = "achievements-2-list";
                m_testAt = now + 900;
                break;
            case 3:
                m_testShot = "achievements-3-end";
                m_testAt = now + 700;
                break;
            default:
                m_running = false;
            }
            return;
        }
        if (m_testLogo)
        {
            // Each game's page, once its logo has settled (SKY_LAUNCHER_TEST_SIZE sets the window).
            static const char* const Games[] = { "ssa", "giants", "swapforce" };
            if (m_testStep >= int(std::size(Games)))
            {
                m_running = false;
                return;
            }
            SelectGame(*FindGame(Games[m_testStep]));
            m_page = Page::Play;
            m_testShot = fmt::format("logo-{}", Games[m_testStep]);
            m_testAt = now + 2200;
            return;
        }
        if (m_testButtons)
        {
            TestButtonsScript(now);
            return;
        }
        if (m_testGallery)
        {
            TestGalleryScript(now);
            return;
        }
        if (m_testNav)
        {
            TestNavScript(now);
            return;
        }
        if (m_testSidebar)
        {
            TestSidebarScript(now);
            return;
        }
        if (!m_testKit.empty())
        {
            TestKitScript(now);
            return;
        }
        if (m_testSwitch)
        {
            TestSwitchScript(now);
            return;
        }
        if (m_testCopy)
        {
            TestCopyScript(now);
            return;
        }
        if (m_testSeatX)
        {
            TestSeatXScript(now);
            return;
        }
        if (m_testCollection)
        {
            // The collection's screens, in the scratch collection SKY_COLLECTION names.
            if (m_testStep >= CollectionPage::TestShotCount())
                m_running = false;
            else
            {
                if (m_testStep == 0)
                    m_s->collection->Seed();
                m_page = Page::Collection;
                m_s->collection->TestScreen(m_testStep);
                m_testAt = now + 700;
            }
            return;
        }
        switch (m_testStep)
        {
        case 0:
            m_page = Page::Play;
            m_testAt = now + 600;
            break;
        case 1:
            if (m_s->disc == DiscState::None && !m_testDrop.empty())
                StartVerify(m_testDrop);
            if (m_s->disc == DiscState::Ready || m_s->disc == DiscState::Failed || m_testDrop.empty())
                m_testAt = now + 400;
            break;
        case 2:
            m_page = Page::GameSettings;
            m_gameTab = GameTab::Video;
            m_testAt = now + 400;
            break;
        case 3:
            m_page = Page::Settings;
            m_tab = SettingsTab::Controls;
            m_testAt = now + 400;
            break;
        case 4:
            // A few figures to show, in the test's own collection: characters, a legendary, a sidekick, an item and an
            // adventure pack; some sit on the next session's seats.
            m_s->collection->Seed();
            m_page = Page::Collection;
            m_s->collection->TestScreen(0);
            m_testAt = now + 600;
            break;
        case 5:
            m_page = Page::Home;
            m_testAt = now + 400;
            break;
        case 6:
            m_page = Page::Collection;
            m_s->collection->TestScreen(3);
            m_testAt = now + 600;
            break;
        case 7:
            m_s->collection->TestScreen(6);
            m_testAt = now + 600;
            break;
        case 8:
            m_page = Page::Play;
            m_testControllers = true;
            m_testAt = now + 500;
            break;
        case 9:
            m_page = Page::GameSettings;
            m_gameTab = GameTab::Language;
            m_testAt = now + 400;
            break;
        case 10:
            m_gameTab = GameTab::Disc;
            m_testAt = now + 400;
            break;
        case 11:
            m_gameTab = GameTab::Saves;
            m_testAt = now + 400;
            break;
        case 12:
            m_gameTab = GameTab::Game;
            m_testAt = now + 400;
            break;
        case 13:
            m_page = Page::Settings;
            m_tab = SettingsTab::Launcher;
            m_testAt = now + 400;
            break;
        case 14:
            // The verified image moved away: INSTALL says so instead of failing later.
            m_page = Page::Play;
            if (m_s->disc == DiscState::Ready)
            {
                m_s->discPath += ".moved";
                StartInstall();
            }
            m_testAt = now + 400;
            break;
        case 15:
            // The logo falling in, early in its fall (it eases out: most of the way is done in the first third): the disc put
            // back, the page entered again.
            if (m_s->discPath.ends_with(".moved"))
            {
                m_s->discPath.resize(m_s->discPath.size() - 6);
                m_s->disc = DiscState::Ready;
                m_s->check.status = GameSourceStatus::Ok;
            }
            m_page = Page::Play;
            m_lastPage = Page::Home;
            m_testAt = now + uint64_t(LogoDropMs / 8);
            break;
        case 16: // the launcher's Support and About tabs, and the problem report over Support
            m_page = Page::Settings;
            m_tab = SettingsTab::Support;
            m_testAt = now + 400;
            break;
        case 17:
            m_tab = SettingsTab::About;
            m_testAt = now + 400;
            break;
        case 18:
            m_tab = SettingsTab::Support;
            OpenProblemReport();
            m_testAt = now + 500;
            break;
        default:
            m_running = false;
            break;
        }
    }

}

using namespace launcher_ui;
using namespace LauncherTheme;

void LauncherTheme::Apply(ImGuiStyle& style)
{
    style.WindowRounding = 0;
    style.DisplaySafeAreaPadding = ImVec2(12, 12); // tooltips and popups keep a margin from the window's edges
    style.ChildRounding = RadiusLarge;
    style.FrameRounding = RadiusSmall;
    style.PopupRounding = RadiusSmall;
    style.GrabRounding = RadiusSmall;
    style.TabRounding = RadiusSmall;
    style.ScrollbarRounding = RadiusSmall;
    style.FramePadding = ImVec2(14, 9);
    style.ItemSpacing = ImVec2(12, 12);
    style.CellPadding = ImVec2(10, 12);
    style.GrabMinSize = 22;
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 1.5f;
    style.FrameBorderSize = 1;
    style.FontSizeBase = TextSize;
    style.DisabledAlpha = ui::DisabledAlpha; // the one disabled look: everything at this alpha
    ImVec4* colors = style.Colors;
    auto color = [](unsigned rgb, float alpha = 1.0f) {
        return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
    };
    colors[ImGuiCol_Text] = color(Text);
    colors[ImGuiCol_TextDisabled] = color(TextDim);
    colors[ImGuiCol_WindowBg] = color(SkyTop, 0);
    colors[ImGuiCol_ChildBg] = color(Panel, 0);
    colors[ImGuiCol_PopupBg] = color(Panel, 0.98f);
    colors[ImGuiCol_Border] = color(Border);
    colors[ImGuiCol_FrameBg] = color(Field);
    colors[ImGuiCol_FrameBgHovered] = color(FieldHover);
    colors[ImGuiCol_FrameBgActive] = color(FieldActive);
    colors[ImGuiCol_Button] = color(PanelRaised);
    colors[ImGuiCol_ButtonHovered] = color(PanelRaisedHover);
    colors[ImGuiCol_ButtonActive] = color(PanelRaisedActive);
    colors[ImGuiCol_Header] = color(PanelRaised);
    colors[ImGuiCol_HeaderHovered] = color(PanelRaisedHover);
    colors[ImGuiCol_HeaderActive] = color(Accent, 0.35f);
    colors[ImGuiCol_SliderGrab] = color(Accent);
    colors[ImGuiCol_SliderGrabActive] = color(AccentHover);
    colors[ImGuiCol_CheckMark] = color(Accent);
    colors[ImGuiCol_PlotHistogram] = color(Accent);
    colors[ImGuiCol_ScrollbarBg] = color(SkyTop, 0);
    colors[ImGuiCol_ScrollbarGrab] = color(Border);
    colors[ImGuiCol_NavCursor] = color(Accent);
    colors[ImGuiCol_TableRowBg] = color(Panel, 0);
    colors[ImGuiCol_TableBorderLight] = color(Border, 0.5f);
}

std::span<const LauncherGameEntry> LauncherGameCatalog(const LauncherConfig& config)
{
    // The names are game_names.h's (the short one: the sidebar's rows).
    static const LauncherGameEntry catalog[] = {
        { "ssa", GameName("ssa", GameNameForm::Short), 0x6BBAFF, true },
        { "giants", GameName("giants", GameNameForm::Short), 0xFFC65C, true },
        { "swapforce", GameName("swapforce", GameNameForm::Short), 0xFF55A8, true },
        { "trapteam", GameName("trapteam", GameNameForm::Short), 0x6DF0B4, false },
        { "superchargers", GameName("superchargers", GameNameForm::Short), 0xFF8A5C, false },
        { "racing", GameName("racing", GameNameForm::Short), 0xE85D75, false },
        { "imaginators", GameName("imaginators", GameNameForm::Short), 0xB49BFF, false },
    };
    static const auto experimental = [] {
        std::array<LauncherGameEntry, std::size(catalog)> entries;
        std::copy(std::begin(catalog), std::end(catalog), entries.begin());
        entries[3].enabled = true;
        return entries;
    }();
    return ExperimentalTrapTeam(config) ? std::span<const LauncherGameEntry>(experimental) : std::span<const LauncherGameEntry>(catalog);
}

std::string LauncherLanguageLabel(const LauncherConfig& config, const GameInfo& game)
{
    std::string code = SettingGet(config, game.id, "language", "en");
    for (const Choice& choice : GiantsLanguageChoices) // a superset of the other game's
        if (code == choice.value)
            return choice.label;
    return code;
}

std::string LauncherVoiceLabel(const LauncherConfig& config, const GameInfo& game)
{
    const bool giants = game.id == std::string_view("giants");
    std::string code = SettingGet(config, game.id, "voice_language", giants ? "" : "en");
    if (code.empty())
        return LauncherLanguageLabel(config, game); // Giants' voices follow the text
    for (const Choice& choice : GiantsLanguageChoices)
        if (code == choice.value)
            return choice.label;
    return code;
}

bool LauncherFullscreen(const LauncherConfig& config)
{
    return ParseBool(SettingGet(config, "launcher", "fullscreen", SettingGet(config, "", "fullscreen")));
}

std::string LauncherFormatSize(uint64_t bytes)
{
    return ui::FormatSize(bytes); // the one size format (components/format.h)
}

std::string LauncherMiddleEllipsis(std::string_view text, size_t max)
{
    // Counts UTF-8 characters, not bytes.
    std::vector<size_t> starts{ 0 };
    for (size_t at = 0; at < text.size();)
    {
        unsigned char lead = static_cast<unsigned char>(text[at]);
        at = std::min(text.size(), at + (lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4));
        starts.push_back(at);
    }
    size_t characters = starts.size() - 1;
    if (characters <= max)
        return std::string(text);
    if (max <= 3)
        return std::string(text.substr(0, starts[max]));
    size_t keep = max - 3, head = (keep + 1) / 2, tail = keep / 2;
    return std::string(text.substr(0, starts[head])) + "..." + std::string(text.substr(starts[characters - tail]));
}

std::string LauncherDisplayName(std::string_view xdbfName, const GameInfo& game)
{
    return xdbfName.empty() ? std::string(game.name) : std::string(xdbfName);
}

std::string RunLauncherWindow(const GameInfo& game, const std::string& configPath, LauncherConfig& config, const std::string& previous,
    std::string* installed, const GameInfo** played, const LauncherOptions& options)
{
    if (!previous.empty())
        config[game.id]["iso"] = previous;
    App app(game, configPath, config, options);
    std::string chosen = app.Run();
    if (installed != nullptr)
        *installed = app.ChosenRuntime();
    if (played != nullptr)
        *played = &app.ChosenGame();
    return chosen;
}
