// The launcher window (Dear ImGui on SDL_Renderer): a sidebar of games, then the Home, Play and Settings pages and the
// launcher's widgets. The collection's pages are in collection_page.cpp; the class is declared in launcher_app.h.
// Every choice is written to launcher.ini, which the game runtime reads (docs/settings.md lists the keys).
#include "launcher/ui/launcher_ui.h"
#include "launcher/ui/launcher_app.h"
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
            constexpr const char* ChooseDisc = "Choose disc";
            constexpr const char* PlayWith = "PLAY WITH";
            constexpr const char* DiscGone = "\"{}\" isn't there any more. Was it moved, renamed, or is its drive disconnected? Choose it again.";
            constexpr const char* GameText = "Game text";
            constexpr const char* GameTextHint = "The game's menus, subtitles and on-screen text. Skylauncher itself is in English.";
            constexpr const char* GameVoices = "Game voices";
            constexpr const char* GameVoicesHint = "The game's spoken lines and cutscenes. They can differ from the text.";
            constexpr const char* ResetKeys = "Reset to defaults";
            constexpr const char* ResetKeysHint = "Hold to put every key back to its default.";
            constexpr const char* Reinstall = "Reinstall the game";
            constexpr const char* ReinstallHint = "Hold to build the game again from your disc (about 15 minutes).";
            constexpr const char* RestoreSaves = "Restore";
            constexpr const char* RestoreSavesHint = "Hold to restore this backup. The saves you have now are backed up first.";
            constexpr const char* OpenFigures = "Open figures folder";
            constexpr const char* NoGamepad = "No gamepad connected.";
            constexpr const char* OneGamepad = "1 gamepad connected.";
            constexpr const char* Gamepads = "{} gamepads connected.";
            constexpr const char* PortalTip = "Without a real portal the game uses the virtual portal (its Toy Box).\n"
                                              "Settings > Controls > Test the portal checks that the game sees it.";
            constexpr const char* QuitTitle = "The game is still installing";
            constexpr const char* QuitText = "If you quit now, the install stops. It carries on from this step next time.";
            constexpr const char* KeepInstalling = "Keep installing";
            constexpr const char* Quit = "Quit";
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
        std::string title = std::string("Choose your ") + game.name + " disc image";
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
        { std::lock_guard lock(mutex); stage = "Opening the disc image"; check = {}; meta = {}; assets = {}; }
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
                report(0.96f, "Reading the title and icon");
                DiscFileSystem disc;
                if (disc.Open(image.c_str()))
                {
                    if (std::vector<uint8_t> xex = disc.ReadAll("default.xex"); !xex.empty())
                        metadata = ReadTitleMetadata(xex);
                    // The game's own art, decoded from the player's disc into the local cache (never shipped).
                    report(0.98f, "Reading the game's pictures");
                    art.Init(image, metadata.titleId);
                    art.ExtractFromDisc(disc, metadata);
                    art.ExtractFigurePortraits(disc);
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
        if (const char* directory = SDL_getenv("SSA_LAUNCHER_FONT_DIR"))
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
        SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Skylauncher");
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

        if (const char* shots = SDL_getenv("SSA_LAUNCHER_SHOTS"))
        {
            m_shots = shots;
            std::error_code error;
            std::filesystem::create_directories(m_shots, error);
            if (const char* drop = SDL_getenv("SSA_LAUNCHER_TEST_DROP"))
                m_testDrop = drop;
            m_testInstall = SDL_getenv("SSA_LAUNCHER_TEST_INSTALL") != nullptr;
            m_testReal = m_testInstall && std::string_view(SDL_getenv("SSA_LAUNCHER_TEST_INSTALL")) == "real";
            m_testUpdate = SDL_getenv("SSA_LAUNCHER_TEST_UPDATE") != nullptr;
            m_testCollection = SDL_getenv("SSA_LAUNCHER_TEST_COLLECTION") != nullptr;
            m_testNav = SDL_getenv("SSA_LAUNCHER_TEST_NAV") != nullptr;
        }
        RefreshGamepads();
        m_github = CreateGitHubMarkTexture(m_renderer, 128);
        m_portal.Start();
        return true;
    }

    void App::Shutdown()
    {
        m_portal.Stop();
        m_verify.Join();
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui_ImplSDLRenderer3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
        }
        m_collection->ClearIcons();
        for (SDL_Texture* texture : { m_icon, m_logo, m_background, m_github })
            if (texture != nullptr)
                SDL_DestroyTexture(texture);
        m_icon = m_logo = m_background = m_github = nullptr;
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
        if (std::string previous = SettingGet(m_config, m_game.id, "iso"); !previous.empty() && m_shots.empty())
            StartVerify(previous);
        if (m_shots.empty() && SDL_getenv("SKYLAUNCHER_NO_UPDATE_CHECK") == nullptr)
        {
            CleanupOldUpdate(LauncherSelf()); // the .old a previous update left
            // At most once a day, and only if the player left it on.
            int64_t last = SettingInt(m_config, "update", "last_check", 0), now = int64_t(std::time(nullptr));
            if (ParseBool(SettingGet(m_config, "update", "auto_check", "1")) && (last <= 0 || now - last > 86400 || last > now))
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
            if (m_disc == DiscState::Verifying && m_verify.done)
                FinishVerify();
            PollInstall();
            PollUpdates();
            Frame();
            TestScript();
        }
        return m_chosen;
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
        if (m_disc == DiscState::Verifying || Installing() || m_updates.Busy() || ImGui::IsAnyItemActive())
            return 0;
        constexpr int Idle = 500;
        int wait = Idle;
        uint64_t now = SDL_GetTicks();
        if (!m_toast.empty() && m_toastUntil > now) // wake to take it down
            wait = std::min<int>(wait, int(m_toastUntil - now) + 1);
        // The night sky's stars twinkle, at 30 fps, while the window is seen.
        bool seen = !(SDL_GetWindowFlags(m_window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED | SDL_WINDOW_HIDDEN));
        if (seen && m_page == Page::Play && m_background == nullptr)
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
                Set("input", (std::string("key_") + m_capturing->id).c_str(), SDL_GetKeyName(event.key.key));
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
            Toast(reset ? "Zoom: automatic" : fmt::format("Zoom: {:g}%", m_uiScale * 100));
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
            if (Installing())
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
                (m_page == Page::Collection && std::filesystem::is_directory(event.drop.data))) && m_collection->Drop(event.drop.data))
                m_page = Page::Collection;
            else if (event.drop.data != nullptr && Installing())
                Toast("Wait for the installation to finish");
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
        m_uiScale = std::clamp(scale, MinUiScale, MaxUiScale);
        fprintf(stderr, "launcher: ui scale %.2f (display scale %.2f, pixel density %.2f, usable %dx%d)\n", m_uiScale,
            SDL_GetWindowDisplayScale(m_window), SDL_GetWindowPixelDensity(m_window), usable.w, usable.h);

        SDL_SetWindowMinimumSize(m_window, int(WindowMinW * m_uiScale), int(WindowMinH * m_uiScale));
        if (resize && !(SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN))
        {
            int w = int(WindowW * m_uiScale), h = int(WindowH * m_uiScale);
            if (bounded)
            {
                w = std::min(w, usable.w);
                h = std::min(h, usable.h - 40);
            }
            SDL_SetWindowSize(m_window, w, h);
            SDL_DisplayID display = SDL_GetDisplayForWindow(m_window);
            SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        }
    }

    void App::Set(const char* section, const char* key, const std::string& value)
    {
        m_config[section][key] = value;
        if (!SaveLauncherConfig(m_configPath, m_config))
            Toast("Couldn't save the settings");
    }

    void App::StartVerify(const std::string& path)
    {
        m_disc = DiscState::Verifying;
        m_discPath = path;
        m_discSize = 0;
        SDL_PathInfo info;
        if (SDL_GetPathInfo(path.c_str(), &info))
            m_discSize = info.size;
        for (SDL_Texture** texture : { &m_icon, &m_logo, &m_background })
            if (*texture != nullptr)
            {
                SDL_DestroyTexture(*texture);
                *texture = nullptr;
            }
        m_verify.Start(path, m_game);
    }

    void App::FinishVerify()
    {
        m_verify.Join();
        m_check = m_verify.check;
        if (m_check.status != GameSourceStatus::Ok)
        {
            m_disc = DiscState::Failed;
            return;
        }
        m_meta = m_verify.meta;
        if (!m_meta.iconPng.empty())
            m_icon = LoadPng(m_renderer, m_meta.iconPng);
        m_assets = m_verify.assets; // the worker cached the disc's art
        m_collection->ClearIcons(); // the collection's pictures now come from the disc
        if (LauncherAssets::Asset logo = m_assets.Get(LauncherAssets::Role::Logo); logo.valid)
            m_logo = LoadPngFile(m_renderer, logo.path);
        if (LauncherAssets::Asset background = m_assets.Get(LauncherAssets::Role::HeroBackground); background.valid)
            m_background = LoadPngFile(m_renderer, background.path);
        Set(m_game.id, "iso", m_discPath); // remembered even if the player quits without playing
        m_disc = DiscState::Ready;
        RefreshInstall();
    }

    // PLAY and INSTALL check the image first: it may have been moved, or its drive unplugged, since it was verified.
    bool App::DiscStillThere()
    {
        SDL_PathInfo info;
        if (SDL_GetPathInfo(m_discPath.c_str(), &info) && info.type == SDL_PATHTYPE_FILE)
            return true;
        m_disc = DiscState::Failed;
        m_check.status = GameSourceStatus::NotFound;
        m_check.message = fmt::format(fmt::runtime(strings::DiscGone), m_discPath);
        m_installView = m_kitUsed = false;
        m_page = Page::Play;
        return false;
    }

    void App::Browse()
    {
        std::string path = ChooseIsoDialog(m_window, m_game, m_discPath.empty() ? SettingGet(m_config, m_game.id, "iso") : m_discPath);
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
        std::vector<std::pair<std::string, std::string>> options = { { "auto", "Automatic (first gamepad, else keyboard)" }, { "keyboard", "Keyboard" } };
        for (SDL_JoystickID id : m_gamepads)
        {
            char text[64];
            SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), text, sizeof(text));
            const char* name = SDL_GetGamepadNameForID(id);
            options.emplace_back(std::string("gamepad:") + text, name != nullptr ? name : "Gamepad");
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
        return current.starts_with("gamepad:") ? "Saved gamepad (not connected)" : "Automatic (first gamepad, else keyboard)";
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

    bool App::PillButton(const char* label, bool primary, float width, bool bold, unsigned fill, unsigned fillHover, unsigned fillActive)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, Rgb(primary ? Accent : fill));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb(primary ? AccentHover : fillHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Rgb(primary ? AccentDark : fillActive));
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(primary ? AccentInk : Text));
        if (bold)
            ImGui::PushFont(m_boldFont, ImGui::GetFontSize());
        bool pressed = ImGui::Button(label, ImVec2(width, 40));
        if (bold)
            ImGui::PopFont();
        ImGui::PopStyleColor(4);
        return pressed;
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

    void App::FocusRing(float rounding, float gap)
    {
        if (!ImGui::IsItemFocused() || !ImGui::GetIO().NavVisible)
            return;
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - gap, min.y - gap), ImVec2(max.x + gap, max.y + gap), Rgb(Accent),
            std::max(0.0f, rounding + gap), 0, 2.0f);
    }

    bool App::HoldButton(const char* label, float width, const char* hint)
    {
        ImVec2 at = ImGui::GetCursorScreenPos(), size(width, 40);
        ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
        // Held: pressed and, for the mouse, still under the pointer (leaving cancels). Space, Enter and the pad's A hold it too.
        bool mouse = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
        bool held = active && (!mouse || hovered);
        bool inputDown = mouse || ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_Enter) ||
            ImGui::IsKeyDown(ImGuiKey_KeypadEnter) || ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown);
        bool done = false;
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

    template<size_t N>
    bool App::Combo(const char* label, const char* section, const char* key, const Choice (&choices)[N], int fallback, float width)
    {
        int index = ChoiceIndex(choices, SettingGet(m_config, section, key), fallback);
        bool changed = false;
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo(label, choices[index].label))
        {
            for (size_t i = 0; i < N; i++)
                if (ImGui::Selectable(choices[i].label, int(i) == index))
                {
                    Set(section, key, choices[i].value);
                    changed = true;
                }
            ImGui::EndCombo();
        }
        return changed;
    }

    void App::BeginCard(const char* id, ImVec2 size, ImGuiChildFlags flags)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Rgb(Panel, 235));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, RadiusLarge);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
        ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened | flags);
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
        // Once a disc is verified: the game's painted Skylands sky, cropped to fill, instead of the drawn stars.
        if (m_background != nullptr)
        {
            float w, h;
            SDL_GetTextureSize(m_background, &w, &h);
            float scale = std::max(size.x / w, size.y / h), u = size.x / (w * scale), v = size.y / (h * scale);
            draw->AddImage(Ref(m_background), ImVec2(0, 0), size, ImVec2((1 - u) / 2, (1 - v) / 2), ImVec2((1 + u) / 2, (1 + v) / 2));
            draw->AddRectFilled(ImVec2(0, 0), size, Rgb(SkyTop, BackdropShade)); // keeps white text readable on the bright sky
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
            FocusRing(0, -2);
            return pressed;
        };

        // The title is the Home button.
        {
            ImVec2 titleAt = ImGui::GetCursorScreenPos();
            ImGui::PushFont(m_titleFont, TitleSize + 4);
            ImVec2 titleSize = ImGui::CalcTextSize("SKYLAUNCHER");
            ImGui::PopFont();
            if (ImGui::InvisibleButton("##home", titleSize, ImGuiButtonFlags_EnableNav))
                GoTo(Page::Home);
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            FocusRing(RadiusSmall);
            ImGui::SetCursorScreenPos(titleAt);
            Title("SKYLAUNCHER", TitleSize + 4, SidebarInk);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(SidebarInkDim));
        ImGui::PushFont(nullptr, SidebarTextSize);
        BoldText("Native PC ports", SidebarInkDim);
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 14));
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(SidebarInkDim));
        ImGui::PushFont(nullptr, SidebarSmallSize);
        BoldText("YOUR GAMES", SidebarInkDim);
        ImGui::PopFont();
        ImGui::PopStyleColor();

        // The games: only those with a runtime can be selected.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 0));
        for (const LauncherGameEntry& entry : LauncherGameCatalog())
        {
            ImGui::PushID(entry.id);
            ImVec2 position;
            // Selected only once the player has picked it: Play is that game's page.
            bool selected = m_page == Page::Play && std::string_view(entry.id) == m_game.id;
            if (row("game", GameRowH, selected, entry.enabled, position))
                GoTo(Page::Play);
            if (std::string_view(entry.id) == m_game.id && (m_page == Page::Play || m_page == Page::Home))
                TakeFocus(FocusTo::Sidebar);
            // The game's icon from the player's disc once it is verified, else its initial on its colour.
            ImVec2 badge(position.x + 34, position.y + GameRowH / 2);
            if (m_icon != nullptr && std::string_view(entry.id) == m_game.id)
                draw->AddImageRounded(Ref(m_icon), ImVec2(badge.x - GameBadgeR, badge.y - GameBadgeR), ImVec2(badge.x + GameBadgeR, badge.y + GameBadgeR),
                    ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, GameBadgeR);
            else
            {
                draw->AddCircleFilled(badge, GameBadgeR, Rgb(entry.accent, entry.enabled ? 255 : 90));
                ImGui::PushFont(m_titleFont, GameBadgeR);
                char letter[2] = { entry.name[0], 0 };
                ImVec2 letterSize = ImGui::CalcTextSize(letter);
                draw->AddText(ImVec2(badge.x - letterSize.x / 2, badge.y - letterSize.y / 2), Rgb(BadgeInk), letter);
                ImGui::PopFont();
            }
            ImGui::PushFont(nullptr, SidebarTextSize);
            AddBoldText(draw, ImVec2(position.x + 62, position.y + 6), Rgb(entry.enabled ? SidebarInk : SidebarInkDim), entry.name);
            ImGui::PopFont();
            ImGui::PushFont(nullptr, SidebarSmallSize);
            const char* status = !entry.enabled ? "Coming later" : m_disc == DiscState::Ready ? "Ready to play" : "Needs your disc";
            float statusX = position.x + 62;
            if (entry.enabled)
            {
                draw->AddCircleFilled(ImVec2(statusX + 4, position.y + 40), 4, Rgb(m_disc == DiscState::Ready ? Ready : Accent));
                statusX += 14;
            }
            draw->AddText(ImVec2(statusX, position.y + 30), Rgb(SidebarInkDim), status);
            ImGui::PopFont();
            ImGui::PopID();
        }

        // The collection, then Settings (the gear), at the bottom of the sidebar; selected while their page is shown.
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - Pad - 2 * GameRowH);
        ImVec2 at;
        bool collection = m_page == Page::Collection;
        if (row("##collection", GameRowH, collection, true, at))
            GoTo(collection ? m_backPage : Page::Collection);
        if (collection)
            TakeFocus(FocusTo::Sidebar);
        {
            // Three figure cards fanned out.
            ImVec2 center(at.x + 34, at.y + GameRowH / 2);
            for (int i = -1; i <= 1; i++)
            {
                ImVec2 card(center.x + i * 7.0f, center.y + (i == 0 ? -3.0f : 0.0f));
                draw->AddRectFilled(ImVec2(card.x - 7, card.y - 10), ImVec2(card.x + 7, card.y + 10), Rgb(i == 0 ? SidebarInk : SidebarInkDim, i == 0 ? 255 : 170), 3);
                draw->AddRect(ImVec2(card.x - 7, card.y - 10), ImVec2(card.x + 7, card.y + 10), Rgb(Sidebar), 3, 0, 1.5f);
            }
            ImGui::PushFont(m_titleFont, 19);
            ImVec2 label = ImGui::CalcTextSize("COLLECTION");
            draw->AddText(ImVec2(at.x + 62, center.y - label.y / 2), Rgb(SidebarInk), "COLLECTION");
            ImGui::PopFont();
        }
        bool open = m_page == Page::Settings;
        if (row("##settings", GameRowH, open, true, at))
            GoTo(open ? m_backPage : Page::Settings);
        if (open)
            TakeFocus(FocusTo::Sidebar);
        ImGui::PopStyleVar();
        ImVec2 center(at.x + 34, at.y + GameRowH / 2);
        draw->AddCircleFilled(center, GearButton / 2, Rgb(SidebarInk), 32);
        DrawGear(draw, center, GearButton * 0.32f, Rgb(Sidebar), Rgb(SidebarInk));
        ImGui::PushFont(m_titleFont, 19);
        ImVec2 label = ImGui::CalcTextSize("SETTINGS");
        draw->AddText(ImVec2(at.x + 62, center.y - label.y / 2), Rgb(SidebarInk), "SETTINGS");
        ImGui::PopFont();

        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    // The launcher's menu, opened by the SKYLAUNCHER title: empty for now, with the project's links bottom right.
    void App::DrawHome()
    {
        constexpr const char* Project = "https://github.com/mflr0/Skylauncher";
        constexpr float Icon = 40, ReportW = 190;
        ImVec2 size = ImGui::GetIO().DisplaySize;
        ImVec2 iconAt(size.x - Pad - Icon, size.y - Pad - Icon);

        ImGui::SetCursorScreenPos(ImVec2(iconAt.x - 14 - ReportW, iconAt.y));
        if (PillButton("Report a bug", false, ReportW, true, BottomBarRaised, BottomBarRaisedHover, BottomBarRaisedActive))
            SDL_OpenURL((std::string(Project) + "/issues/new").c_str());
        TakeFocus(FocusTo::Page);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Opens a new issue on GitHub");

        ImGui::SetCursorScreenPos(iconAt);
        if (ImGui::InvisibleButton("##github", ImVec2(Icon, Icon), ImGuiButtonFlags_EnableNav))
            SDL_OpenURL(Project);
        FocusRing(Icon / 2);
        bool hovered = ImGui::IsItemHovered();
        if (hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Skylauncher on GitHub");
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

        // Hero: the game's logo from the player's disc (drawn below) over the drawn sky; the disc's background art isn't used.
        draw->AddRectFilledMultiColor(ImVec2(position.x, position.y + heroH * 0.55f), end, IM_COL32(11, 15, 46, 0), IM_COL32(11, 15, 46, 0),
            IM_COL32(11, 15, 46, 230), IM_COL32(11, 15, 46, 230));
        if (m_dragOver)
            draw->AddRect(ImVec2(position.x + 6, position.y + 6), ImVec2(end.x - 6, end.y - 6), Rgb(AccentDark), RadiusLarge, 4.0f);

        // The logo (else the game's name), as large as fits and centred between the language menu and the disc status.
        float areaTop = position.y + Pad * 3, areaBottom = end.y - 120, areaH = areaBottom - areaTop;
        if (m_installView)
        {
            float cardW = std::min(available.x - 80, 680.0f);
            DrawInstall(ImVec2(position.x + (available.x - cardW) / 2, areaTop), ImVec2(cardW, end.y - Pad - areaTop));
        }
        else if (m_logo != nullptr)
        {
            float w, h;
            SDL_GetTextureSize(m_logo, &w, &h);
            float scale = std::min(available.x * 0.85f / w, areaH / h);
            ImVec2 logoSize(w * scale, h * scale);
            ImVec2 logoAt(position.x + (available.x - logoSize.x) / 2, areaTop + (areaH - logoSize.y) / 2);
            draw->AddImage(Ref(m_logo), logoAt, ImVec2(logoAt.x + logoSize.x, logoAt.y + logoSize.y));
        }
        else
        {
            ImGui::PushFont(m_titleFont, HeroSize);
            float wrap = available.x - 96;
            ImVec2 text = ImGui::CalcTextSize(m_game.name, nullptr, false, wrap);
            ImVec2 at(position.x + (available.x - text.x) / 2, areaTop + (areaH - text.y) / 2);
            ImGui::SetCursorScreenPos(at);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Text));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap); // window coordinates, not the screen's
            ImGui::TextUnformatted(m_game.name);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        // The disc, bottom left of the hero: the game's icon (rounded), the image's file name, its status under it.
        ImGui::SetCursorScreenPos(ImVec2(position.x + 40, end.y - 120));
        ImGui::BeginGroup();
        if (!m_installView) // the progress card takes the whole hero
        {
            if (m_icon != nullptr)
            {
                ImVec2 at = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(64, 64));
                draw->AddImageRounded(Ref(m_icon), at, ImVec2(at.x + 64, at.y + 64), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, Radius);
                ImGui::SameLine(0, 16);
            }
            ImGui::BeginGroup();
            // The file name on top, then "DISC IMAGE:" and the status on the line below.
            std::string file = m_discPath.empty() ? "No disc image yet" : LauncherMiddleEllipsis(std::filesystem::path(m_discPath).filename().string(), 40);
            ImGui::PushFont(nullptr, TitleSize);
            BoldText(file.c_str(), Text);
            ImGui::PopFont();
            if (!m_discPath.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", m_discPath.c_str());
            BoldText("DISC IMAGE:", TextDim);
            ImGui::SameLine(0, 6);
            switch (m_disc)
            {
            case DiscState::None:
                ImGui::TextColored(ImColor(Rgb(TextSecondary)), "Drop your disc image (.iso) here, or choose it below.");
                break;
            case DiscState::Verifying:
                ImGui::TextColored(ImColor(Rgb(Play)), "%s...", m_verify.Stage().c_str());
                ImGui::ProgressBar(m_verify.progress, ImVec2(360, 8), "");
                break;
            case DiscState::Ready:
                ImGui::TextColored(ImColor(Rgb(Ready)), "Disc verified: %s", LauncherFormatSize(m_discSize).c_str());
                if (NeedsInstall())
                {
                    ImGui::PushTextWrapPos(available.x - 40); // window coordinates
                    ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
                    ImGui::TextWrapped("Install builds the game on your PC from your disc: about %d minutes and %d GB, internet needed "
                        "once for the compiler.", InstallMinutes, InstallGigabytes);
                    ImGui::PopStyleColor();
                    ImGui::PopTextWrapPos();
                }
                else if (UpdateAvailable())
                {
                    ImGui::TextColored(ImColor(Rgb(TextSecondary)), "A new version of the game is ready to build.");
                    ImGui::SameLine(0, 10);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 2));
                    ImGui::PushStyleColor(ImGuiCol_Button, Rgb(Accent));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb(AccentHover));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Rgb(AccentDark));
                    ImGui::PushStyleColor(ImGuiCol_Text, Rgb(AccentInk));
                    if (ImGui::Button("Update the game"))
                        StartInstall();
                    ImGui::PopStyleColor(4);
                    ImGui::PopStyleVar();
                }
                break;
            case DiscState::Failed:
                ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Danger));
                ImGui::PushTextWrapPos(available.x - 40);
                ImGui::TextWrapped("%s", m_check.message.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                break;
        }
        ImGui::EndGroup();
        }
        ImGui::EndGroup();

        // Bottom band: the controller and the main button in the middle, "Change disc..." on the right.
        ImVec2 bar(position.x, end.y), barEnd(end.x, position.y + available.y);
        draw->AddRectFilled(bar, barEnd, Rgb(BottomBar));
        float middle = bar.y + BottomBarH / 2;

        bool ready = m_disc == DiscState::Ready, verifying = m_disc == DiscState::Verifying, installing = Installing();
        bool install = ready && NeedsInstall();
        const char* label = installing ? "INSTALLING..." : install ? "INSTALL" : ready ? "PLAY" : verifying ? "CHECKING..." : strings::ChooseDisc;
        // Centred, unless the controller picker on its left would reach the sidebar (a narrow window).
        constexpr float MinGap = 16;
        float mainX = std::max(bar.x + (available.x - PlayButtonW) / 2, bar.x + Pad + ControllerW + MinGap), buttonY = middle - PlayButtonH / 2 + 3;
        ImGui::SetCursorScreenPos(ImVec2(mainX, buttonY));
        if (BigButton(label, ImVec2(PlayButtonW, PlayButtonH), Play, PlayDark, PlayInk, !verifying && !installing))
        {
            if (!ready)
                Browse();
            else if (install)
                StartInstall(); // which checks first that the image is still there
            else if (DiscStillThere())
            {
                m_chosen = m_discPath;
                m_chosenRuntime = m_installed.runtime;
                Set(m_game.id, "iso", m_chosen);
                m_running = false;
            }
        }
        TakeFocus(FocusTo::Page);

        // Controller picker, left of the main button: what the game listens to, a menu of the choices above it.
        float controllerGap = std::clamp(mainX - (bar.x + Pad) - ControllerW, MinGap, ControllerGap);
        ImVec2 controllerAt(mainX - controllerGap - ControllerW, buttonY), controllerSize(ControllerW, PlayButtonH - 6);
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
        std::string deviceName = device == "auto" ? "Automatic" : device == "keyboard" ? "Keyboard" : LauncherMiddleEllipsis(DeviceLabel(), 13);
        ImGui::PushFont(nullptr, SmallSize);
        AddBoldText(draw, ImVec2(controllerAt.x + 64, controllerMid - 20), Rgb(BottomBarInk), "CONTROLLER");
        ImGui::PopFont();
        AddBoldText(draw, ImVec2(controllerAt.x + 64, controllerMid - 1), Rgb(BottomBarInk), deviceName.c_str(), BolderPasses);
        ImVec2 chevron(controllerEnd.x - 20, controllerMid);
        draw->AddTriangleFilled(ImVec2(chevron.x - 6, chevron.y + 3), ImVec2(chevron.x + 6, chevron.y + 3), ImVec2(chevron.x, chevron.y - 4), Rgb(SidebarInk));
        if (pickerHovered && !pickerOpen)
            ImGui::SetTooltip("Controller: %s", DeviceLabel().c_str());

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
            float labelsW = ImGui::CalcTextSize(strings::PlayWith).x;
            for (const auto& [value, name] : options)
                labelsW = std::max(labelsW, ImGui::CalcTextSize(name.c_str()).x);
            float rowW = std::max(ControllerW - 12, LabelX + labelsW + 16);
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + LabelX);
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", strings::PlayWith);
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 2));
            ImDrawList* popup = ImGui::GetWindowDrawList();
            for (const auto& [value, name] : options)
            {
                bool current = value == device;
                ImVec2 at = ImGui::GetCursorScreenPos();
                ImGui::PushID(value.c_str());
                if (ImGui::Selectable("##device", current, 0, ImVec2(rowW, RowH)))
                    SetDevice(value);
                ImGui::PopID();
                float mid = at.y + RowH / 2;
                popup->AddCircle(ImVec2(at.x + DotX, mid), Dot, Rgb(current ? Accent : TextDim), 16, 2.0f);
                if (current)
                    popup->AddCircleFilled(ImVec2(at.x + DotX, mid), Dot - 3, Rgb(Accent), 12);
                popup->AddText(ImVec2(at.x + LabelX, mid - ImGui::GetTextLineHeight() / 2), Rgb(Text), name.c_str());
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

        // "Change disc...", right-aligned and centred in the band once a disc is verified; styled like the controller picker.
        if (ready && !installing)
        {
            constexpr float ChangeDiscW = 170;
            ImGui::SetCursorScreenPos(ImVec2(barEnd.x - Pad - 8 - ChangeDiscW, middle - 20));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Radius);
            if (PillButton("Change disc...", false, ChangeDiscW, true, BottomBarRaised, BottomBarRaisedHover, BottomBarRaisedActive))
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
            ImGui::SetTooltip("%s", strings::PortalTip);
    }

    void App::DrawGraphics()
    {
        if (!BeginSettingsTable("##graphics"))
            return;

        SettingRow("Fullscreen", "Play in fullscreen instead of a window.");
        bool fullscreen = ParseBool(SettingGet(m_config, "video", "fullscreen"));
        if (Toggle("##fullscreen", &fullscreen))
            Set("video", "fullscreen", fullscreen ? "1" : "0");

        SettingRow("Resolution scale", "Auto picks x1 on integrated graphics. x2 or x3 need a dedicated graphics card.");
        Combo("##scale", "video", "resolution_scale", ScaleChoices, 0);
        if (int scale = SettingInt(m_config, "video", "resolution_scale", 0); scale >= 1)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
            ImGui::Text("%d x %d for the 3D scene", 1120 * std::min(scale, 10), 704 * std::min(scale, 10));
            ImGui::PopStyleColor();
        }

        SettingRow("Frame rate limit", "The game was made for 30 FPS; 60 is experimental.");
        Combo("##fps", "video", "fps_limit", FpsChoices, 0);

        SettingRow("Skip intro movies", "Skips the start-up logos; the story movies still play.");
        bool skipIntroMovies = ParseBool(SettingGet(m_config, "video", "skip_intro_movies", "0"));
        if (Toggle("##skipintromovies", &skipIntroMovies))
            Set("video", "skip_intro_movies", skipIntroMovies ? "1" : "0");

        SettingRow("VSync", "Waits for the screen's refresh: no tearing, a little more latency.");
        bool vsync = ParseBool(SettingGet(m_config, "video", "vsync", "1"));
        if (Toggle("##vsync", &vsync))
            Set("video", "vsync", vsync ? "1" : "0");

        SettingRow("Texture filtering", "Anisotropic filtering keeps ground textures sharp at an angle.");
        Combo("##filter", "video", "anisotropy", FilterChoices, 0);

        SettingRow("Widescreen", "Auto shows more to the sides on a window wider than 16:9. 16:9 keeps the game's picture, with bars.");
        Combo("##aspect", "video", "aspect", AspectChoices, 0);

        SettingRow("HD texture packs", "Replaces the game's textures with the files of the textures folder.");
        bool texturePacks = ParseBool(SettingGet(m_config, "video", "texture_packs", "1"));
        if (Toggle("##texturepacks", &texturePacks))
            Set("video", "texture_packs", texturePacks ? "1" : "0");
        if (PillButton("Open texture packs folder", false, 300))
        {
            if (!OpenFolder(GameInstallFolder({}) / "textures"))
                Toast("Couldn't open the folder");
        }
        EndSettingsTable();

        if (ImGui::CollapsingHeader("Advanced") && BeginSettingsTable("##graphicsadvanced"))
        {
            SettingRow("Dump textures (for pack makers)", "Writes the game's textures as DDS files into the textures folder, to make a pack from.");
            bool textureDump = ParseBool(SettingGet(m_config, "video", "texture_dump", "0"));
            if (Toggle("##texturedump", &textureDump))
                Set("video", "texture_dump", textureDump ? "1" : "0");
            EndSettingsTable();
        }
    }

    void App::DrawControls()
    {
        std::string current = CurrentDevice();
        Title("Controller", TitleSize, Text);
        ImGui::SetNextItemWidth(440);
        if (ImGui::BeginCombo("##device", DeviceLabel().c_str()))
        {
            for (const auto& [value, label] : DeviceOptions())
                if (ImGui::Selectable(label.c_str(), value == current))
                    SetDevice(value);
            ImGui::EndCombo();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        size_t gamepads = m_gamepads.size();
        std::string connected = gamepads == 0 ? strings::NoGamepad : gamepads == 1 ? strings::OneGamepad : fmt::format(fmt::runtime(strings::Gamepads), gamepads);
        ImGui::Text("%s Xbox, PlayStation, Switch and most other controllers work.", connected.c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 12));

        Title("Portal of Power", TitleSize, Text);
        Combo("##portal", "portal", "mode", PortalChoices, 0, 440);
        bool autoOpen = ParseBool(SettingGet(m_config, "portal", "toybox_auto_open", "1"));
        if (Toggle("##toyboxautoopen", &autoOpen))
            Set("portal", "toybox_auto_open", autoOpen ? "1" : "0");
        ImGui::SameLine(0, 14);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Open the Toy Box when the game asks for a figure");
        DrawPortalTest();
        ImGui::Dummy(ImVec2(0, 12));

        Title("Button glyphs", TitleSize, Text);
        Combo("##glyphs", "input", "glyphs", GlyphChoices, 0, 440);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::TextUnformatted("The button pictures the game shows in its menus and prompts.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 12));

        // Keyboard bindings, two columns of actions.
        Title("Keyboard", TitleSize, Text);
        ImGui::SameLine(0, 24);
        if (HoldButton(strings::ResetKeys, 200, strings::ResetKeysHint))
        {
            for (const KeyAction& action : KeyActions)
                m_config["input"].erase(std::string("key_") + action.id);
            Set("input", "device", SettingGet(m_config, "input", "device", "auto"));
            Toast("Keyboard bindings reset");
        }
        if (m_capturing != nullptr)
            ImGui::TextColored(ImColor(Rgb(Accent)), "Press a key for \"%s\" (Escape cancels)", m_capturing->label);
        else
            ImGui::TextColored(ImColor(Rgb(TextDim)), "Click a key to change it.");
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6, 4));
        if (ImGui::BeginTable("##keys", 4, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("action1", ImGuiTableColumnFlags_WidthFixed, 190);
            ImGui::TableSetupColumn("key1", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableSetupColumn("action2", ImGuiTableColumnFlags_WidthFixed, 190);
            ImGui::TableSetupColumn("key2", ImGuiTableColumnFlags_WidthFixed, 150);
            for (size_t i = 0; i < std::size(KeyActions); i++)
            {
                const KeyAction& action = KeyActions[i];
                if (i % 2 == 0)
                    ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(action.label);
                ImGui::TableNextColumn();
                std::string key = SettingGet(m_config, "input", std::string("key_") + action.id, SDL_GetKeyName(action.fallback));
                bool duplicate = false;
                for (const KeyAction& other : KeyActions)
                    if (&other != &action && SettingGet(m_config, "input", std::string("key_") + other.id, SDL_GetKeyName(other.fallback)) == key)
                        duplicate = true;
                ImGui::PushID(action.id);
                ImGui::PushStyleColor(ImGuiCol_Button, Rgb(m_capturing == &action ? Accent : Field));
                ImGui::PushStyleColor(ImGuiCol_Text, Rgb(m_capturing == &action ? AccentInk : duplicate ? Danger : Text));
                if (ImGui::Button(m_capturing == &action ? "..." : key.c_str(), ImVec2(140, 32)))
                    m_capturing = &action;
                ImGui::PopStyleColor(2);
                if (duplicate && ImGui::IsItemHovered())
                    ImGui::SetTooltip("This key is used by another action");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::TextUnformatted("F6/Tab opens the Toy Box, F7 places the next figure, F8 removes them all.");
        ImGui::PopStyleColor();
    }

    void App::DrawLanguage()
    {
        if (!BeginSettingsTable("##language"))
            return;
        SettingRow(strings::GameText, strings::GameTextHint);
        Combo("##text", m_game.id, "language", LanguageChoices, 0);
        SettingRow(strings::GameVoices, strings::GameVoicesHint);
        Combo("##voice", m_game.id, "voice_language", LanguageChoices, 0);
        EndSettingsTable();
    }

    void App::DrawGameData()
    {
        Title("Disc image", TitleSize, Text);
        ImGui::TextWrapped("%s", m_discPath.empty() ? "None chosen yet." : m_discPath.c_str());
        if (m_disc == DiscState::Ready)
            ImGui::TextColored(ImColor(Rgb(Ok)), "Verified, %s", LauncherFormatSize(m_discSize).c_str());
        if (PillButton("Change disc...", true, 200))
            Browse();
        ImGui::Dummy(ImVec2(0, 16));
        Title("Folders", TitleSize, Text);
        if (PillButton("Open data folder", false, 260))
        {
            std::error_code error;
            std::filesystem::create_directories(LauncherDataFolder(), error);
            OpenFolder(LauncherDataFolder()); // the "Skylanders" folder: settings, saves, figures, the game, logs
        }
        ImGui::SameLine();
        if (PillButton(strings::OpenFigures, false, 260))
        {
            SDL_CreateDirectory(m_collection->Folder().c_str());
            { std::string folder = m_collection->Folder(); OpenFolder(std::filesystem::path(std::u8string(folder.begin(), folder.end()))); }
        }
        ImGui::Dummy(ImVec2(0, 16));
        DrawSaveBackups();
        ImGui::Dummy(ImVec2(0, 16));
        Title("Support", TitleSize, Text);
        DrawLogs();
        ImGui::Dummy(ImVec2(0, 16));
        Title("Game runtime", TitleSize, Text);
        if (!m_installed.runtime.empty())
        {
            ImGui::TextWrapped("Built on this computer for this disc: %s", m_installed.runtime.c_str());
            if (HoldButton(strings::Reinstall, 260, strings::ReinstallHint) && !Installing())
                StartInstall();
            ImGui::SameLine();
            if (PillButton("Open the game folder", false, 260))
                OpenFolder(GameInstallFolder(m_check.xexSha256));
            ImGui::Dummy(ImVec2(0, 8));
        }
        ImGui::TextWrapped("Skylauncher starts the game it built for the disc, else the game runtime (%s) next to it, or the one set as "
            "runtime= in the [launcher] section of %s.", m_game.runtime, m_configPath.c_str());
    }

    // The game's save backups (save_backup.h): one row per backup (date, time, size) and the button that puts it back.
    // The game keeps them itself before it overwrites a save; restoring one first keeps the saves the player has now.
    void App::RefreshSaveBackups()
    {
        m_saveBackups = savebackup::List(savebackup::Root(m_game.id));
        m_saveBackupsLoaded = true;
    }

    void App::RestoreSaveBackup(const std::string& stamp)
    {
        const savebackup::Restore restore = savebackup::RestoreBackup(savebackup::Root(m_game.id), stamp);
        m_saveBackupsLoaded = false; // the restore made a backup of its own
        if (restore.ok)
            Toast("Saves restored from " + stamp.substr(0, 4) + "-" + stamp.substr(4, 2) + "-" + stamp.substr(6, 2) + " " +
                stamp.substr(9, 2) + ":" + stamp.substr(11, 2) + ".");
        else if (restore.error == "the game is running")
            Toast("Quit the game before restoring a backup.");
        else
            Toast("The backup could not be restored: " + restore.error + ".");
    }

    void App::DrawSaveBackups()
    {
        Title("Save backups", TitleSize, Text);
        if (!m_saveBackupsLoaded)
            RefreshSaveBackups();
        const bool running = savebackup::GameRunning(savebackup::Root(m_game.id));
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped(running ? "The game is running: quit it to restore a backup."
            : "The game keeps the previous version of a save here before it overwrites it. Restoring one keeps the saves you have now "
              "first.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (m_saveBackups.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
            ImGui::TextUnformatted("No backups yet.");
            ImGui::PopStyleColor();
            return;
        }

        // Ten rows at a time; the newest first.
        ImGui::BeginChild("##savebackuplist", ImVec2(0, std::min(300.0f, 44.0f * float(m_saveBackups.size()))), ImGuiChildFlags_NavFlattened);
        if (ImGui::BeginTable("##savebackups", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableSetupColumn("when");
            ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed, 128);
            for (const savebackup::Entry& backup : m_saveBackups)
            {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 40);
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(backup.date.c_str());
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
                ImGui::TextUnformatted(backup.time.c_str());
                ImGui::PopStyleColor();
                if (backup.files == 1 && ImGui::IsItemHovered())
                    ImGui::SetTooltip("One file");
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(LauncherFormatSize(backup.bytes).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::BeginDisabled(running);
                ImGui::PushID(backup.stamp.c_str());
                if (HoldButton(strings::RestoreSaves, 112, strings::RestoreSavesHint))
                    RestoreSaveBackup(backup.stamp);
                ImGui::PopID();
                ImGui::EndDisabled();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

    void App::DrawSettings()
    {
        // A centred column: settings read badly when they stretch across a wide window.
        float available = ImGui::GetContentRegionAvail().x;
        float width = std::min(available, 940.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) / 2);
        ImGui::BeginChild("##settingscolumn", ImVec2(width, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);

        constexpr std::pair<SettingsTab, const char*> Tabs[] = { { SettingsTab::Graphics, "Graphics" }, { SettingsTab::Controls, "Controls" },
            { SettingsTab::Language, "Language" }, { SettingsTab::GameData, "Game data" },
            { SettingsTab::About, "About" } };
        Title("SETTINGS", HeadlineSize, Text);
        ImGui::Dummy(ImVec2(0, 4));
        // The tabs: words on one line, an accent line under the selected one (like the sidebar's bar).
        // LB / RB change tab, as in the console launchers.
        if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) && !ImGui::IsAnyItemActive())
        {
            int index = int(m_tab), count = int(std::size(Tabs));
            int step = ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) ? -1 : ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) ? 1 : 0;
            if (step != 0)
            {
                m_tab = Tabs[std::clamp(index + step, 0, count - 1)].first;
                m_capturing = nullptr;
                m_saveBackupsLoaded = false;
                RequestFocus(FocusTo::Page); // the ring follows to the new tab
            }
        }
        ImGui::PushFont(nullptr, SidebarTextSize);
        for (const auto& [tab, label] : Tabs)
        {
            bool active = m_tab == tab;
            ImVec2 at = ImGui::GetCursorScreenPos(), text = ImGui::CalcTextSize(label);
            ImVec2 size(text.x + BoldOffset, text.y + 10 + TabUnderlineH);
            if (ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav))
            {
                m_tab = tab;
                m_capturing = nullptr;
                m_saveBackupsLoaded = false; // the saves may have changed since the tab was last open
            }
            bool hovered = ImGui::IsItemHovered();
            FocusRing(RadiusSmall);
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
        BeginCard("##settings", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        switch (m_tab)
        {
        case SettingsTab::Graphics: DrawGraphics(); break;
        case SettingsTab::Controls: DrawControls(); break;
        case SettingsTab::Language: DrawLanguage(); break;
        case SettingsTab::GameData: DrawGameData(); break;
        case SettingsTab::About: DrawAbout(); break;
        }
        EndCard();
        ImGui::EndChild();
    }

    void App::GoTo(Page page)
    {
        if ((page == Page::Settings || page == Page::Collection) && (m_page == Page::Home || m_page == Page::Play))
            m_backPage = m_page;
        m_page = page;
        m_capturing = nullptr;
        RequestFocus(FocusTo::Page);
    }

    // B / Escape when nothing else took it: out of Settings and Collection, else to the sidebar.
    void App::Back()
    {
        if (m_page == Page::Settings || m_page == Page::Collection)
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

    void App::Toast(std::string text)
    {
        // Long enough to read: 2.5 s, and more for a long message (an error and its reason), up to 6 s.
        uint64_t readMs = std::clamp<uint64_t>(2500 + 45 * (text.size() > 40 ? text.size() - 40 : 0), 2500, 6000);
        m_toast = std::move(text);
        m_toastUntil = SDL_GetTicks() + readMs;
    }

    void App::DrawToast()
    {
        if (m_toast.empty() || SDL_GetTicks() > m_toastUntil)
            return;
        // Bottom right, at most 560 points wide (a long one wraps, never over the sidebar).
        ImVec2 size = ImGui::GetIO().DisplaySize;
        float wrap = std::min(560.0f, size.x - SidebarW - 2 * Pad - 40);
        ImVec2 text = ImGui::CalcTextSize(m_toast.c_str(), nullptr, false, wrap);
        ImVec2 end(size.x - 24, size.y - 28), position(end.x - text.x - 40, end.y - text.y - 24);
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        draw->AddRectFilled(position, end, Rgb(PanelRaised, 245), Radius);
        draw->AddRect(position, end, Rgb(Accent, 200), Radius, 2.0f);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(position.x + 20, position.y + 12), Rgb(Text), m_toast.c_str(), nullptr, wrap);
    }

    // Quitting during an install: keep installing (the default, B / Escape) or quit.
    void App::DrawQuitPrompt()
    {
        if (!m_quitPrompt)
            return;
        if (!Installing()) // it ended meanwhile: nothing to lose any more
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
            Title(strings::QuitTitle, TitleSize, Text);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
            ImGui::TextWrapped("%s", strings::QuitText);
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 8));
            bool keep = PillButton(strings::KeepInstalling, true, 200, true);
            if (ImGui::IsWindowAppearing())
                ImGui::SetItemDefaultFocus();
            ImGui::SameLine(ImGui::GetWindowWidth() - Pad - 120);
            bool quit = PillButton(strings::Quit, false, 120, true);
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
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        // The launcher's points are m_uiScale window units: a smaller page, drawn (and its fonts rasterized) denser.
        ImGuiIO& frameIo = ImGui::GetIO();
        frameIo.DisplaySize = ImVec2(frameIo.DisplaySize.x / m_uiScale, frameIo.DisplaySize.y / m_uiScale);
        frameIo.DisplayFramebufferScale = ImVec2(frameIo.DisplayFramebufferScale.x * m_uiScale, frameIo.DisplayFramebufferScale.y * m_uiScale);
        if (SDL_GetMouseFocus() == nullptr) // the backend's outside-the-window position is in unscaled window units
            frameIo.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        if (m_testNav)
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
        case Page::Collection: m_collection->Draw(); break;
        case Page::Settings: DrawSettings(); break;
        }
        ImGui::End();
        ImGui::PopStyleVar();
        DrawWhatsNew();
        DrawProblemReport();
        DrawQuitPrompt();
        DrawToast();
        // Who answers B / Escape next frame: an open menu or dialog, a field being edited, the collection's search or View.
        m_backTaken = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || ImGui::IsAnyItemActive() ||
            m_reportOpen || m_whatsNewOpen || m_quitPrompt || (m_page == Page::Collection && m_collection->TakesBack());
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
            static const char* const Names[] = { "01-no-iso", "02-verified", "03-graphics", "04-controls", "05-collection", "06-home",
                "07-figure-sheet", "08-new-figure", "09-play-with", "10-language", "11-disc-gone" };
            static const char* const InstallNames[] = { "10-install", "11-installing", "12-quit-while-installing", "13-install-details",
                "14-install-failed", "15-installed", "16-game-data", "17-install-interrupted" };
            static const char* const UpdateNames[] = { "20-update-banner", "21-whats-new", "22-about", "23-about-checked", "24-update-failed" };
            static const char* const NavNames[] = { "40-nav-start", "41-nav-sidebar", "42-nav-down", "43-nav-collection", "44-nav-view-menu",
                "45-nav-view-closed", "46-nav-card", "47-nav-figure-page", "48-nav-delete-hint", "49-nav-delete-held",
                "50-nav-back-to-play", "51-nav-settings-controls", "52-nav-settings-back" };
            if (m_testNav)
                Screenshot(NavNames[std::min<int>(m_testStep, int(std::size(NavNames)) - 1)]);
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

    // Keyboard and pad navigation (SSA_LAUNCHER_TEST_NAV): keys go to ImGui as a player's would, a shot after each group.
    void App::TestNavScript(uint64_t now)
    {
        // A key is pressed and released, unless Hold (down only) or Release (up only) wraps it.
        constexpr int Hold = 0x10000, Release = 0x20000;
        static const std::vector<int> Steps[] = {
            {},                                                        // 40: the launcher opens, the focus in the sidebar
            { ImGuiKey_Escape },                                       // 41: B: the ring on the game's row
            { ImGuiKey_DownArrow },                                    // 42: down: past the games coming later
            { ImGuiKey_Space },                                        // 43: A: Collection, the ring on Add a figure
            { ImGuiKey_GamepadFaceUp, ImGuiKey_DownArrow },            // 44: Y: the View menu, focused
            { ImGuiKey_GamepadFaceRight },                             // 45: B: closed, the ring back on View
            { ImGuiKey_LeftArrow, ImGuiKey_LeftArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow,
              ImGuiKey_RightArrow },                                   // 46: a seat, then right: the list's first card
            { ImGuiKey_GamepadFaceLeft },                              // 47: X: its Figure page
            { ImGuiKey_DownArrow, ImGuiKey_RightArrow, ImGuiKey_RightArrow, ImGuiKey_RightArrow }, // 48: on Delete, its hint
            { Hold | ImGuiKey_Space },                                 // 49: held for 1.5 s: in the trash, back to the list
            { Release | ImGuiKey_Space, ImGuiKey_GamepadFaceRight },   // 50: B: back to Play, on the main button
            { ImGuiKey_Escape, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_Space, ImGuiKey_GamepadR1 }, // 51: Settings > Controls
            { ImGuiKey_Escape },                                       // 52: Escape: back to Play
        };
        if (m_testStep >= int(std::size(Steps)))
        {
            m_running = false;
            return;
        }
        if (m_testStep == 0)
        {
            m_page = Page::Play;
            m_collection->Seed();
            if (m_disc == DiscState::None && !m_testDrop.empty())
                StartVerify(m_testDrop);
            if (m_disc == DiscState::Ready || m_disc == DiscState::Failed || m_testDrop.empty())
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
        if (m_testNav)
        {
            TestNavScript(now);
            return;
        }
        if (m_testCollection)
        {
            // The collection's screens, in the scratch collection SSA_COLLECTION names.
            if (m_testStep >= CollectionPage::TestShotCount())
                m_running = false;
            else
            {
                if (m_testStep == 0)
                    m_collection->Seed();
                m_page = Page::Collection;
                m_collection->TestScreen(m_testStep);
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
            if (m_disc == DiscState::None && !m_testDrop.empty())
                StartVerify(m_testDrop);
            if (m_disc == DiscState::Ready || m_disc == DiscState::Failed || m_testDrop.empty())
                m_testAt = now + 400;
            break;
        case 2:
            m_page = Page::Settings;
            m_tab = SettingsTab::Graphics;
            m_testAt = now + 400;
            break;
        case 3:
            m_tab = SettingsTab::Controls;
            m_testAt = now + 400;
            break;
        case 4:
            // A few figures to show, in the test's own collection: characters, a legendary, a sidekick, an item and an
            // adventure pack; some sit on the next session's seats.
            m_collection->Seed();
            m_page = Page::Collection;
            m_collection->TestScreen(0);
            m_testAt = now + 600;
            break;
        case 5:
            m_page = Page::Home;
            m_testAt = now + 400;
            break;
        case 6:
            m_page = Page::Collection;
            m_collection->TestScreen(3);
            m_testAt = now + 600;
            break;
        case 7:
            m_collection->TestScreen(6);
            m_testAt = now + 600;
            break;
        case 8:
            m_page = Page::Play;
            m_testControllers = true;
            m_testAt = now + 500;
            break;
        case 9:
            m_page = Page::Settings;
            m_tab = SettingsTab::Language;
            m_testAt = now + 400;
            break;
        case 10:
            // The verified image moved away: INSTALL says so instead of failing later.
            m_page = Page::Play;
            if (m_disc == DiscState::Ready)
            {
                m_discPath += ".moved";
                StartInstall();
            }
            m_testAt = now + 400;
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
        { "giants", "Giants", "Giants", 0xFFC65C, false },
        { "swapforce", "Swap Force", "Swap Force", 0xFF55A8, false },
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
    for (const Choice& choice : LanguageChoices)
        if (code == choice.value)
            return choice.label;
    return code;
}

std::string LauncherVoiceLabel(const LauncherConfig& config, const GameInfo& game)
{
    std::string code = SettingGet(config, game.id, "voice_language", "en");
    for (const Choice& choice : LanguageChoices)
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
        return fmt::format("{:.1f} GB", double(bytes) / GB);
    if (bytes >= uint64_t(MB))
        return fmt::format("{:.0f} MB", double(bytes) / MB);
    if (bytes >= uint64_t(KB))
        return fmt::format("{:.0f} KB", double(bytes) / KB);
    return fmt::format("{} bytes", bytes);
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
    std::string* installed)
{
    if (!previous.empty())
        config[game.id]["iso"] = previous;
    App app(game, configPath, config);
    std::string chosen = app.Run();
    if (installed != nullptr)
        *installed = app.ChosenRuntime();
    return chosen;
}
