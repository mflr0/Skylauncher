// The launcher window (Dear ImGui on SDL_Renderer): a sidebar of games, then the Home, Play and Settings pages and the
// launcher's widgets. The collection's pages are in collection_page.cpp; the class is declared in launcher_app.h.
// Every choice is written to launcher.ini, which the game runtime reads (docs/settings.md lists the keys).
#include "launcher/ui/launcher_ui.h"
#include "launcher/env.h"
#include "launcher/ui/text_fit.h"
#include "launcher/install_checklist.h"
#include "launcher/ui/launcher_app.h"
#include "shared/collection/datadir.h"
#include "shared/embedded_fonts.h"
#include "assets/launcher_assets.h"
#include "launcher/theme/launcher_theme.h"
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
    ImFont* g_boldFont = nullptr; // Outfit SemiBold, for AddBoldText (set by App::LoadFonts)

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
        std::ifstream file(path, std::ios::binary | std::ios::ate);
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
                        metadata = ReadTitleMetadata(xex);
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
        g_boldFont = m_boldFont = io.Fonts->AddFontFromMemoryTTF(m_fontData[2].data(), int(m_fontData[2].size()), TextSize, &config);
        m_titleFont = io.Fonts->AddFontFromMemoryTTF(m_fontData[1].data(), int(m_fontData[1].size()), TitleSize, &config);
        m_monoFont = io.Fonts->AddFontDefaultVector(); // logs: Dear ImGui's own monospace font
        return m_textFont != nullptr && m_boldFont != nullptr && m_titleFont != nullptr && m_monoFont != nullptr;
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
            m_testSidebar = LauncherEnv("SKY_LAUNCHER_TEST_SIDEBAR") != nullptr;
            if (const char* kit = LauncherEnv("SKY_LAUNCHER_TEST_KIT"))
                m_testKit = kit;
            m_testCopy = LauncherEnv("SKY_LAUNCHER_TEST_COPY") != nullptr;
            m_testSeatX = LauncherEnv("SKY_LAUNCHER_TEST_SEATX") != nullptr;
            m_testButtons = LauncherEnv("SKY_LAUNCHER_TEST_BUTTONS") != nullptr;
            m_testUninstall = LauncherEnv("SKY_LAUNCHER_TEST_UNINSTALL") != nullptr;
            m_testLogo = LauncherEnv("SKY_LAUNCHER_TEST_LOGO") != nullptr;
            m_testKeys = LauncherEnv("SKY_LAUNCHER_TEST_KEYS") != nullptr;
        }
        RefreshGamepads();
        m_github = CreateGitHubMarkTexture(m_renderer, 128);
        m_portal.Start();
        return true;
    }

    App::App(const GameInfo& game, const std::string& configPath, LauncherConfig& config, const LauncherOptions& options)
        : m_configPath(configPath), m_config(config)
    {
        m_s = &SlotFor(game);
        SetActiveGame(game.id);
        m_autoPlay = options.play;
        if (options.openGame)
            m_page = Page::Play; // a shortcut opens on the game, not on the launcher's menu
        for (const GameInfo& each : g_games)
        {
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
        return *m_slots.back();
    }

    // The sidebar's switch. Nothing closes: the pages now draw this game's state. Its disc check starts on the first
    // visit and runs in the background; a game seen before is shown as it was left.
    void App::SelectGame(const GameInfo& game)
    {
        m_s = &SlotFor(game);
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
        // Something moves: a check, an install's spinner, an update's bar, a held button, a text cursor, a drag.
        if (AnyVerifying() || AnyInstalling() || m_updates.Busy() || ImGui::IsAnyItemActive())
            return 0;
        if (m_page == Page::Play && m_logoDropAt != 0 && SDL_GetTicks() < m_logoDropAt + uint64_t(LogoDropMs) + 20) // the logo is falling
            return 0;
        constexpr int Idle = 500;
        int wait = Idle;
        uint64_t now = SDL_GetTicks();
        for (const ToastItem& item : m_toasts)
            if (item.until > now) // wake to take it down
                wait = std::min<int>(wait, int(item.until - now) + 1);
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
                Set(GameInputSection(m_s->game.id).c_str(), (std::string("key_") + m_capturing->action).c_str(), SDL_GetKeyName(event.key.key));
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
            Toast(Tr("Couldn't save the settings"), true);
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
            Toast(Tr("Couldn't save the settings"), true);
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
                Toast(Tr("Game data damaged: using the disc image"), true);
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
        m_s->chosen = m_s->discPath;
        m_s->chosenRuntime = m_s->installed.runtime;
        if (!m_s->fromCopy)
            Set(m_s->game.id, "iso", m_s->chosen);
        m_running = false;
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

    // A cog of `radius` around `center`, with a hole the colour of what is behind it.
    void DrawGear(ImDrawList* draw, ImVec2 center, float radius, ImU32 ink, ImU32 hole)
    {
        constexpr int Teeth = 8;
        float half = radius * 0.2f, inner = radius * 0.7f;
        for (int i = 0; i < Teeth; i++)
        {
            float angle = float(i) * 6.2831853f / Teeth, c = std::cos(angle), s = std::sin(angle);
            auto at = [&](float along, float across) { return ImVec2(center.x + c * along - s * across, center.y + s * along + c * across); };
            draw->AddQuadFilled(at(inner, -half), at(radius, -half), at(radius, half), at(inner, half), ink);
        }
        draw->AddCircleFilled(center, radius * 0.78f, ink, 32);
        draw->AddCircleFilled(center, radius * 0.32f, hole, 24);
    }

    // A gamepad seen from above: body, two grips, a d-pad and two face buttons cut out in `hole`.
    void DrawGamepad(ImDrawList* draw, ImVec2 center, float width, ImU32 ink, ImU32 hole)
    {
        float w = width / 2, h = width * 0.3f;
        draw->AddRectFilled(ImVec2(center.x - w, center.y - h), ImVec2(center.x + w, center.y + h * 0.6f), ink, h);
        draw->AddCircleFilled(ImVec2(center.x - w * 0.62f, center.y + h * 0.45f), h * 0.62f, ink, 20);
        draw->AddCircleFilled(ImVec2(center.x + w * 0.62f, center.y + h * 0.45f), h * 0.62f, ink, 20);
        ImVec2 pad(center.x - w * 0.5f, center.y - h * 0.15f);
        float arm = h * 0.42f, thick = h * 0.14f;
        draw->AddRectFilled(ImVec2(pad.x - arm, pad.y - thick), ImVec2(pad.x + arm, pad.y + thick), hole);
        draw->AddRectFilled(ImVec2(pad.x - thick, pad.y - arm), ImVec2(pad.x + thick, pad.y + arm), hole);
        draw->AddCircleFilled(ImVec2(center.x + w * 0.38f, center.y - h * 0.05f), h * 0.17f, hole, 12);
        draw->AddCircleFilled(ImVec2(center.x + w * 0.62f, center.y - h * 0.35f), h * 0.17f, hole, 12);
    }

    // Bold is Outfit SemiBold at the current size; `passes` extra copies BoldOffset apart make it heavier still.
    void AddBoldText(ImDrawList* draw, ImVec2 at, ImU32 color, const char* text, int passes)
    {
        ImGui::PushFont(g_boldFont, ImGui::GetFontSize());
        for (int i = 0; i < passes; i++)
            draw->AddText(ImVec2(at.x + BoldOffset * i, at.y), color, text);
        ImGui::PopFont();
    }

    void App::BoldText(const char* text, unsigned color)
    {
        ImGui::PushFont(m_boldFont, ImGui::GetFontSize());
        ImVec2 size = ImGui::CalcTextSize(text);
        ImGui::PopFont();
        AddBoldText(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), Rgb(color), text);
        ImGui::Dummy(size);
    }

    void App::Title(const char* text, float size, unsigned color)
    {
        ImGui::PushFont(m_titleFont, size);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(color));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    bool App::BigButton(const char* label, ImVec2 size, unsigned color, unsigned dark, unsigned ink, bool enabled)
    {
        ImVec2 position = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(!enabled);
        bool pressed = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
        bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        ImGui::EndDisabled();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float lift = held ? 2.0f : 0.0f;
        unsigned alpha = enabled ? 255 : 90;
        if (hovered && enabled)
            draw->AddRectFilled(ImVec2(position.x - 4, position.y - 4), ImVec2(position.x + size.x + 4, position.y + size.y + 4), Rgb(color, 60), Radius + 4);
        draw->AddRectFilled(ImVec2(position.x, position.y + 6), ImVec2(position.x + size.x, position.y + size.y), Rgb(dark, alpha), Radius);
        draw->AddRectFilled(ImVec2(position.x, position.y + lift), ImVec2(position.x + size.x, position.y + size.y - 6 + lift), Rgb(color, alpha), Radius);
        draw->AddRectFilled(ImVec2(position.x + 8, position.y + 6 + lift), ImVec2(position.x + size.x - 8, position.y + size.y * 0.42f + lift), IM_COL32(255, 255, 255, enabled ? 40 : 15), Radius - 4);
        ImGui::PushFont(m_titleFont, size.y * 0.42f);
        const char* labelEnd = std::strstr(label, "##"); // "##id" alone draws no text
        ImVec2 text = ImGui::CalcTextSize(label, labelEnd);
        draw->AddText(ImVec2(position.x + (size.x - text.x) / 2, position.y + (size.y - 6 - text.y) / 2 + lift), Rgb(ink, alpha), label, labelEnd);
        ImGui::PopFont();
        FocusRing(Radius);
        return pressed && enabled;
    }

    bool App::PillButton(const char* label, bool primary, float width, bool bold, unsigned fill, unsigned fillHover, unsigned fillActive, int chevron)
    {
        unsigned ink = primary ? AccentInk : Text;
        ImGui::PushStyleColor(ImGuiCol_Button, Rgb(primary ? Accent : fill));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb(primary ? AccentHover : fillHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Rgb(primary ? AccentDark : fillActive));
        ImGui::PushStyleColor(ImGuiCol_Text, chevron != 0 ? IM_COL32(0, 0, 0, 0) : Rgb(ink));
        ImGui::PushStyleColor(ImGuiCol_Border, primary ? ImVec4(0, 0, 0, 0) : ImGui::GetStyleColorVec4(ImGuiCol_Border)); // the grey frame shows as a dark ring round the light fill
        if (bold)
            ImGui::PushFont(m_boldFont, ImGui::GetFontSize());
        bool pressed = ImGui::Button(label, ImVec2(PillWidth(label, width, bold, chevron), 40));
        if (bold)
            ImGui::PopFont();
        ImGui::PopStyleColor(5);
        if (chevron != 0)
            DrawLabelChevron(label, ink, chevron, bold);
        return pressed;
    }

    float App::PillWidth(const char* label, float width, bool bold, int chevron)
    {
        if (bold)
            ImGui::PushFont(m_boldFont, ImGui::GetFontSize());
        float need = ImGui::CalcTextSize(label, nullptr, true).x + 2 * ImGui::GetStyle().FramePadding.x + 16;
        if (chevron != 0)
            need += ImGui::GetFontSize() * 0.9f;
        if (bold)
            ImGui::PopFont();
        return std::max(width, std::ceil(need));
    }

    void App::DrawChevron(ImDrawList* draw, ImVec2 centre, float size, unsigned color, int dir)
    {
        float half = size / 2, rise = size / 4 * (dir > 0 ? 1 : -1);
        ImVec2 points[3] = { ImVec2(centre.x - half, centre.y - rise), ImVec2(centre.x, centre.y + rise), ImVec2(centre.x + half, centre.y - rise) };
        draw->AddPolyline(points, 3, Rgb(color), ImDrawFlags_None, std::max(1.5f, size / 6));
    }

    void App::DrawLabelChevron(const char* label, unsigned color, int dir, bool bold)
    {
        if (bold)
            ImGui::PushFont(m_boldFont, ImGui::GetFontSize());
        const char* end = std::strstr(label, "##");
        ImVec2 text = ImGui::CalcTextSize(label, end);
        float size = ImGui::GetFontSize() * 0.6f, gap = ImGui::GetFontSize() * 0.4f;
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        float left = (min.x + max.x - (text.x + gap + size)) / 2, middle = (min.y + max.y) / 2;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddText(ImVec2(left, middle - text.y / 2), Rgb(color), label, end);
        DrawChevron(draw, ImVec2(left + text.x + gap + size / 2, middle), size, color, dir);
        if (bold)
            ImGui::PopFont();
    }

    bool App::Toggle(const char* id, bool* value)
    {
        ImVec2 position = ImGui::GetCursorScreenPos();
        ImVec2 size(54, 30);
        bool pressed = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
        if (pressed)
            *value = !*value;
        bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), Rgb(*value ? (hovered ? AccentHover : Accent) : hovered ? FieldHover : Field), size.y / 2);
        draw->AddRect(position, ImVec2(position.x + size.x, position.y + size.y), Rgb(*value ? AccentDark : hovered ? BorderBright : Border), size.y / 2, 2.0f);
        float knob = *value ? position.x + size.x - size.y / 2 : position.x + size.y / 2;
        draw->AddCircleFilled(ImVec2(knob, position.y + size.y / 2), size.y / 2 - 5, Rgb(*value ? AccentInk : TextSecondary));
        FocusRing(size.y / 2);
        return pressed;
    }

    void App::FocusRing(float rounding, float gap, ImDrawFlags flags)
    {
        if (!ImGui::IsItemFocused() || !ImGui::GetIO().NavVisible)
            return;
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - gap, min.y - gap), ImVec2(max.x + gap, max.y + gap), Rgb(Accent),
            std::max(0.0f, rounding + gap), flags, 2.0f);
    }

    bool App::BeginCombo(const char* id, const char* preview)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList(); // the box's window, not the list's
        ImVec2 at = ImGui::GetCursorScreenPos();
        const float width = ImGui::CalcItemWidth(), height = ImGui::GetFrameHeight();
        const bool open = ImGui::BeginCombo(id, preview, ImGuiComboFlags_NoArrowButton);
        DrawChevron(draw, ImVec2(at.x + width - 18, at.y + height / 2), 10, TextSecondary, open ? -1 : 1);
        return open;
    }

    bool App::Option(const char* label, bool selected, ImGuiSelectableFlags flags, ImVec2 size)
    {
        // ImGui's highlight is square: it is left out, and drawn with rounded corners under the row's text.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->ChannelsSplit(2);
        draw->ChannelsSetCurrent(1);
        ImGui::PushStyleColor(ImGuiCol_Header, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0);
        const bool pressed = ImGui::Selectable(label, selected, flags, size);
        ImGui::PopStyleColor(3);
        draw->ChannelsSetCurrent(0);
        if (selected || ImGui::IsItemHovered())
            draw->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), Rgb(FieldActive), RadiusSmall);
        draw->ChannelsMerge();
        return pressed;
    }

    bool App::LevelSlider(const char* id, int* value, int min, int max, float width)
    {
        const float height = ImGui::GetFrameHeight(), knob = 11, track = 8;
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(width, height), ImGuiButtonFlags_EnableNav);
        bool changed = false;
        const float left = at.x + knob + 2, right = at.x + width - knob - 2;
        if (ImGui::IsItemActive())
        {
            int next = min + int(std::lround(std::clamp((ImGui::GetIO().MousePos.x - left) / (right - left), 0.0f, 1.0f) * float(max - min)));
            changed = next != *value;
            *value = next;
        }
        else if (ImGui::IsItemFocused())
        {
            int step = (ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? 1 : 0) - (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? 1 : 0);
            if (step != 0)
            {
                *value = std::clamp(*value + step, min, max);
                changed = true;
            }
        }
        const float t = max > min ? float(*value - min) / float(max - min) : 0.0f, x = left + (right - left) * t, mid = at.y + height / 2;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const bool hovered = ImGui::IsItemHovered();
        draw->AddRectFilled(ImVec2(at.x, mid - track / 2), ImVec2(at.x + width, mid + track / 2), Rgb(Field), track / 2);
        draw->AddRect(ImVec2(at.x, mid - track / 2), ImVec2(at.x + width, mid + track / 2), Rgb(Border), track / 2);
        draw->AddRectFilled(ImVec2(at.x, mid - track / 2), ImVec2(x, mid + track / 2), Rgb(Accent), track / 2);
        draw->AddCircleFilled(ImVec2(x, mid), knob, Rgb(hovered || ImGui::IsItemActive() ? AccentHover : Accent), 24);
        FocusRing(height / 2);
        return changed;
    }

    void App::BeginPageColumn(const char* id, float width, bool alwaysScrollbar)
    {
        constexpr float Bleed = 4;
        float below = ImGui::GetStyle().WindowPadding.y;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - Bleed);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Bleed, 0));
        ImGui::BeginChild(id, ImVec2(width + 2 * Bleed, ImGui::GetContentRegionAvail().y + below),
            ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoBackground | (alwaysScrollbar ? ImGuiWindowFlags_AlwaysVerticalScrollbar : 0));
        ImGui::PopStyleVar();
        ImGui::SetScrollX(0.0f);
        m_columnBelow = below;
    }

    void App::EndPageColumn()
    {
        ImGui::Dummy(ImVec2(0, m_columnBelow));
        ImGui::EndChild();
    }

    bool App::HoldButton(const char* label, float width, const char* hint)
    {
        ImVec2 at = ImGui::GetCursorScreenPos(), size(PillWidth(label, width, true), 40);
        ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
        // Held: pressed and, for the mouse, still under the pointer (leaving cancels). Space, Enter and the pad's A hold it too.
        bool mouse = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
        bool held = active && (!mouse || hovered);
        bool inputDown = mouse || ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_Enter) ||
            ImGui::IsKeyDown(ImGuiKey_KeypadEnter) || ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown);
        bool done = false;
        if (m_testHoldAt != 0 && m_un.holdId != 0 && ImGui::GetItemID() == m_un.holdId) // the screenshot test holds it down
            held = inputDown = true;
        float fill = m_hold.Update(ImGui::GetItemID(), held, inputDown, SDL_GetTicks(), done);

        // Colours through GetColorU32: they fade with the rest under BeginDisabled.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float rounding = ImGui::GetStyle().FrameRounding;
        ImVec2 end(at.x + size.x, at.y + size.y);
        draw->AddRectFilled(at, end, ImGui::GetColorU32(Rgb(Danger, held ? 150 : hovered ? 110 : 70)), rounding);
        if (fill > 0)
            draw->AddRectFilled(at, ImVec2(at.x + size.x * fill, end.y), ImGui::GetColorU32(Rgb(Danger, 200)), rounding);
        const char* labelEnd = std::strstr(label, "##"); // "Delete##trash" shows "Delete"
        ImGui::PushFont(m_boldFont, ImGui::GetFontSize());
        ImVec2 text = ImGui::CalcTextSize(label, labelEnd);
        draw->AddText(ImVec2(at.x + (size.x - text.x) / 2, at.y + (size.y - text.y) / 2), ImGui::GetColorU32(Rgb(Text)), label, labelEnd);
        ImGui::PopFont();
        FocusRing(rounding);
        if (hovered && hint != nullptr)
            ImGui::SetTooltip("%s", hint);
        return done;
    }

    void App::SettingRow(const char* label, const char* hint)
    {
        ImGui::TableNextRow(ImGuiTableRowFlags_None, 62);
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 3));
        ImGui::TextUnformatted(label);
        if (hint != nullptr)
        {
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
            ImGui::TextWrapped("%s", hint);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::PopStyleVar();
        ImGui::TableSetColumnIndex(1);
    }

    bool App::BeginSettingsTable(const char* id)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 14));
        ImGui::PushStyleColor(ImGuiCol_TableBorderLight, Rgb(Border, 110));
        bool open = ImGui::BeginTable(id, 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp);
        if (open)
        {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, 320);
        }
        else
        {
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        return open;
    }

    void App::EndSettingsTable()
    {
        ImGui::EndTable();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    void App::BeginCard(const char* id, ImVec2 size, ImGuiChildFlags flags)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Rgb(Panel, 235));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, RadiusLarge);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
        ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened | flags);
        ImGui::SetScrollX(0.0f);
    }

    void App::EndCard()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

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
        const ArtTexture* art = CachedArt(m_s->game.id, LauncherAssets::Role::HeroBackground, BackdropArtW);
        SDL_Texture* background = art != nullptr ? art->texture : nullptr;
        if (background != nullptr)
        {
            float w, h;
            SDL_GetTextureSize(background, &w, &h);
            ImVec2 page(SidebarW, 0), pageSize(size.x - SidebarW, size.y);
            float scale = std::max(pageSize.x / w, pageSize.y / h), u = pageSize.x / (w * scale), v = pageSize.y / (h * scale);
            draw->AddImage(Ref(background), page, size, ImVec2((1 - u) / 2, (1 - v) / 2), ImVec2((1 + u) / 2, (1 + v) / 2));
            // Shading, so the logo and the white text read on any painting: darker at the top, lighter behind the logo,
            // dark again towards the band; and a shadow the sidebar casts on its left edge.
            float middle = size.y * 0.38f, low = size.y - BottomBarH;
            draw->AddRectFilledMultiColor(page, ImVec2(size.x, middle), Rgb(SkyTop, BackdropTopShade), Rgb(SkyTop, BackdropTopShade),
                Rgb(SkyTop, 40), Rgb(SkyTop, 40));
            draw->AddRectFilledMultiColor(ImVec2(page.x, middle), ImVec2(size.x, low), Rgb(SkyTop, 40), Rgb(SkyTop, 40),
                Rgb(SkyTop, BackdropBottomShade), Rgb(SkyTop, BackdropBottomShade));
            draw->AddRectFilled(ImVec2(page.x, low), size, Rgb(SkyTop, BackdropBottomShade));
            draw->AddRectFilledMultiColor(page, ImVec2(page.x + 120, size.y), IM_COL32(0, 0, 0, 90), IM_COL32(0, 0, 0, 0),
                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 90));
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
    }

    // A game's state in a few words, for the sidebar and Home, and the label of the game page's main button: the one place
    // that decides, read from the game's own slot (nullptr: not opened this session). The page's other lines (the disc's
    // status, the install card) say more about the same state. The table is in docs/launcher_ui.md.
    App::SlotLine App::SlotStatus(GameSlot* slot)
    {
        using S = SlotState;
        auto line = [](S state, const char* text, unsigned dot, const char* button, bool enabled = true, float progress = -1) {
            return SlotLine{ text, dot, progress, state, button, enabled };
        };
        if (slot == nullptr)
            return line(S::Closed, Tr("Open"), Accent, Tr(strings::ChooseDisc));
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
            result.text = Tf("{} {} %", verb, int(std::floor(std::clamp(list.overall, 0.0f, 1.0f) * 100)));
            return result;
        }
        if (slot->disc == DiscState::Verifying)
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

    void App::DrawSidebar()
    {
        // A fixed column (SidebarW wide, never resized or scrolled): the games, then the settings gear at the bottom.
        ImVec2 size = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(SidebarW, size.y));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Rgb(Sidebar));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, Pad));
        ImGui::Begin("##sidebar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushStyleColor(ImGuiCol_NavCursor, 0); // the rows draw their own ring (FocusRing), not ImGui's as well
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float rowW = SidebarW;

        // A row across the whole sidebar, like the Minecraft Launcher: lighter when hovered, a bar on the left when selected.
        auto row = [&](const char* id, float height, bool selected, bool enabled, ImVec2& at) {
            at = ImVec2(0, ImGui::GetCursorScreenPos().y);
            ImGui::SetCursorScreenPos(at);
            ImGui::BeginDisabled(!enabled);
            bool pressed = ImGui::InvisibleButton(id, ImVec2(rowW, height), ImGuiButtonFlags_EnableNav) && enabled;
            bool hovered = ImGui::IsItemHovered();
            ImGui::EndDisabled();
            if (selected || hovered)
                draw->AddRectFilled(at, ImVec2(rowW, at.y + height), Rgb(selected ? SidebarSelected : SidebarHover));
            if (selected)
                draw->AddRectFilled(at, ImVec2(SidebarBarW, at.y + height), Rgb(SidebarBar));
            FocusRing(8, -4); // inside the row, rounded, clear of the window's edges
            return pressed;
        };

        // The title is the Home button; the launcher's version sits beside it, small and dim. The title is as large as the
        // column leaves room for next to the version.
        {
            ImVec2 titleAt = ImGui::GetCursorScreenPos();
            const std::string version = "v" + LauncherVersion();
            ImGui::PushFont(m_boldFont, VersionSize);
            ImVec2 versionSize = ImGui::CalcTextSize(version.c_str());
            ImGui::PopFont();
            const float room = SidebarW - 2 * 18 - versionSize.x - 8; // the sidebar's padding, then the gap to the version
            ImGui::PushFont(m_titleFont, TitleSize + 4);
            const float wide = ImGui::CalcTextSize("ARKEYAN LAUNCHER").x;
            ImGui::PopFont();
            const float titleFontSize = std::min(TitleSize + 4, std::floor((TitleSize + 4) * room / wide));
            ImGui::PushFont(m_titleFont, titleFontSize);
            ImVec2 titleSize = ImGui::CalcTextSize("ARKEYAN LAUNCHER");
            ImGui::PopFont();
            if (ImGui::InvisibleButton("##home", titleSize, ImGuiButtonFlags_EnableNav))
                GoTo(Page::Home);
            bool hovered = ImGui::IsItemHovered();
            if (hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            FocusRing(RadiusSmall);
            ImGui::SetCursorScreenPos(titleAt);
            Title("ARKEYAN LAUNCHER", titleFontSize, hovered ? 0xFFFFFF : SidebarInk);
            ImGui::PushFont(m_boldFont, VersionSize);
            draw->AddText(ImVec2(titleAt.x + titleSize.x + 8, titleAt.y + titleSize.y - versionSize.y - 3), Rgb(TextDim, 200), version.c_str());
            ImGui::PopFont();
        }
        ImGui::PushFont(nullptr, SidebarSmallSize);
        ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", Tr("Native PC ports"));
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 16));
        ImGui::PushFont(nullptr, CaptionSize + 1);
        BoldText(Tr("YOUR GAMES"), TextDim);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 2));

        // The games: only those with a runtime can be selected.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 0));
        for (const LauncherGameEntry& entry : LauncherGameCatalog())
        {
            ImGui::PushID(entry.id);
            ImVec2 position;
            // The game already opened this session has its slot: its icon and disc state show here, not just the shown one's.
            GameSlot* slot = nullptr;
            for (const auto& opened : m_slots)
                if (std::string_view(opened->game.id) == entry.id)
                    slot = opened.get();
            // Selected only once the player has picked it: Play is that game's page.
            bool selected = (m_page == Page::Play || m_page == Page::GameSettings) && std::string_view(entry.id) == m_s->game.id;
            if (row("game", GameRowH, selected, entry.enabled && !m_uninstalled, position))
            {
                if (std::string_view(entry.id) != m_s->game.id)
                    SelectGame(*FindGame(entry.id));
                GoTo(Page::Play);
            }
            if (std::string_view(entry.id) == m_s->game.id && (m_page == Page::Play || m_page == Page::Home || m_page == Page::GameSettings))
                TakeFocus(FocusTo::Sidebar);
            // The game's logo (with "Skylanders" on it) once its disc has been read, here or in an earlier session; a game
            // that isn't out yet keeps its neutral badge: its initial on its colour.
            ImVec2 box(position.x + 14, position.y + (GameRowH - GameLogoH) / 2);
            const ArtTexture* logo = entry.enabled ? CachedArt(entry.id, LauncherAssets::Role::Logo, int(GameLogoW * 2.5f)) : nullptr;
            if (logo != nullptr)
                DrawLogo(draw, *logo, box, ImVec2(GameLogoW, GameLogoH), 1, false);
            else
            {
                ImVec2 badge(box.x + GameLogoW / 2, box.y + GameLogoH / 2);
                draw->AddCircleFilled(badge, GameBadgeR, Rgb(entry.accent, entry.enabled ? 255 : 90));
                ImGui::PushFont(m_titleFont, GameBadgeR);
                char letter[2] = { entry.name[0], 0 };
                ImVec2 letterSize = ImGui::CalcTextSize(letter);
                draw->AddText(ImVec2(badge.x - letterSize.x / 2, badge.y - letterSize.y / 2), Rgb(BadgeInk), letter);
                ImGui::PopFont();
            }
            // The name, as large as fits; the game's version small after it on the same line; the disc's state under it.
            const std::string version = slot != nullptr && entry.enabled ? GameVersion(*slot) : std::string();
            const std::string versionText = version.empty() ? std::string() : "\xC2\xB7 " + version;
            float nameX = position.x + GameTextX, nameW = rowW - GameTextX - 10, nameSize = SidebarTextSize - 1;
            auto versionWidth = [&] {
                if (versionText.empty())
                    return 0.0f;
                ImGui::PushFont(nullptr, SidebarSmallSize - 2);
                float width = ImGui::CalcTextSize(versionText.c_str()).x;
                ImGui::PopFont();
                return width;
            };
            const float versionW = versionWidth() + (versionText.empty() ? 0.0f : 6.0f); // 6: the gap after the name
            ImGui::PushFont(m_boldFont, nameSize);
            while (nameSize > SidebarSmallSize && ImGui::CalcTextSize(entry.name).x + versionW > nameW)
            {
                ImGui::PopFont();
                ImGui::PushFont(m_boldFont, --nameSize);
            }
            draw->AddText(ImVec2(nameX, position.y + 9), Rgb(entry.enabled ? SidebarInk : TextDim), entry.name);
            float nameEnd = nameX + ImGui::CalcTextSize(entry.name).x;
            ImGui::PopFont();
            if (!versionText.empty())
            {
                ImGui::PushFont(nullptr, SidebarSmallSize - 2);
                draw->AddText(ImVec2(nameEnd + 6, position.y + 9 + (nameSize - ImGui::GetFontSize()) * 0.8f),
                    Rgb(TextSecondary), versionText.c_str());
                ImGui::PopFont();
            }
            ImGui::PushFont(nullptr, SidebarSmallSize - 2);
            SlotLine line = entry.enabled ? SlotStatus(slot) : SlotLine{ Tr("Coming later"), Accent };
            float statusX = nameX, statusY = position.y + 32;
            if (entry.enabled)
            {
                draw->AddCircleFilled(ImVec2(statusX + 3.5f, statusY + ImGui::GetFontSize() / 2 + 1), 3.5f, Rgb(line.dot));
                statusX += 12;
            }
            // Fits the column at the smallest window too: cut with "..." rather than run over the edge.
            std::string status = FitText(line.text, rowW - 10 - statusX, [](const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x; });
            draw->AddText(ImVec2(statusX, statusY), Rgb(entry.enabled ? TextSecondary : TextDim, entry.enabled ? 255 : 200), status.c_str());
            if (m_testSidebar)
                m_drawn[std::string("sidebar:") + entry.id] = line.text;
            ImGui::PopFont();
            if (line.progress >= 0) // a thin bar in the game's colour along the row's foot, over the text's column
            {
                const GameStyle& style = StyleOf(entry.id);
                float left = nameX, right = rowW - 14, y = position.y + GameRowH - 7;
                draw->AddRectFilled(ImVec2(left, y), ImVec2(right, y + 3), Rgb(0xFFFFFF, 28), 1.5f);
                float fill = std::floor(left + (right - left) * std::clamp(line.progress, 0.0f, 1.0f));
                if (fill > left)
                    draw->AddRectFilled(ImVec2(left, y), ImVec2(std::max(fill, left + 3), y + 3), Rgb(style.top), 1.5f);
            }
            ImGui::PopID();
        }

        // The collection, then Settings (the gear), at the bottom of the sidebar; selected while their page is shown.
        const float listEnd = ImGui::GetCursorScreenPos().y;
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - Pad - 2 * GameRowH);
        // A hairline sets the two apart from the games, midway between them (none when they nearly touch).
        const float lineY = std::round((listEnd + ImGui::GetCursorScreenPos().y) / 2);
        if (ImGui::GetCursorScreenPos().y - listEnd >= 24)
            draw->AddLine(ImVec2(18, lineY), ImVec2(rowW - 18, lineY), Rgb(Border, 160));
        ImVec2 at;
        bool collection = m_page == Page::Collection;
        if (row("##collection", GameRowH, collection, true, at))
            GoTo(collection ? m_backPage : Page::Collection);
        if (collection)
            TakeFocus(FocusTo::Sidebar);
        {
            // Three figure cards fanned out, in the logos' column; the label in the names' column.
            ImVec2 center(at.x + 14 + GameLogoW / 2, at.y + GameRowH / 2);
            for (int i = -1; i <= 1; i++)
            {
                ImVec2 card(center.x + i * 7.0f, center.y + (i == 0 ? -3.0f : 0.0f));
                draw->AddRectFilled(ImVec2(card.x - 7, card.y - 10), ImVec2(card.x + 7, card.y + 10), Rgb(i == 0 ? SidebarInk : SidebarInkDim, i == 0 ? 255 : 170), 3);
                draw->AddRect(ImVec2(card.x - 7, card.y - 10), ImVec2(card.x + 7, card.y + 10), Rgb(Sidebar), 3, 0, 1.5f);
            }
            ImGui::PushFont(m_titleFont, 19);
            const char* name = Tr("COLLECTION");
            ImVec2 label = ImGui::CalcTextSize(name);
            draw->AddText(ImVec2(at.x + GameTextX, center.y - label.y / 2), Rgb(SidebarInk), name);
            ImGui::PopFont();
        }
        bool open = m_page == Page::Settings;
        if (row("##settings", GameRowH, open, true, at))
            GoTo(open ? m_backPage : Page::Settings);
        if (open)
            TakeFocus(FocusTo::Sidebar);
        ImGui::PopStyleVar();
        ImVec2 center(at.x + 14 + GameLogoW / 2, at.y + GameRowH / 2);
        draw->AddCircleFilled(center, GearButton / 2, Rgb(SidebarInk), 32);
        DrawGear(draw, center, GearButton * 0.32f, Rgb(Sidebar), Rgb(SidebarInk));
        ImGui::PushFont(m_titleFont, 19);
        const char* name = Tr("SETTINGS");
        ImVec2 label = ImGui::CalcTextSize(name);
        draw->AddText(ImVec2(at.x + GameTextX, center.y - label.y / 2), Rgb(SidebarInk), name);
        ImGui::PopFont();

        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    // The launcher's menu, opened by the ARKEYAN LAUNCHER title: the games as a library (a card each, with its art from the
    // player's disc), the games still to come under them, the project's links bottom right.
    void App::DrawHome()
    {
        constexpr const char* Project = "https://github.com/mflr0/Skylauncher";
        constexpr float Icon = 40, ReportW = 190, CardGap = 20, CardRadius = RadiusLarge;
        ImVec2 size = ImGui::GetIO().DisplaySize;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float width = ImGui::GetContentRegionAvail().x;

        Title(Tr("LIBRARY"), HeadlineSize, Text);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::TextUnformatted(Tr("Game art comes from your own disc."));
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 10));

        // The playable games: a card each, the game's painting behind its logo, its state at the bottom.
        std::vector<const LauncherGameEntry*> playable, later;
        for (const LauncherGameEntry& entry : LauncherGameCatalog())
            (entry.enabled ? playable : later).push_back(&entry);
        float cardW = (width - CardGap * float(playable.size() - 1)) / float(std::max<size_t>(playable.size(), 1));
        float cardH = std::min(cardW * 0.6f, size.y - origin.y - 300);
        ImVec2 row = ImGui::GetCursorScreenPos();
        for (size_t i = 0; i < playable.size(); i++)
        {
            const LauncherGameEntry& entry = *playable[i];
            ImVec2 at(row.x + float(i) * (cardW + CardGap), row.y), end(at.x + cardW, at.y + cardH);
            ImGui::SetCursorScreenPos(at);
            ImGui::PushID(entry.id);
            if (ImGui::InvisibleButton("##card", ImVec2(cardW, cardH), ImGuiButtonFlags_EnableNav))
            {
                if (std::string_view(entry.id) != m_s->game.id)
                    SelectGame(*FindGame(entry.id));
                GoTo(Page::Play);
            }
            if (i == 0)
                TakeFocus(FocusTo::Page);
            bool hovered = ImGui::IsItemHovered();
            if (hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopID();

            const GameStyle& style = StyleOf(entry.id);
            draw->AddRectFilled(ImVec2(at.x, at.y + 6), ImVec2(end.x, end.y + 6), IM_COL32(0, 0, 0, 70), CardRadius);
            if (const ArtTexture* art = CachedArt(entry.id, LauncherAssets::Role::HeroBackground, 640))
            {
                float w, h;
                SDL_GetTextureSize(art->texture, &w, &h);
                float scale = std::max(cardW / w, cardH / h), u = cardW / (w * scale), v = cardH / (h * scale);
                draw->AddImageRounded(Ref(art->texture), at, end, ImVec2((1 - u) / 2, (1 - v) / 2), ImVec2((1 + u) / 2, (1 + v) / 2),
                    IM_COL32(255, 255, 255, hovered ? 255 : 225), CardRadius);
            }
            else
            {
                // Not read yet: the game's colours, as its buttons have them.
                GradientRect(draw, at, end, Rgb(style.bottom), Rgb(style.outline), CardRadius);
            }
            // The bottom fades to dark for the name; the logo sits in the upper part.
            float fade = at.y + cardH * 0.45f;
            GradientRect(draw, ImVec2(at.x, fade), end, Rgb(SkyTop, 0), Rgb(SkyTop, 235), CardRadius, ImDrawFlags_RoundCornersBottom);
            if (const ArtTexture* logo = CachedArt(entry.id, LauncherAssets::Role::Logo, 640))
                DrawLogo(draw, *logo, ImVec2(at.x + cardW * 0.12f, at.y + cardH * 0.08f), ImVec2(cardW * 0.76f, cardH * 0.56f));
            else
            {
                // The name in the title font, smaller when it would not fit the card's width with a margin.
                float titleSize = TitleSize + 6;
                ImGui::PushFont(m_titleFont, titleSize);
                ImVec2 text = ImGui::CalcTextSize(entry.name);
                if (text.x > cardW * 0.84f)
                {
                    ImGui::PopFont();
                    ImGui::PushFont(m_titleFont, titleSize * cardW * 0.84f / text.x);
                    text = ImGui::CalcTextSize(entry.name);
                }
                draw->AddText(ImVec2(at.x + (cardW - text.x) / 2, at.y + cardH * 0.36f - text.y / 2), Rgb(Text), entry.name);
                ImGui::PopFont();
            }
            GameSlot* slot = nullptr;
            for (const auto& opened : m_slots)
                if (std::string_view(opened->game.id) == entry.id)
                    slot = opened.get();
            SlotLine line = SlotStatus(slot);
            // The full name; on a narrow card the short one (without "Skylanders: "), smaller if it still does not fit
            // between the card's 20 px margins. Never cut.
            const char* name = FindGame(entry.id) != nullptr ? FindGame(entry.id)->name : entry.name;
            ImGui::PushFont(m_boldFont, SidebarTextSize);
            if (ImGui::CalcTextSize(name).x > cardW - 40)
                name = entry.name;
            if (float nameW = ImGui::CalcTextSize(name).x; nameW > cardW - 40)
            {
                ImGui::PopFont();
                ImGui::PushFont(m_boldFont, SidebarTextSize * (cardW - 40) / nameW);
            }
            draw->AddText(ImVec2(at.x + 20, end.y - 56), Rgb(Text), name);
            ImGui::PopFont();
            ImGui::PushFont(nullptr, SmallSize);
            draw->AddCircleFilled(ImVec2(at.x + 24, end.y - 22), 4, Rgb(line.dot));
            draw->AddText(ImVec2(at.x + 34, end.y - 22 - ImGui::GetFontSize() / 2 - 1), Rgb(TextSecondary), line.text.c_str());
            if (m_testSidebar)
                m_drawn[std::string("home:") + entry.id] = line.text;
            ImGui::PopFont();
            // Under the pointer: the game's own colour around the card.
            draw->AddRect(at, end, hovered ? Rgb(style.glow) : IM_COL32(255, 255, 255, 28), CardRadius, 0, hovered ? 2.5f : 1.0f);
            FocusRing(CardRadius);
        }
        ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + cardH + 30));

        // The games still to come: their names, in their colours, quietly.
        ImGui::PushFont(nullptr, CaptionSize + 1);
        BoldText(Tr("COMING LATER"), TextDim);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4));
        ImVec2 chip = ImGui::GetCursorScreenPos();
        ImGui::PushFont(m_boldFont, SmallSize + 1);
        for (const LauncherGameEntry* entry : later)
        {
            ImVec2 text = ImGui::CalcTextSize(entry->name);
            float chipW = text.x + 46;
            if (chip.x + chipW > origin.x + width)
                chip = ImVec2(origin.x, chip.y + 46);
            draw->AddRectFilled(chip, ImVec2(chip.x + chipW, chip.y + 36), Rgb(Panel, 220), 18);
            draw->AddRect(chip, ImVec2(chip.x + chipW, chip.y + 36), Rgb(Border, 160), 18);
            draw->AddCircleFilled(ImVec2(chip.x + 20, chip.y + 18), 6, Rgb(entry->accent, 200));
            draw->AddText(ImVec2(chip.x + 34, chip.y + 18 - text.y / 2), Rgb(TextSecondary), entry->name);
            chip.x += chipW + 10;
        }
        ImGui::PopFont();

        ImVec2 iconAt(size.x - Pad - Icon, size.y - Pad - Icon);
        ImGui::SetCursorScreenPos(ImVec2(iconAt.x - 14 - PillWidth(Tr("Report a bug"), ReportW, true), iconAt.y));
        if (PillButton(Tr("Report a bug"), false, ReportW, true, BottomBarRaised, BottomBarRaisedHover, BottomBarRaisedActive))
            SDL_OpenURL((std::string(Project) + "/issues/new").c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("Opens a new issue on GitHub"));

        ImGui::SetCursorScreenPos(iconAt);
        if (ImGui::InvisibleButton("##github", ImVec2(Icon, Icon), ImGuiButtonFlags_EnableNav))
            SDL_OpenURL(Project);
        FocusRing(Icon / 2);
        bool hovered = ImGui::IsItemHovered();
        if (hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", Tr("Arkeyan Launcher on GitHub"));
        }
        if (m_github != nullptr)
            ImGui::GetWindowDrawList()->AddImage(Ref(m_github), iconAt, ImVec2(iconAt.x + Icon, iconAt.y + Icon), ImVec2(0, 0), ImVec2(1, 1),
                Rgb(hovered ? Accent : Text));
    }

    void App::DrawPlay()
    {
        // Edge to edge: the game's logo on top, a band with the main button below it.
        ImVec2 position = ImGui::GetCursorScreenPos();
        ImVec2 available = ImGui::GetContentRegionAvail();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 end(position.x + available.x, position.y + available.y - BottomBarH);
        float heroH = end.y - position.y;

        // Hero: the game's logo from the player's disc over its painting (DrawBackdrop shades it).
        if (m_dragOver)
            draw->AddRect(ImVec2(position.x + 6, position.y + 6), ImVec2(end.x - 6, end.y - 6), Rgb(AccentDark), RadiusLarge, 4.0f);

        // The logo (else the game's name), as large as fits and centred between the language menu and the disc status.
        float areaTop = position.y + Pad * 2.5f, areaBottom = end.y - 128, areaH = areaBottom - areaTop;
        const ArtTexture* logo = m_s->logo ? &m_s->logo : CachedArt(m_s->game.id, LauncherAssets::Role::Logo, 0);
        if (m_s->installView)
        {
            float cardW = std::min(available.x - 80, 680.0f);
            DrawInstall(ImVec2(position.x + (available.x - cardW) / 2, areaTop), ImVec2(cardW, end.y - Pad - areaTop));
        }
        else if (logo != nullptr)
            DrawHeroLogo(draw, *logo, ImVec2(position.x + available.x * 0.14f, areaTop), ImVec2(available.x * 0.72f, areaH));
        else
        {
            ImGui::PushFont(m_titleFont, HeroSize);
            float wrap = available.x - 96;
            ImVec2 text = ImGui::CalcTextSize(m_s->game.name, nullptr, false, wrap);
            ImVec2 at(position.x + (available.x - text.x) / 2, areaTop + (areaH - text.y) / 2);
            ImGui::SetCursorScreenPos(at);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Text));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap); // window coordinates, not the screen's
            ImGui::TextUnformatted(m_s->game.name);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        // The disc, bottom left of the hero: the image's file name, its status under it ("Change disc..." on the right).
        ImGui::SetCursorScreenPos(ImVec2(position.x + 40, end.y - 116));
        ImGui::BeginGroup();
        if (!m_s->installView) // the progress card takes the whole hero
        {
            ImGui::BeginGroup();
            // The file name on top, then "DISC IMAGE:" and the status on the line below.
            std::string file = m_s->fromCopy ? Tr("Copied game data")
                : m_s->discPath.empty() ? Tr("No disc image yet") : LauncherMiddleEllipsis(std::filesystem::path(m_s->discPath).filename().string(), 40);
            ImGui::PushFont(nullptr, TitleSize);
            BoldText(file.c_str(), Text);
            ImGui::PopFont();
            if (!m_s->discPath.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", m_s->discPath.c_str());
            BoldText(m_s->fromCopy ? Tr("GAME DATA:") : Tr("DISC IMAGE:"), TextDim);
            ImGui::SameLine(0, 6);
            switch (m_s->disc)
            {
            case DiscState::None:
                ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tr("Drop your disc image (.iso) here."));
                break;
            case DiscState::Verifying:
                ImGui::TextColored(ImColor(Rgb(Play)), "%s...", m_s->verify.Stage().c_str());
                ImGui::ProgressBar(m_s->verify.progress, ImVec2(360, 8), "");
                break;
            case DiscState::Ready:
                ImGui::TextColored(ImColor(Rgb(Ready)), "%s", (m_s->fromCopy ? Tf("Game data copied: {}", LauncherFormatSize(m_s->discSize))
                                                                 : Tf("Disc verified: {}", LauncherFormatSize(m_s->discSize))).c_str());
                if (!m_s->fromCopy && m_s->copy.state == gamedata::State::Damaged)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Danger));
                    ImGui::TextWrapped("%s", Tr("Game data damaged: using the disc image."));
                    ImGui::PopStyleColor();
                }
                if (NeedsInstall())
                {
                    ImGui::PushTextWrapPos(available.x - 40); // window coordinates
                    ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
                    ImGui::TextWrapped("%s", Tf("Builds the game on your PC: about {} min, {} GB, internet needed once.", InstallMinutesFor(m_s->game.id), InstallGigabytes).c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopTextWrapPos();
                }
                else if (UpdateAvailable())
                {
                    // What UPDATE does: from the built version to the published one, the download (the kit, if it is not here yet)
                    // and the time, then a small link to play what is built (offline, in a hurry).
                    const GameUpdate* pending = PendingUpdate(*m_s);
                    std::string size = Tr("no download");
                    if (KitNeedsFetch(*m_s))
                    {
                        const ReleaseInfo kit = m_updates.Snapshot().kit;
                        if (const ReleaseAsset* asset = FindAsset(kit, KitAssetName()); asset != nullptr && asset->size > 0)
                            size = Tf("{} download", LauncherFormatSize(asset->size));
                    }
                    ImGui::PushTextWrapPos(available.x - 40); // window coordinates
                    ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
                    ImGui::TextWrapped("%s", (m_s->installed.gameVersion.empty()
                            ? Tf("Update to {}: {}, up to about {} min.", pending->version, size, InstallMinutesFor(m_s->game.id))
                            : Tf("Update {} to {}: {}, up to about {} min.", m_s->installed.gameVersion, pending->version, size, InstallMinutesFor(m_s->game.id))).c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopTextWrapPos();
                    if (!Installing())
                    {
                        ImGui::PushFont(nullptr, SmallSize);
                        const char* text = Tr("Play without updating");
                        ImVec2 extent = ImGui::CalcTextSize(text);
                        ImVec2 linkAt = ImGui::GetCursorScreenPos();
                        linkAt.y += 4;
                        ImGui::SetCursorScreenPos(linkAt);
                        ImGui::InvisibleButton("##playwithout", ImVec2(extent.x, extent.y + 4), ImGuiButtonFlags_EnableNav);
                        bool hovered = ImGui::IsItemHovered();
                        if (hovered)
                            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        FocusRing(4);
                        unsigned ink = hovered ? Text : TextSecondary;
                        AddBoldText(draw, linkAt, Rgb(ink), text);
                        draw->AddLine(ImVec2(linkAt.x, linkAt.y + extent.y + 1), ImVec2(linkAt.x + extent.x, linkAt.y + extent.y + 1), Rgb(ink), 1.0f);
                        if (ImGui::IsItemClicked() || m_testPlayWithout)
                        {
                            m_testPlayWithout = false;
                            m_s->skipUpdate = true;
                        }
                        ImGui::PopFont();
                    }
                }
                break;
            case DiscState::Failed:
                ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Danger));
                ImGui::PushTextWrapPos(available.x - 40);
                ImGui::TextWrapped("%s", m_s->check.message.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                // The other supported game's disc: one button to use it there.
                if (const GameInfo* other = FindGame(m_s->check.otherGame); other != nullptr && m_s->check.status == GameSourceStatus::WrongGame)
                {
                    if (PillButton(Tf("Use it for {}", other->name).c_str(), true, 0, true))
                        UseDiscForOtherGame();
                }
                break;
        }
        ImGui::EndGroup();
        }
        ImGui::EndGroup();

        // Bottom band: the controller and the main button in the middle, "Change disc..." on the right.
        ImVec2 bar(position.x, end.y), barEnd(end.x, position.y + available.y);
        GradientRect(draw, bar, barEnd, Rgb(BottomBarTop), Rgb(BottomBar), 0);
        draw->AddLine(bar, ImVec2(barEnd.x, bar.y), Rgb(0xFFFFFF, 26), 1.0f);
        float middle = bar.y + BottomBarH / 2;

        SlotLine state = SlotStatus(m_s);
        bool ready = m_s->disc == DiscState::Ready, verifying = state.state == SlotState::Checking, installing = Installing();
        bool install = ready && NeedsInstall();
        bool update = ready && !install && UpdateAvailable();
        const char* label = state.button;
        // The band's group, centred in it on whole points (crisp edges at any scale): the controller picker, then the
        // action button (the main action and the game's settings), both PlayButtonH tall. The smallest window fits it.
        const float groupW = ControllerW + ControllerGap + PlayButtonW + PlayGearW;
        float groupX = std::floor(bar.x + (barEnd.x - bar.x - groupW) / 2), buttonY = std::floor(middle - PlayButtonH / 2);
        float mainX = groupX + ControllerW + ControllerGap;
        if (m_testSidebar)
            m_drawn["button"] = std::string(label) + (state.buttonEnabled ? "" : " (off)");
        MainAction action{ label, state.buttonEnabled && !m_testButtonsOff };
        if (installing)
            action.progress = MakeInstallChecklist(InstallSnapshot(), SDL_GetTicks()).overall;
        else if (verifying)
            action.progress = m_s->verify.progress;
        switch (DrawMainAction(ImVec2(mainX, buttonY), action))
        {
        case MainActionHit::Main:
            if (!ready)
                Browse();
            else if (install || update)
                StartInstall(); // which checks first that the image is still there; an update rebuilds the game
            else
                PlayNow();
            break;
        case MainActionHit::Settings:
            GoTo(Page::GameSettings);
            break;
        case MainActionHit::None:
            break;
        }

        // Controller picker, left of the main button: what the game listens to, a menu of the choices above it.
        ImVec2 controllerAt(groupX, buttonY), controllerSize(ControllerW, PlayButtonH);
        ImGui::SetCursorScreenPos(controllerAt);
        if (ImGui::InvisibleButton("##controller", controllerSize, ImGuiButtonFlags_EnableNav) || m_testControllers)
        {
            m_testControllers = false;
            RefreshGamepads();
            ImGui::OpenPopup("##controllers");
        }
        FocusRing(Radius);
        bool pickerHovered = ImGui::IsItemHovered(), pickerOpen = ImGui::IsPopupOpen("##controllers");
        ImVec2 controllerEnd(controllerAt.x + controllerSize.x, controllerAt.y + controllerSize.y);
        draw->AddRectFilled(controllerAt, controllerEnd, Rgb(pickerHovered || pickerOpen ? BottomBarRaisedHover : BottomBarRaised), Radius);
        // Same surface as "Change disc...": the band's button grey, a thin border, the accent while its menu is open.
        draw->AddRect(controllerAt, controllerEnd, Rgb(pickerOpen ? Accent : BottomBarBorder), Radius, 0, pickerOpen ? 2.0f : 1.0f);
        float controllerMid = controllerAt.y + controllerSize.y / 2;
        DrawGamepad(draw, ImVec2(controllerAt.x + 34, controllerMid), 38, Rgb(BottomBarInk), Rgb(pickerHovered || pickerOpen ? BottomBarRaisedHover : BottomBarRaised));
        std::string device = CurrentDevice();
        std::string deviceName = device == "auto" ? Tr("Automatic") : device == "keyboard" ? Tr("Keyboard") : LauncherMiddleEllipsis(DeviceLabel(), 13);
        ImGui::PushFont(nullptr, SmallSize);
        AddBoldText(draw, ImVec2(controllerAt.x + 64, controllerMid - 20), Rgb(BottomBarInk), Tr("CONTROLLER"));
        ImGui::PopFont();
        AddBoldText(draw, ImVec2(controllerAt.x + 64, controllerMid - 1), Rgb(BottomBarInk), deviceName.c_str(), BolderPasses);
        ImVec2 chevron(controllerEnd.x - 20, controllerMid);
        draw->AddTriangleFilled(ImVec2(chevron.x - 6, chevron.y + 3), ImVec2(chevron.x + 6, chevron.y + 3), ImVec2(chevron.x, chevron.y - 4), Rgb(SidebarInk));
        if (pickerHovered && !pickerOpen)
            ImGui::SetTooltip("%s", Tf("Controller: {}", DeviceLabel()).c_str());

        // PLAY WITH, above the picker: rows of one height, the radio and its label centred on the row, the heading on the
        // labels' column; an opaque surface with a border over the art behind it.
        ImGui::SetNextWindowPos(ImVec2(controllerAt.x, controllerAt.y - 8), ImGuiCond_Always, ImVec2(0, 1));
        ImGui::SetNextWindowSizeConstraints(ImVec2(ControllerW, 0), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Rgb(BottomBarRaised));
        ImGui::PushStyleColor(ImGuiCol_Border, Rgb(BottomBarBorder));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 10));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        if (ImGui::BeginPopup("##controllers"))
        {
            constexpr float RowH = 38, Dot = 6, DotX = 12 + Dot, LabelX = DotX + Dot + 12;
            std::vector<std::pair<std::string, std::string>> options = DeviceOptions();
            // As wide as the picker, never wider: a long name is cut to the row.
            const float rowW = ControllerW - 12, labelW = rowW - LabelX - 12;
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + LabelX);
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", Tr(strings::PlayWith));
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 2));
            ImDrawList* popup = ImGui::GetWindowDrawList();
            for (const auto& [value, name] : options)
            {
                bool current = value == device;
                ImVec2 at = ImGui::GetCursorScreenPos();
                ImGui::PushID(value.c_str());
                if (Option("##device", current, 0, ImVec2(rowW, RowH)))
                    SetDevice(value);
                ImGui::PopID();
                float mid = at.y + RowH / 2;
                popup->AddCircle(ImVec2(at.x + DotX, mid), Dot, Rgb(current ? Accent : TextDim), 16, 2.0f);
                if (current)
                    popup->AddCircleFilled(ImVec2(at.x + DotX, mid), Dot - 3, Rgb(Accent), 12);
                const std::string shown = FitText(name, labelW, [](const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x; });
                popup->AddText(ImVec2(at.x + LabelX, mid - ImGui::GetTextLineHeight() / 2), Rgb(Text), shown.c_str());
                if (shown != name && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", name.c_str());
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);

        // The portal's state, small, under the main button (above the band it ran into the disc's lines).
        {
            std::string line = m_portal.Current().Line();
            if (!line.empty())
            {
                ImGui::PushFont(nullptr, SmallSize);
                ImGui::SetCursorScreenPos(ImVec2(mainX + (PlayButtonW - ImGui::CalcTextSize(line.c_str()).x) / 2, buttonY + PlayButtonH + 4));
                DrawPortalStatus();
                ImGui::PopFont();
            }
        }

        // "Change disc...", at the right of the disc's lines once a disc is verified; styled like the controller picker.
        // The game's other settings (video, languages, saves, data) are behind the gear next to the main button.
        if (ready && !installing && !m_s->installView && !m_s->fromCopy) // the copy replaces the disc image
        {
            constexpr float ChangeDiscMin = 160;
            const float ChangeDiscW = PillWidth(Tr("Change disc..."), ChangeDiscMin, true);
            ImGui::SetCursorScreenPos(ImVec2(mainX + PlayButtonW + PlayGearW - ChangeDiscW, end.y - 112)); // its right edge is the action button's
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Radius);
            if (PillButton(Tr("Change disc..."), false, ChangeDiscW, true, BottomBarRaised, BottomBarRaisedHover, BottomBarRaisedActive))
                Browse();
            ImGui::PopStyleVar();
        }
    }

    void App::DrawPortalStatus()
    {
        std::string line = m_portal.Current().Line();
        if (line.empty())
            return;
        ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", line.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr(strings::PortalTip));
    }

    void App::DrawControls()
    {
        std::string current = CurrentDevice();
        Title(Tr("Controller"), TitleSize, Text);
        ImGui::SetNextItemWidth(440);
        if (BeginCombo("##device", DeviceLabel().c_str()))
        {
            for (const auto& [value, label] : DeviceOptions())
                if (Option(label.c_str(), value == current))
                    SetDevice(value);
            ImGui::EndCombo();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        size_t gamepads = m_gamepads.size();
        std::string connected = gamepads == 0 ? Tr(strings::NoGamepad) : gamepads == 1 ? Tr(strings::OneGamepad) : Tf(strings::Gamepads, gamepads);
        ImGui::Text("%s", Tf("{} Xbox, PlayStation and Switch pads work.", connected).c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 12));

        Title(Tr("Portal of Power"), TitleSize, Text);
        Combo("##portal", "portal", "mode", PortalChoices, 0, 440);
        bool autoOpen = ParseBool(SettingGet(m_config, "portal", "toybox_auto_open", "1"));
        ImGui::Dummy(ImVec2(0, 10));
        if (Toggle("##toyboxautoopen", &autoOpen))
            Set("portal", "toybox_auto_open", autoOpen ? "1" : "0");
        ImGui::SameLine(0, 14);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Tr("Auto-open Toy Box"));
        DrawPortalTest();
        ImGui::Dummy(ImVec2(0, 12));

        Title(Tr("Button glyphs"), TitleSize, Text);
        Combo("##glyphs", "input", "glyphs", GlyphChoices, 0, 440);
        ImGui::Dummy(ImVec2(0, 12));

        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::TextUnformatted(Tr("F6 or Tab opens the Toy Box, F7 adds a figure, F8 clears them."));
        ImGui::PopStyleColor();
    }

    // The tabs of a settings page: words on one line, an accent line under the selected one (like the sidebar's bar).
    // LB / RB change tab, as in the console launchers.
    bool App::DrawTabBar(const char* const* labels, int count, int* current)
    {
        bool changed = false;
        if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) && !ImGui::IsAnyItemActive())
        {
            int step = ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) ? -1 : ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) ? 1 : 0;
            if (step != 0 && std::clamp(*current + step, 0, count - 1) != *current)
            {
                *current = std::clamp(*current + step, 0, count - 1);
                changed = true;
                RequestFocus(FocusTo::Page); // the ring follows to the new tab
            }
        }
        ImGui::PushFont(nullptr, SidebarTextSize);
        for (int i = 0; i < count; i++)
        {
            const char* label = Tr(labels[i]);
            bool active = *current == i;
            ImVec2 at = ImGui::GetCursorScreenPos(), text = ImGui::CalcTextSize(label);
            ImVec2 size(text.x + BoldOffset, text.y + 10 + TabUnderlineH);
            if (ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav))
            {
                changed = changed || *current != i;
                *current = i;
            }
            bool hovered = ImGui::IsItemHovered();
            FocusRing(RadiusSmall, 6);
            if (active)
                TakeFocus(FocusTo::Page);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            unsigned color = active || hovered ? Text : TextDim;
            if (active)
            {
                AddBoldText(draw, at, Rgb(color), label);
                draw->AddRectFilled(ImVec2(at.x, at.y + size.y - TabUnderlineH), ImVec2(at.x + size.x, at.y + size.y), Rgb(AccentDark));
            }
            else
                draw->AddText(at, Rgb(color), label);
            ImGui::SameLine(0, TabGap);
        }
        ImGui::PopFont();
        ImGui::NewLine();
        ImGui::Dummy(ImVec2(0, 4));
        if (changed)
        {
            m_capturing = nullptr;
            m_s->saveBackupsLoaded = m_s->dataSizesLoaded = false; // the saves and the folders may have changed since the tab was open
        }
        return changed;
    }

    // The launcher's settings, apart from any game's: the controller (all games read the same), the launcher
    // itself, support and About. Each game has its own settings page (game_settings_page.cpp).
    void App::DrawSettings()
    {
        // A centred column: settings read badly when they stretch across a wide window.
        float available = ImGui::GetContentRegionAvail().x;
        float width = std::min(available, 940.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) / 2);
        BeginPageColumn("##settingscolumn", width, true); // the scrollbar is always there: the card is as wide on every tab

        static const char* const Tabs[] = { N_("Controls"), N_("Launcher"), N_("Support"), N_("About") };
        Title(Tr("LAUNCHER SETTINGS"), HeadlineSize, Text);
        ImGui::Dummy(ImVec2(0, 10));
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
        EndPageColumn();
    }

    void App::DrawLauncherTab()
    {
        if (m_un.stage != UninstallStage::Closed)
        {
            DrawUninstall(); // the list and the result take the tab
            return;
        }
        DrawLauncherLanguage();
        ImGui::Dummy(ImVec2(0, 16));
        Title(Tr("Updates"), TitleSize, Text);
        DrawUpdates();
        ImGui::Dummy(ImVec2(0, 16));
        Title(Tr("Data folder"), TitleSize, Text);
        {
            // The path breaks after a separator, never in the middle of a name.
            const std::string path = datadir::Text(LauncherDataFolder());
            const float room = ImGui::GetContentRegionAvail().x;
            std::string line;
            for (size_t from = 0; from < path.size();)
            {
                size_t to = path.find_first_of("/\\", from);
                to = to == std::string::npos ? path.size() : to + 1;
                const std::string part = path.substr(from, to - from);
                if (!line.empty() && ImGui::CalcTextSize((line + part).c_str()).x > room)
                {
                    ImGui::TextUnformatted(line.c_str());
                    line.clear();
                }
                line += part;
                from = to;
            }
            ImGui::TextUnformatted(line.c_str());
        }
        if (PillButton(Tr("Show in folder"), false, 260))
        {
            std::error_code error;
            std::filesystem::create_directories(LauncherDataFolder(), error);
            OpenFolder(LauncherDataFolder());
        }
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
        ImGui::TextWrapped("%s", Tr(strings::DataFolderHint));
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 16));
        DrawUninstallSection();
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
        Title(Tr("Language"), TitleSize, Text);
        const std::string setting = SettingGet(m_config, "launcher", "language");
        std::string shown = i18n::Current();
        for (const i18n::Language& language : i18n::Languages())
            if (language.code == i18n::Current())
                shown = language.name;
        if (setting.empty())
            shown = Tr("System default");
        ImGui::SetNextItemWidth(440);
        if (BeginCombo("##launcherlanguage", shown.c_str()))
        {
            if (Option(Tr("System default"), setting.empty()))
                Set("launcher", "language", "");
            for (const i18n::Language& language : i18n::Languages())
                if (Option(language.name.c_str(), language.code == setting))
                    Set("launcher", "language", language.code);
            ImGui::EndCombo();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::TextWrapped("%s", Tr(strings::LauncherLanguageHint));
        ImGui::PopStyleColor();
    }

    void App::DrawSupport()
    {
        Title(Tr("Support"), TitleSize, Text);
        DrawLogs();
    }

    void App::GoTo(Page page)
    {
        if (m_uninstalled)
            return; // nothing else to do in a launcher that has removed itself (and nothing to write back)
        if ((page == Page::Settings || page == Page::Collection) && (m_page == Page::Home || m_page == Page::Play))
            m_backPage = m_page;
        else if (page == Page::GameSettings)
            m_backPage = Page::Play; // a game's settings are opened from its page, and close back to it
        else if (page == Page::Settings && m_page == Page::GameSettings)
            m_backPage = Page::Play;
        else if (page == Page::Collection && m_page == Page::GameSettings)
            m_backPage = Page::Play;
        m_page = page;
        m_capturing = nullptr;
        RequestFocus(FocusTo::Page);
    }

    // B / Escape when nothing else took it: out of Settings and Collection, else to the sidebar.
    void App::Back()
    {
        if (m_page == Page::Settings || m_page == Page::Collection || m_page == Page::GameSettings)
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

    constexpr float ToastWidth = 420, ToastMargin = 20; // the stack's text width and its distance from the window's top and right
    constexpr size_t MaxToasts = 4;

    void App::Toast(std::string text, bool error)
    {
        // Long enough to read: 2.5 s, and more for a long message, up to 6 s. A failure ("Couldn't ...") stays until closed.
        error = error || text.rfind("Couldn't", 0) == 0;
        uint64_t readMs = std::clamp<uint64_t>(2500 + 45 * (text.size() > 40 ? text.size() - 40 : 0), 2500, 6000);
        uint64_t until = error ? 0 : SDL_GetTicks() + readMs;
        for (ToastItem& item : m_toasts)
            if (item.text == text) // the same words again: the one already shown, kept longer
            {
                item.until = until;
                return;
            }
        m_toasts.push_back({ std::move(text), until });
        if (m_toasts.size() > MaxToasts)
            m_toasts.erase(m_toasts.begin());
    }

    void App::DrawToast()
    {
        uint64_t now = SDL_GetTicks();
        std::erase_if(m_toasts, [&](const ToastItem& item) { return item.until != 0 && now > item.until; });
        if (m_toasts.empty())
            return;
        // Bottom right, stacked upwards, newest at the bottom, above the game page's band and Home's buttons so no control is
        // covered; each at most 420 points wide (a long one wraps). Errors have a cross; a click on any message closes it.
        ImVec2 size = ImGui::GetIO().DisplaySize;
        float wrap = std::min(ToastWidth, size.x - SidebarW - 2 * Pad - 80);
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        float bottom = size.y - ToastMargin - (m_page == Page::Play || m_page == Page::Home ? BottomBarH : 0.0f);
        int closed = -1;
        for (size_t i = m_toasts.size(); i-- > 0;)
        {
            const ToastItem& item = m_toasts[i];
            bool error = item.until == 0;
            ImVec2 text = ImGui::CalcTextSize(item.text.c_str(), nullptr, false, wrap);
            float width = text.x + 40 + (error ? 28 : 0);
            ImVec2 position(size.x - ToastMargin - width, bottom - text.y - 24), end(size.x - ToastMargin, bottom);
            // A window of its own, so a click closes the message and does not reach what is under it.
            ImGui::SetNextWindowPos(position);
            ImGui::SetNextWindowSize(ImVec2(end.x - position.x, end.y - position.y));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
            ImGui::Begin(fmt::format("##toast{}", i).c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);
            if (ImGui::InvisibleButton("##close", ImVec2(end.x - position.x, end.y - position.y)))
                closed = int(i);
            bool hovered = ImGui::IsItemHovered();
            ImGui::End();
            ImGui::PopStyleVar(2);
            draw->AddRectFilled(position, end, Rgb(PanelRaised, 245), Radius);
            draw->AddRect(position, end, Rgb(error ? Danger : Accent, 200), Radius, 2.0f);
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(position.x + 20, position.y + 12), Rgb(Text), item.text.c_str(), nullptr, wrap);
            if (error) // a cross
            {
                ImVec2 centre(end.x - 22, position.y + 24);
                draw->AddLine(ImVec2(centre.x - 5, centre.y - 5), ImVec2(centre.x + 5, centre.y + 5), Rgb(hovered ? Text : TextDim), 2.0f);
                draw->AddLine(ImVec2(centre.x - 5, centre.y + 5), ImVec2(centre.x + 5, centre.y - 5), Rgb(hovered ? Text : TextDim), 2.0f);
            }
            bottom = position.y - 8;
        }
        if (closed >= 0)
            m_toasts.erase(m_toasts.begin() + closed);
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
        if (!ImGui::IsPopupOpen("##quit"))
            ImGui::OpenPopup("##quit");
        ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(ImVec2(std::min(520.0f, display.x - 80), 0));
        ImGui::SetNextWindowPos(ImVec2(display.x / 2, display.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Rgb(Panel, 250));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, RadiusLarge);
        if (ImGui::BeginPopupModal("##quit", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
        {
            Title(Tr(strings::QuitTitle), TitleSize, Text);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
            ImGui::TextWrapped("%s", Tr(strings::QuitText));
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 8));
            bool keep = PillButton(Tr(strings::KeepInstalling), true, 200, true);
            if (ImGui::IsWindowAppearing())
                ImGui::SetItemDefaultFocus();
            ImGui::SameLine(ImGui::GetWindowWidth() - Pad - PillWidth(Tr(strings::Quit), 120, true));
            bool quit = PillButton(Tr(strings::Quit), false, 120, true);
            if (keep || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
            {
                m_quitPrompt = false;
                ImGui::CloseCurrentPopup();
            }
            else if (quit)
                m_running = false;
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
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
        if (m_testNav || m_testSeatX)
            frameIo.BackendFlags |= ImGuiBackendFlags_HasGamepad; // the test's pad keys count with no pad plugged in
        ImGui::NewFrame();
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
            m_reportOpen || m_whatsNewOpen || m_quitPrompt || (m_page == Page::Collection && m_s->collection->TakesBack());
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
                "14-launcher-settings", "15-disc-gone", "16-logo-drop" };
            static const char* const InstallNames[] = { "10-install", "11-installing", "12-quit-while-installing", "13-install-details",
                "14-install-failed", "15-installed", "16-game-data", "17-install-interrupted" };
            static const char* const UpdateNames[] = { "20-update-banner", "21-whats-new", "22-about", "23-about-checked", "24-update-failed" };
            static const char* const NavNames[] = { "40-nav-start", "41-nav-sidebar", "42-nav-down", "43-nav-collection", "44-nav-search",
                "45-nav-search-left", "46-nav-seat", "47-nav-choosing", "48-nav-card", "49-nav-placed", "50-nav-sheet",
                "51-nav-back-to-play", "52-nav-settings-controls", "53-nav-settings-back" };
            static const char* const UninstallNames[] = { "01-settings", "02-list", "03-ticked", "04-confirm", "05-result" };
            if (m_testButtons || m_testSidebar || m_testCopy || m_testLogo || m_testKeys || !m_testKit.empty())
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
            m_toasts.clear(); // the install's "Game installed" would sit on the banner
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
        if (now - m_testBegan > 180000)
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
        case 11:
            m_running = false;
            break;
        case 20: // offline
            StartUpdateCheck(false);
            m_testStep++;
            break;
        case 21:
            if (m_updates.Busy() || m_updateWasBusy)
                return;
            expect(!m_updates.Snapshot().checked && m_gameUpdates.empty() && BannerHeight() == 0 && m_toasts.empty(), "offline: nothing is said");
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
                Toast(Tr("Game installed: press PLAY"));
                Toast(Tr("Log copied"));
                Toast(Tr("Couldn't save the settings"), true);
                Toast(Tf("Saves restored from {}", "2026-10-26 09:30"));
                shoot("toasts-stacked", 400);
                break;
            }
            if (m_testStep == Toasts + 1)
            {
                expect(m_toasts.size() == 4 && m_toasts[2].until == 0, "four messages, the error kept");
                m_testAt = now + 3500; // the others have gone by themselves
                m_testShot = fmt::format("{:02}-toasts-error-kept", m_testStep);
                break;
            }
            if (m_testStep == Toasts + 2)
            {
                expect(m_toasts.size() == 1 && m_toasts[0].until == 0, "only the error is left");
                m_toasts.clear();
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
            { ImGuiKey_DownArrow, ImGuiKey_DownArrow },                // 42: down: past Giants and the games coming later
            { ImGuiKey_Space },                                        // 43: A: Collection, the ring on Add a figure
            { ImGuiKey_GamepadFaceUp },                                // 44: Y: the search box takes the keyboard
            { ImGuiKey_Escape },                                       // 45: B: out of the search
            { ImGuiKey_UpArrow },                                      // 46: up: to the seats, above the bar
            { ImGuiKey_Space },                                        // 47: A: choosing for that seat (lit, the bar says so)
            { ImGuiKey_DownArrow },                                    // 48: down: the shelf's first card
            { ImGuiKey_Space },                                        // 49: A: it goes on the seat, the choosing ends
            { ImGuiKey_GamepadFaceLeft },                              // 50: X: the focused card's sheet
            { ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadFaceRight },  // 51: B, B: closed, back to Play
            { ImGuiKey_Escape, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_Space, ImGuiKey_GamepadR1 }, // 52: Settings > Controls
            { ImGuiKey_Escape },                                       // 53: Escape: back to Play
        };
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
    void App::TestScript()
    {
        if (m_shots.empty() || m_testAt != 0)
            return;
        uint64_t now = SDL_GetTicks();
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

std::span<const LauncherGameEntry> LauncherGameCatalog()
{
    static const LauncherGameEntry catalog[] = {
        { "ssa", "Spyro's Adventure", "Spyro's Adventure", 0x6BBAFF, true },
        { "giants", "Giants", "Giants", 0xFFC65C, true },
        { "swapforce", "Swap Force", "Swap Force", 0xFF55A8, true },
        { "trapteam", "Trap Team", "Trap Team", 0x6DF0B4, false },
        { "superchargers", "SuperChargers", "SuperChargers", 0xFF8A5C, false },
        { "racing", "SuperChargers Racing", "SuperChargers Racing", 0xE85D75, false },
        { "imaginators", "Imaginators", "Imaginators", 0xB49BFF, false },
    };
    return catalog;
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
    constexpr double KB = 1024, MB = KB * 1024, GB = MB * 1024;
    if (bytes >= uint64_t(GB))
        return Tf("{:.1f} GB", double(bytes) / GB);
    if (bytes >= uint64_t(MB))
        return Tf("{:.0f} MB", double(bytes) / MB);
    if (bytes >= uint64_t(KB))
        return Tf("{:.0f} KB", double(bytes) / KB);
    return Tf("{} bytes", bytes);
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
