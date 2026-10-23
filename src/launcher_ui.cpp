// The launcher window (Dear ImGui on SDL_Renderer): a sidebar of games, then Play, My Skylanders and Settings pages.
// Every choice is written to launcher.ini, which the game runtime reads (docs/settings.md lists the keys).
#include "launcher_ui.h"
#include "embedded_fonts.h"
#include "figures/figure_catalog.h"
#include "launcher_assets.h"
#include "launcher_theme.h"
#include "vfs.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <mutex>
#include <stb_image.h>
#include <stb_image_write.h>
#include <thread>
#include <vector>

using namespace LauncherTheme;

namespace
{
    // ---- Settings file ------------------------------------------------------------------------------------------

    std::string SettingGet(const LauncherConfig& config, std::string_view section, std::string_view key, std::string fallback = {})
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

    // Keyboard bindings: the runtime reads "[input] key_<action>=<SDL key name>".
    struct KeyAction
    {
        const char* id;
        const char* label;
        SDL_Keycode fallback;
    };

    constexpr KeyAction KeyActions[] = {
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

    constexpr Choice FpsChoices[] = { { "30", "30 (original)" }, { "60", "60" }, { "120", "120" }, { "144", "144" },
        { "240", "240" }, { "0", "Unlimited" } };
    constexpr Choice FilterChoices[] = { { "1", "Trilinear" }, { "2", "Anisotropic 2x" }, { "4", "Anisotropic 4x" },
        { "8", "Anisotropic 8x" }, { "16", "Anisotropic 16x" } };
    constexpr Choice PortalChoices[] = { { "auto", "Real portal if plugged in, else emulated" }, { "emulated", "Emulated portal" },
        { "hid", "Wii / Wii U / PlayStation portal (HID)" }, { "usb", "Xbox 360 / Xbox One portal (USB)" } };
    constexpr Choice LanguageChoices[] = { { "en", "English" }, { "fr", "Français" }, { "de", "Deutsch" }, { "es", "Español" },
        { "it", "Italiano" }, { "nl", "Nederlands" }, { "sv", "Svenska" }, { "no", "Norsk" }, { "da", "Dansk" }, { "fi", "Suomi" } };

    template<size_t N>
    int ChoiceIndex(const Choice (&choices)[N], const std::string& value, int fallback)
    {
        for (size_t i = 0; i < N; i++)
            if (value == choices[i].value)
                return int(i);
        return fallback;
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

    std::string PreferencePath(const char* sub)
    {
        std::string path;
        if (char* base = SDL_GetPrefPath("", g_launcherName))
        {
            path = std::string(base) + sub;
            SDL_free(base);
        }
        return path;
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

    // ---- Disc verification on a worker thread --------------------------------------------------------------------

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

        void Start(const std::string& image, const GameInfo& game)
        {
            Join();
            done = false;
            progress = 0;
            { std::lock_guard lock(mutex); stage = "Opening the disc image"; check = {}; meta = {}; }
            thread = std::thread([this, image, &game] {
                auto report = [this](float fraction, std::string_view text) {
                    progress = fraction;
                    std::lock_guard lock(mutex);
                    stage.assign(text);
                };
                GameSourceCheck result = CheckGameSource(image, game, report);
                TitleMetadata metadata;
                if (result.status == GameSourceStatus::Ok)
                {
                    report(0.98f, "Reading the title and icon");
                    DiscFileSystem disc;
                    if (disc.Open(image.c_str()))
                        if (std::vector<uint8_t> xex = disc.ReadAll("default.xex"); !xex.empty())
                            metadata = ReadTitleMetadata(xex);
                }
                { std::lock_guard lock(mutex); check = std::move(result); meta = std::move(metadata); }
                progress = 1;
                done = true;
            });
        }
    };

    // ---- Figures (My Skylanders) ---------------------------------------------------------------------------------

    struct FigureFile
    {
        std::string file;   // name in the figures folder
        const figures::ToyInfo* toy = nullptr;
        SDL_Texture* portrait = nullptr;
    };

    enum class Page { Play, Skylanders, Settings };
    enum class SettingsTab { Graphics, Controls, Language, GameData, About };
    enum class DiscState { None, Verifying, Ready, Failed };

    class App
    {
    public:
        App(const GameInfo& game, const std::string& configPath, LauncherConfig& config) : m_game(game), m_configPath(configPath), m_config(config) {}
        ~App() { Shutdown(); }
        std::string Run();

    private:
        bool Init();
        void Shutdown();
        bool LoadFonts();
        void HandleEvent(const SDL_Event& event);
        void Frame();
        void DrawBackdrop();
        void DrawSidebar();
        void DrawPlay();
        void DrawSkylanders();
        void DrawSettings();
        void DrawGraphics();
        void DrawControls();
        void DrawLanguage();
        void DrawGameData();
        void DrawAbout();
        void DrawToast();

        // Widgets in the launcher's style.
        bool BigButton(const char* label, ImVec2 size, unsigned color, unsigned dark, unsigned ink, bool enabled = true);
        bool PillButton(const char* label, bool primary = false, float width = 0);
        bool Toggle(const char* id, bool* value);
        template<size_t N> bool Combo(const char* label, const char* section, const char* key, const Choice (&choices)[N], int fallback);
        void SettingRow(const char* label, const char* hint);
        void BeginCard(const char* id, ImVec2 size);
        void EndCard();
        void Title(const char* text, float size = TitleSize, unsigned color = Gold);

        void StartVerify(const std::string& path);
        void FinishVerify();
        void Browse();
        void Set(const char* section, const char* key, const std::string& value);
        void Toast(std::string text) { m_toast = std::move(text); m_toastUntil = SDL_GetTicks() + 2500; }
        void RefreshGamepads();
        void LoadFigures();
        std::vector<std::string> StartupFigures() const;
        void SetStartupFigure(const std::string& file, bool on);
        void Screenshot(const char* name);
        void TestScript();

        const GameInfo& m_game;
        std::string m_configPath;
        LauncherConfig& m_config;
        std::string m_chosen;
        bool m_running = true;

        SDL_Window* m_window = nullptr;
        SDL_Renderer* m_renderer = nullptr;
        std::vector<uint8_t> m_fontData[2];
        ImFont* m_textFont = nullptr;
        ImFont* m_titleFont = nullptr;

        Page m_page = Page::Play;
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
        SDL_Texture* m_hero = nullptr;
        bool m_dragOver = false;

        std::vector<SDL_JoystickID> m_gamepads;
        const KeyAction* m_capturing = nullptr;
        std::vector<FigureFile> m_figures;
        bool m_figuresLoaded = false;

        std::string m_toast;
        uint64_t m_toastUntil = 0;
        std::vector<ImVec4> m_stars; // x, y (0..1), radius, phase

        // Screenshot test (SSA_LAUNCHER_SHOTS, SSA_LAUNCHER_TEST_DROP): drives the screens without a player.
        std::string m_shots, m_testDrop;
        int m_testStep = 0;
        uint64_t m_testAt = 0;
    };

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
            if (ReadWholeFile(directory + "/Outfit-Variable.ttf", m_fontData[0]) && ReadWholeFile(directory + "/Bungee-Regular.ttf", m_fontData[1]))
            {
                found = true;
                break;
            }
        if (!found)
        {
            m_fontData[0].assign(g_embeddedOutfit, g_embeddedOutfit + g_embeddedOutfitSize);
            m_fontData[1].assign(g_embeddedBungee, g_embeddedBungee + g_embeddedBungeeSize);
        }

        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false; // the vectors outlive the atlas
        m_textFont = io.Fonts->AddFontFromMemoryTTF(m_fontData[0].data(), int(m_fontData[0].size()), TextSize, &config);
        m_titleFont = io.Fonts->AddFontFromMemoryTTF(m_fontData[1].data(), int(m_fontData[1].size()), TitleSize, &config);
        return m_textFont != nullptr && m_titleFont != nullptr;
    }

    bool App::Init()
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
            return false;
        // The offscreen driver (screenshot test) has no GPU: no graphics context for the window, software drawing.
        const char* driver = SDL_GetCurrentVideoDriver();
        bool offscreen = driver != nullptr && (std::string_view(driver) == "offscreen" || std::string_view(driver) == "dummy");
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
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
        SDL_SetWindowMinimumSize(m_window, int(WindowMinW), int(WindowMinH));
        m_renderer = SDL_CreateRenderer(m_window, offscreen ? SDL_SOFTWARE_RENDERER : nullptr);
        if (m_renderer == nullptr)
            m_renderer = SDL_CreateRenderer(m_window, SDL_SOFTWARE_RENDERER);
        if (m_renderer == nullptr)
            return false;
        SDL_SetRenderVSync(m_renderer, 1);

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
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
        }
        RefreshGamepads();
        return true;
    }

    void App::Shutdown()
    {
        m_verify.Join();
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui_ImplSDLRenderer3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
        }
        for (FigureFile& figure : m_figures)
            if (figure.portrait != nullptr)
                SDL_DestroyTexture(figure.portrait);
        m_figures.clear();
        for (SDL_Texture* texture : { m_icon, m_logo, m_hero })
            if (texture != nullptr)
                SDL_DestroyTexture(texture);
        m_icon = m_logo = m_hero = nullptr;
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
        while (m_running)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
                HandleEvent(event);
            if (m_disc == DiscState::Verifying && m_verify.done)
                FinishVerify();
            Frame();
            TestScript();
        }
        return m_chosen;
    }

    // ---- Events and actions --------------------------------------------------------------------------------------

    void App::HandleEvent(const SDL_Event& event)
    {
        // A key binding being captured takes the next key press for itself.
        if (m_capturing != nullptr && event.type == SDL_EVENT_KEY_DOWN)
        {
            if (event.key.key != SDLK_ESCAPE)
                Set("input", (std::string("key_") + m_capturing->id).c_str(), SDL_GetKeyName(event.key.key));
            m_capturing = nullptr;
            return;
        }
        ImGui_ImplSDL3_ProcessEvent(&event);
        switch (event.type)
        {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
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
            if (event.drop.data != nullptr)
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
        for (SDL_Texture** texture : { &m_icon, &m_logo, &m_hero })
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
        // The game's own art, decoded from the player's disc into the local cache (never shipped).
        m_assets.Init(m_discPath, m_meta.titleId);
        DiscFileSystem disc;
        if (disc.Open(m_discPath.c_str()))
            m_assets.ExtractFromDisc(disc, m_meta);
        if (LauncherAssets::Asset logo = m_assets.Get(LauncherAssets::Role::Logo); logo.valid)
            m_logo = LoadPngFile(m_renderer, logo.path);
        if (LauncherAssets::Asset hero = m_assets.Get(LauncherAssets::Role::HeroBackground); hero.valid)
            m_hero = LoadPngFile(m_renderer, hero.path);
        Set(m_game.id, "iso", m_discPath); // remembered even if the player quits without playing
        m_disc = DiscState::Ready;
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

    void App::LoadFigures()
    {
        m_figuresLoaded = true;
        std::string folder = PreferencePath("figures");
        std::string portraits = PreferencePath("portraits/");
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(folder, error))
        {
            std::string extension = entry.path().extension().string();
            if (extension != ".sky" && extension != ".bin" && extension != ".dmp")
                continue;
            // The toy type sits unencrypted in block 1 (bytes 0x10-0x11, little endian).
            std::ifstream file(entry.path(), std::ios::binary);
            uint8_t header[0x12] = {};
            if (!file.read(reinterpret_cast<char*>(header), sizeof(header)))
                continue;
            FigureFile figure;
            figure.file = entry.path().filename().string();
            figure.toy = figures::FindToy(uint16_t(header[0x10] | header[0x11] << 8));
            if (figure.toy != nullptr)
                if (std::string name = figures::PortraitImageName(*figure.toy); !name.empty())
                    figure.portrait = LoadPngFile(m_renderer, portraits + name + ".png");
            m_figures.push_back(std::move(figure));
        }
        std::sort(m_figures.begin(), m_figures.end(), [](const FigureFile& a, const FigureFile& b) {
            int kindA = a.toy != nullptr ? int(a.toy->kind) : 9, kindB = b.toy != nullptr ? int(b.toy->kind) : 9;
            return kindA != kindB ? kindA < kindB : a.file < b.file;
        });
    }

    std::vector<std::string> App::StartupFigures() const
    {
        std::vector<std::string> files;
        std::string list = SettingGet(m_config, "portal", "place");
        for (size_t begin = 0; begin < list.size();)
        {
            size_t end = std::min(list.find(',', begin), list.size());
            if (end > begin)
                files.push_back(list.substr(begin, end - begin));
            begin = end + 1;
        }
        return files;
    }

    void App::SetStartupFigure(const std::string& file, bool on)
    {
        std::vector<std::string> files = StartupFigures();
        files.erase(std::remove(files.begin(), files.end(), file), files.end());
        if (on)
            files.push_back(file);
        std::string list;
        for (const std::string& name : files)
            list += (list.empty() ? "" : ",") + name;
        Set("portal", "place", list);
    }

    // ---- Widgets -------------------------------------------------------------------------------------------------

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
        bool pressed = ImGui::InvisibleButton(label, size);
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
        ImVec2 text = ImGui::CalcTextSize(label, nullptr, true);
        draw->AddText(ImVec2(position.x + (size.x - text.x) / 2, position.y + (size.y - 6 - text.y) / 2 + lift), Rgb(ink, alpha), label);
        ImGui::PopFont();
        return pressed && enabled;
    }

    bool App::PillButton(const char* label, bool primary, float width)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, Rgb(primary ? Gold : PanelRaised));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb(primary ? 0xFFD970 : 0x2F3784));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Rgb(primary ? GoldDark : 0x3A4290));
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(primary ? GoldInk : Text));
        bool pressed = ImGui::Button(label, ImVec2(width, 40));
        ImGui::PopStyleColor(4);
        return pressed;
    }

    bool App::Toggle(const char* id, bool* value)
    {
        ImVec2 position = ImGui::GetCursorScreenPos();
        ImVec2 size(54, 30);
        bool pressed = ImGui::InvisibleButton(id, size);
        if (pressed)
            *value = !*value;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), Rgb(*value ? Gold : Field), size.y / 2);
        draw->AddRect(position, ImVec2(position.x + size.x, position.y + size.y), Rgb(*value ? GoldDark : Border), size.y / 2, 2.0f);
        float knob = *value ? position.x + size.x - size.y / 2 : position.x + size.y / 2;
        draw->AddCircleFilled(ImVec2(knob, position.y + size.y / 2), size.y / 2 - 5, Rgb(*value ? GoldInk : TextSecondary));
        return pressed;
    }

    void App::SettingRow(const char* label, const char* hint)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (hint != nullptr)
        {
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
            ImGui::TextWrapped("%s", hint);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::TableSetColumnIndex(1);
    }

    template<size_t N>
    bool App::Combo(const char* label, const char* section, const char* key, const Choice (&choices)[N], int fallback)
    {
        int index = ChoiceIndex(choices, SettingGet(m_config, section, key), fallback);
        bool changed = false;
        ImGui::SetNextItemWidth(-1);
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

    void App::BeginCard(const char* id, ImVec2 size)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Rgb(Panel, 235));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, RadiusLarge);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
        ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
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
        draw->AddRectFilledMultiColor(ImVec2(0, 0), size, Rgb(SkyTop), Rgb(SkyTop), Rgb(SkyBottom), Rgb(SkyBottom));
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
        ImVec2 size = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(SidebarW, size.y));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Rgb(Sidebar, 240));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, Pad));
        ImGui::Begin("##sidebar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        Title("SKYLAUNCHER", TitleSize + 2);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::TextUnformatted("Native PC ports");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 18));
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::TextUnformatted("YOUR GAMES");
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        // The games: only those with a runtime can be selected.
        for (const LauncherGameEntry& entry : LauncherGameCatalog())
        {
            ImGui::PushID(entry.id);
            ImVec2 position = ImGui::GetCursorScreenPos();
            float width = ImGui::GetContentRegionAvail().x;
            bool selected = entry.enabled && std::string_view(entry.id) == m_game.id;
            ImGui::BeginDisabled(!entry.enabled);
            if (ImGui::InvisibleButton("game", ImVec2(width, 58)) && entry.enabled)
                m_page = Page::Play;
            bool hovered = ImGui::IsItemHovered();
            ImGui::EndDisabled();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            if (selected || hovered)
                draw->AddRectFilled(position, ImVec2(position.x + width, position.y + 58), Rgb(selected ? PanelRaised : Panel), Radius);
            if (selected)
                draw->AddRect(position, ImVec2(position.x + width, position.y + 58), Rgb(Gold, 200), Radius, 2.0f);
            unsigned accent = entry.accent;
            ImVec2 badge(position.x + 29, position.y + 29);
            draw->AddCircleFilled(badge, 19, Rgb(accent, entry.enabled ? 255 : 90));
            ImGui::PushFont(m_titleFont, 18);
            char letter[2] = { entry.name[0], 0 };
            ImVec2 letterSize = ImGui::CalcTextSize(letter);
            draw->AddText(ImVec2(badge.x - letterSize.x / 2, badge.y - letterSize.y / 2), Rgb(0x0B0F2E), letter);
            ImGui::PopFont();
            draw->AddText(ImVec2(position.x + 58, position.y + 9), Rgb(entry.enabled ? Text : TextDim), entry.name);
            ImGui::PushFont(nullptr, SmallSize);
            const char* status = !entry.enabled ? "Coming later" : m_disc == DiscState::Ready ? "Ready to play" : "Needs your disc";
            draw->AddText(ImVec2(position.x + 58, position.y + 32), Rgb(entry.enabled ? (m_disc == DiscState::Ready ? Ok : Gold) : TextDim), status);
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::PopID();
        }

        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    void App::DrawPlay()
    {
        ImVec2 available = ImGui::GetContentRegionAvail();
        float heroH = available.y - 150;

        // Hero: the game's art from the player's disc, else the drawn sky.
        ImVec2 position = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 end(position.x + available.x, position.y + heroH);
        draw->AddRectFilled(position, end, Rgb(Panel, 200), RadiusLarge);
        if (m_hero != nullptr)
            draw->AddImageRounded(Ref(m_hero), position, end, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 120), RadiusLarge);
        draw->AddRectFilledMultiColor(ImVec2(position.x, position.y + heroH * 0.55f), end, IM_COL32(11, 15, 46, 0), IM_COL32(11, 15, 46, 0),
            IM_COL32(11, 15, 46, 230), IM_COL32(11, 15, 46, 230));
        draw->AddRect(position, end, Rgb(m_dragOver ? Gold : Border, m_dragOver ? 255 : 160), RadiusLarge, m_dragOver ? 4.0f : 1.5f);

        if (m_logo != nullptr)
        {
            float w, h;
            SDL_GetTextureSize(m_logo, &w, &h);
            float scale = std::min(available.x * 0.62f / w, heroH * 0.55f / h);
            ImVec2 logoSize(w * scale, h * scale);
            ImVec2 logoAt(position.x + (available.x - logoSize.x) / 2, position.y + heroH * 0.08f);
            draw->AddImage(Ref(m_logo), logoAt, ImVec2(logoAt.x + logoSize.x, logoAt.y + logoSize.y));
        }
        else
        {
            ImGui::PushFont(m_titleFont, HeroSize);
            const char* name = m_game.name;
            ImVec2 text = ImGui::CalcTextSize(name, nullptr, false, available.x - 80);
            ImGui::SetCursorScreenPos(ImVec2(position.x + 40, position.y + heroH * 0.22f));
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Gold));
            ImGui::PushTextWrapPos(position.x + available.x - 40);
            ImGui::TextUnformatted(name);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
            (void)text;
        }

        // Disc status, bottom left of the hero.
        ImGui::SetCursorScreenPos(ImVec2(position.x + 32, end.y - 118));
        ImGui::BeginGroup();
        if (m_icon != nullptr)
        {
            ImGui::Image(Ref(m_icon), ImVec2(64, 64));
            ImGui::SameLine(0, 16);
        }
        ImGui::BeginGroup();
        Title(m_meta.name.empty() ? m_game.name : m_meta.name.c_str(), TitleSize, Text);
        switch (m_disc)
        {
        case DiscState::None:
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "Drop your disc image (.iso) here, or choose it below.");
            break;
        case DiscState::Verifying:
            ImGui::TextColored(ImColor(Rgb(Gold)), "%s...", m_verify.Stage().c_str());
            ImGui::ProgressBar(m_verify.progress, ImVec2(360, 8), "");
            break;
        case DiscState::Ready:
            ImGui::TextColored(ImColor(Rgb(Ok)), "Disc verified: %s", LauncherFormatSize(m_discSize).c_str());
            break;
        case DiscState::Failed:
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Danger));
            ImGui::PushTextWrapPos(end.x - 40);
            ImGui::TextWrapped("%s", m_check.message.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            break;
        }
        ImGui::EndGroup();
        ImGui::EndGroup();

        // Bottom bar: the disc and the PLAY button.
        ImGui::SetCursorScreenPos(ImVec2(position.x, end.y + 24));
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::TextUnformatted("DISC IMAGE");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
        ImGui::TextUnformatted(m_discPath.empty() ? "None yet" : LauncherMiddleEllipsis(m_discPath, 70).c_str());
        ImGui::PopStyleColor();
        if (PillButton(m_discPath.empty() ? "Choose disc image..." : "Change disc...", false, 220))
            Browse();
        ImGui::EndGroup();

        ImGui::SetCursorScreenPos(ImVec2(position.x + available.x - PlayButtonW, end.y + 30));
        bool ready = m_disc == DiscState::Ready;
        if (BigButton("PLAY", ImVec2(PlayButtonW, PlayButtonH), Play, PlayDark, PlayInk, ready))
        {
            m_chosen = m_discPath;
            Set(m_game.id, "iso", m_chosen);
            m_running = false;
        }
    }

    void App::DrawSkylanders()
    {
        if (!m_figuresLoaded)
            LoadFigures();
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
        ImGui::TextWrapped("Choose who starts on the portal. Levels, gold and nicknames are edited in the game's Toy Box (F6 or Tab while playing); F7 puts the next figure on the portal.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 8));
        if (PillButton("Open the figures folder", false, 260))
        {
            std::string folder = PreferencePath("figures");
            SDL_CreateDirectory(folder.c_str());
            SDL_OpenURL(("file://" + folder).c_str());
        }
        ImGui::SameLine();
        if (PillButton("Refresh", false, 120))
        {
            for (FigureFile& figure : m_figures)
                if (figure.portrait != nullptr)
                    SDL_DestroyTexture(figure.portrait);
            m_figures.clear();
            LoadFigures();
        }
        ImGui::Dummy(ImVec2(0, 10));

        if (m_figures.empty())
        {
            BeginCard("##nofigures", ImVec2(0, 150));
            Title("No figures yet", TitleSize, Text);
            ImGui::TextWrapped("Start the game once: it creates a figure for every Skylander in the figures folder. You can also copy dumps of your own figures (.sky, .bin, .dmp) there.");
            EndCard();
            return;
        }

        std::vector<std::string> startup = StartupFigures();
        ImGui::BeginChild("##figuregrid", ImVec2(0, 0), ImGuiChildFlags_None);
        float cell = 150, width = ImGui::GetContentRegionAvail().x;
        int columns = std::max(1, int((width + 12) / (cell + 12)));
        for (size_t i = 0; i < m_figures.size(); i++)
        {
            FigureFile& figure = m_figures[i];
            if (i % columns != 0)
                ImGui::SameLine(0, 12);
            ImGui::PushID(int(i));
            bool onPortal = std::find(startup.begin(), startup.end(), figure.file) != startup.end();
            ImVec2 position = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("figure", ImVec2(cell, cell + 34)))
            {
                SetStartupFigure(figure.file, !onPortal);
                Toast(onPortal ? "Taken off the portal" : "Will start on the portal");
            }
            bool hovered = ImGui::IsItemHovered();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 end(position.x + cell, position.y + cell + 34);
            draw->AddRectFilled(position, end, Rgb(onPortal ? PanelRaised : Panel, 235), Radius);
            draw->AddRect(position, end, Rgb(onPortal ? Gold : hovered ? BorderBright : Border), Radius, onPortal ? 3.0f : 1.5f);
            ImU32 color = figure.toy != nullptr ? figures::ElementColor(figure.toy->element) : Rgb(Border);
            ImVec2 center(position.x + cell / 2, position.y + cell / 2 - 4);
            draw->AddCircleFilled(center, cell * 0.36f, color, 48);
            if (figure.portrait != nullptr)
                draw->AddImageRounded(Ref(figure.portrait), ImVec2(center.x - cell * 0.34f, center.y - cell * 0.34f),
                    ImVec2(center.x + cell * 0.34f, center.y + cell * 0.34f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, cell * 0.34f);
            std::string name = figure.toy != nullptr ? figure.toy->name : figure.file;
            ImVec2 text = ImGui::CalcTextSize(name.c_str());
            draw->AddText(ImVec2(position.x + std::max(6.0f, (cell - text.x) / 2), end.y - 30), Rgb(Text), name.c_str());
            if (onPortal)
            {
                draw->AddRectFilled(ImVec2(position.x + 8, position.y + 8), ImVec2(position.x + 70, position.y + 30), Rgb(Gold), 11);
                ImGui::PushFont(nullptr, SmallSize);
                draw->AddText(ImVec2(position.x + 16, position.y + 10), Rgb(GoldInk), "PORTAL");
                ImGui::PopFont();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void App::DrawGraphics()
    {
        if (!ImGui::BeginTable("##graphics", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
            return;
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.45f);

        SettingRow("Fullscreen", "Play in fullscreen instead of a window.");
        bool fullscreen = ParseBool(SettingGet(m_config, "video", "fullscreen"));
        if (Toggle("##fullscreen", &fullscreen))
            Set("video", "fullscreen", fullscreen ? "1" : "0");

        SettingRow("Resolution scale", "Renders the 3D at a multiple of the console's resolution: sharper, heavier on the GPU.");
        int scale = std::clamp(SettingInt(m_config, "video", "resolution_scale", 1), 1, 10);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##scale", &scale, 1, 10, "x%d"))
            Set("video", "resolution_scale", std::to_string(scale));
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::Text("%d x %d for the 3D scene", 1120 * scale, 704 * scale);
        ImGui::PopStyleColor();

        SettingRow("Frame rate limit", "The game was made for 30 FPS; higher limits are experimental.");
        Combo("##fps", "video", "fps_limit", FpsChoices, 0);

        SettingRow("VSync", "Waits for the screen's refresh: no tearing, a little more latency.");
        bool vsync = ParseBool(SettingGet(m_config, "video", "vsync", "1"));
        if (Toggle("##vsync", &vsync))
            Set("video", "vsync", vsync ? "1" : "0");

        SettingRow("Texture filtering", "Anisotropic filtering keeps ground textures sharp at an angle.");
        Combo("##filter", "video", "anisotropy", FilterChoices, 0);
        ImGui::EndTable();
    }

    void App::DrawControls()
    {
        // The device the game listens to.
        std::string device = SettingGet(m_config, "input", "device", "auto");
        std::string guid = SettingGet(m_config, "input", "gamepad");
        std::vector<std::pair<std::string, std::string>> options = { { "auto", "Automatic (first gamepad, else keyboard)" }, { "keyboard", "Keyboard" } };
        for (SDL_JoystickID id : m_gamepads)
        {
            char text[64];
            SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), text, sizeof(text));
            const char* name = SDL_GetGamepadNameForID(id);
            options.emplace_back(std::string("gamepad:") + text, name != nullptr ? name : "Gamepad");
        }
        std::string current = device == "gamepad" ? "gamepad:" + guid : device;
        std::string currentLabel = "Automatic (first gamepad, else keyboard)";
        for (const auto& [value, label] : options)
            if (value == current)
                currentLabel = label;
        if (device == "gamepad" && currentLabel.starts_with("Automatic"))
            currentLabel = "Saved gamepad (not connected)";

        Title("Controller", TitleSize, Text);
        ImGui::SetNextItemWidth(520);
        if (ImGui::BeginCombo("##device", currentLabel.c_str()))
        {
            for (const auto& [value, label] : options)
                if (ImGui::Selectable(label.c_str(), value == current))
                {
                    if (value.starts_with("gamepad:"))
                    {
                        Set("input", "device", "gamepad");
                        Set("input", "gamepad", value.substr(8));
                    }
                    else
                        Set("input", "device", value);
                }
            ImGui::EndCombo();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::Text("%d gamepad(s) connected. Xbox, PlayStation, Switch and most other controllers work.", int(m_gamepads.size()));
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 12));

        Title("Portal of Power", TitleSize, Text);
        ImGui::SetNextItemWidth(520);
        Combo("##portal", "portal", "mode", PortalChoices, 0);
        ImGui::Dummy(ImVec2(0, 12));

        // Keyboard bindings, two columns of actions.
        Title("Keyboard", TitleSize, Text);
        ImGui::SameLine(0, 24);
        if (PillButton("Reset to defaults", false, 200))
        {
            for (const KeyAction& action : KeyActions)
                m_config["input"].erase(std::string("key_") + action.id);
            Set("input", "device", SettingGet(m_config, "input", "device", "auto"));
            Toast("Keyboard bindings reset");
        }
        if (m_capturing != nullptr)
            ImGui::TextColored(ImColor(Rgb(Gold)), "Press a key for \"%s\" (Escape cancels)", m_capturing->label);
        else
            ImGui::TextColored(ImColor(Rgb(TextDim)), "Click a key to change it.");
        if (ImGui::BeginTable("##keys", 4, ImGuiTableFlags_SizingStretchProp))
        {
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
                ImGui::PushStyleColor(ImGuiCol_Button, Rgb(m_capturing == &action ? Gold : Field));
                ImGui::PushStyleColor(ImGuiCol_Text, Rgb(m_capturing == &action ? GoldInk : duplicate ? Danger : Text));
                if (ImGui::Button(m_capturing == &action ? "..." : key.c_str(), ImVec2(160, 34)))
                    m_capturing = &action;
                ImGui::PopStyleColor(2);
                if (duplicate && ImGui::IsItemHovered())
                    ImGui::SetTooltip("This key is used by another action");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::TextUnformatted("F6/Tab opens the Toy Box, F7 places the next figure, F8 removes them all.");
        ImGui::PopStyleColor();
    }

    void App::DrawLanguage()
    {
        if (!ImGui::BeginTable("##language", 2, ImGuiTableFlags_SizingStretchProp))
            return;
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        SettingRow("Text", "Menus, subtitles and on-screen text.");
        Combo("##text", m_game.id, "language", LanguageChoices, 0);
        SettingRow("Voices", "Spoken lines and cutscenes. They can differ from the text.");
        Combo("##voice", m_game.id, "voice_language", LanguageChoices, 0);
        ImGui::EndTable();
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
        auto folder = [&](const char* label, const char* sub) {
            std::string path = PreferencePath(sub);
            if (PillButton(label, false, 260))
            {
                SDL_CreateDirectory(path.c_str());
                SDL_OpenURL(("file://" + path).c_str());
            }
        };
        folder("Saves and settings", "");
        ImGui::SameLine();
        folder("Figures", "figures");
        ImGui::Dummy(ImVec2(0, 16));
        Title("Game runtime", TitleSize, Text);
        ImGui::TextWrapped("Skylauncher starts the game runtime (%s) next to it, or the one set as runtime= in the [launcher] section of %s.",
            m_game.runtime, m_configPath.c_str());
    }

    void App::DrawAbout()
    {
        Title("Skylauncher", HeadlineSize);
        ImGui::TextWrapped("A launcher for the native PC ports of the Skylanders games. It contains no game code or art: everything shown from the game is read from your own disc and kept on this computer.");
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextWrapped("Made with SDL 3, Dear ImGui, stb, XenonRecomp's XEX readers, the Outfit and Bungee fonts (SIL OFL). Licences: THIRD_PARTY_NOTICES.md. Skylauncher is MIT licensed.");
        ImGui::Dummy(ImVec2(0, 8));
        if (PillButton("Project page", false, 200))
            SDL_OpenURL("https://github.com/mflr0/Skylauncher");
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::TextWrapped("Skylanders is a trademark of Activision. This project is not affiliated with or endorsed by Activision.");
        ImGui::PopStyleColor();
    }

    void App::DrawSettings()
    {
        constexpr std::pair<SettingsTab, const char*> Tabs[] = { { SettingsTab::Graphics, "Graphics" }, { SettingsTab::Controls, "Controls" },
            { SettingsTab::Language, "Language" }, { SettingsTab::GameData, "Game data" }, { SettingsTab::About, "About" } };
        for (const auto& [tab, label] : Tabs)
        {
            bool active = m_tab == tab;
            ImGui::PushStyleColor(ImGuiCol_Button, Rgb(active ? Gold : PanelRaised));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb(active ? Gold : 0x2F3784));
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(active ? GoldInk : Text));
            if (ImGui::Button(label, ImVec2(150, 40)))
            {
                m_tab = tab;
                m_capturing = nullptr;
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0, 10);
        }
        ImGui::NewLine();
        ImGui::Dummy(ImVec2(0, 8));
        BeginCard("##settings", ImVec2(0, 0));
        switch (m_tab)
        {
        case SettingsTab::Graphics: DrawGraphics(); break;
        case SettingsTab::Controls: DrawControls(); break;
        case SettingsTab::Language: DrawLanguage(); break;
        case SettingsTab::GameData: DrawGameData(); break;
        case SettingsTab::About: DrawAbout(); break;
        }
        EndCard();
    }

    void App::DrawToast()
    {
        if (m_toast.empty() || SDL_GetTicks() > m_toastUntil)
            return;
        ImVec2 size = ImGui::GetIO().DisplaySize;
        ImVec2 text = ImGui::CalcTextSize(m_toast.c_str());
        ImVec2 position(size.x - text.x - 64, size.y - 72);
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        draw->AddRectFilled(position, ImVec2(size.x - 24, size.y - 28), Rgb(PanelRaised, 245), Radius);
        draw->AddRect(position, ImVec2(size.x - 24, size.y - 28), Rgb(Gold, 200), Radius, 2.0f);
        draw->AddText(ImVec2(position.x + 20, position.y + 12), Rgb(Text), m_toast.c_str());
    }

    void App::Frame()
    {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        DrawBackdrop();
        DrawSidebar();

        ImVec2 size = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(SidebarW, 0));
        ImGui::SetNextWindowSize(ImVec2(size.x - SidebarW, size.y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad + 8, Pad + 4));
        ImGui::Begin("##page", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        // Pages as tabs across the top, like the Minecraft Launcher.
        constexpr std::pair<Page, const char*> Pages[] = { { Page::Play, "PLAY" }, { Page::Skylanders, "MY SKYLANDERS" }, { Page::Settings, "SETTINGS" } };
        for (const auto& [page, label] : Pages)
        {
            bool active = m_page == page;
            ImGui::PushFont(m_titleFont, 18);
            ImVec2 text = ImGui::CalcTextSize(label);
            ImVec2 at = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton(label, ImVec2(text.x + 8, text.y + 18)))
            {
                m_page = page;
                m_capturing = nullptr;
            }
            bool hovered = ImGui::IsItemHovered();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddText(ImVec2(at.x + 4, at.y + 4), Rgb(active ? Gold : hovered ? Text : TextSecondary), label);
            if (active)
                draw->AddRectFilled(ImVec2(at.x, at.y + text.y + 12), ImVec2(at.x + text.x + 8, at.y + text.y + 16), Rgb(Gold), 2);
            ImGui::PopFont();
            ImGui::SameLine(0, 34);
        }
        ImGui::NewLine();
        ImGui::Dummy(ImVec2(0, 6));
        switch (m_page)
        {
        case Page::Play: DrawPlay(); break;
        case Page::Skylanders: DrawSkylanders(); break;
        case Page::Settings: DrawSettings(); break;
        }
        ImGui::End();
        ImGui::PopStyleVar();
        DrawToast();

        ImGui::Render();
        SDL_SetRenderDrawColor(m_renderer, 11, 15, 46, 255);
        SDL_RenderClear(m_renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
        if (!m_shots.empty() && m_testAt != 0 && SDL_GetTicks() >= m_testAt)
        {
            static const char* const Names[] = { "01-no-iso", "02-verified", "03-graphics", "04-controls", "05-skylanders" };
            Screenshot(Names[std::min(m_testStep, 4)]);
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

    // Screens of the screenshot test, each captured once it has settled.
    void App::TestScript()
    {
        if (m_shots.empty() || m_testAt != 0)
            return;
        uint64_t now = SDL_GetTicks();
        switch (m_testStep)
        {
        case 0:
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
            m_page = Page::Skylanders;
            m_testAt = now + 600;
            break;
        default:
            m_running = false;
            break;
        }
    }
}

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
    colors[ImGuiCol_FrameBgHovered] = color(0x1A1F5C);
    colors[ImGuiCol_FrameBgActive] = color(0x232A78);
    colors[ImGuiCol_Button] = color(PanelRaised);
    colors[ImGuiCol_ButtonHovered] = color(0x2F3784);
    colors[ImGuiCol_ButtonActive] = color(0x3A4290);
    colors[ImGuiCol_Header] = color(PanelRaised);
    colors[ImGuiCol_HeaderHovered] = color(0x2F3784);
    colors[ImGuiCol_HeaderActive] = color(Gold, 0.35f);
    colors[ImGuiCol_SliderGrab] = color(Gold);
    colors[ImGuiCol_SliderGrabActive] = color(0xFFD970);
    colors[ImGuiCol_CheckMark] = color(Gold);
    colors[ImGuiCol_PlotHistogram] = color(Gold);
    colors[ImGuiCol_ScrollbarBg] = color(SkyTop, 0);
    colors[ImGuiCol_ScrollbarGrab] = color(Border);
    colors[ImGuiCol_NavCursor] = color(Gold);
    colors[ImGuiCol_TableRowBg] = color(Panel, 0);
    colors[ImGuiCol_TableBorderLight] = color(Border, 0.5f);
}

std::span<const LauncherGameEntry> LauncherGameCatalog()
{
    static const LauncherGameEntry catalog[] = {
        { "ssa", "Spyro's Adventure", "Spyro's Adventure", 0x4AE3FF, true },
        { "giants", "Giants", "Giants", 0xFFC65C, false },
        { "swapforce", "Swap Force", "Swap Force", 0xFF55A8, false },
        { "trapteam", "Trap Team", "Trap Team", 0x6DF0B4, false },
        { "superchargers", "SuperChargers", "SuperChargers", 0xFF8A5C, false },
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

std::string RunLauncherWindow(const GameInfo& game, const std::string& configPath, LauncherConfig& config, const std::string& previous)
{
    if (!previous.empty())
        config[game.id]["iso"] = previous;
    App app(game, configPath, config);
    return app.Run();
}
