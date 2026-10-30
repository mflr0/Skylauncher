// The game page in each game's own look (docs/launcher_ui.md, "Each game's look"): the disc's pictures as textures, the
// logo's arrival, and the action button (ui::MainAction) in the game's colours.
#include "launcher/ui/launcher_app.h"
#include "launcher/install_checklist.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fmt/format.h>
#include <imgui_internal.h>
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace LauncherTheme
{
    const GameStyle& StyleOf(const char* game)
    {
        static constexpr GameStyle Launcher = { AccentHover, AccentDark, AccentInk, Accent };
        if (game != nullptr && strcmp(game, "ssa") == 0)
            return SpyrosAdventureStyle;
        if (game != nullptr && strcmp(game, "giants") == 0)
            return GiantsStyle;
        if (game != nullptr && strcmp(game, "swapforce") == 0)
            return SwapForceStyle;
        if (game != nullptr && strcmp(game, "trapteam") == 0)
            return TrapTeamStyle;
        return Launcher;
    }
}

namespace launcher_ui
{
    namespace
    {
        // Straight-alpha RGBA averaged down by whole boxes, weighted by alpha (so transparent texels don't darken edges).
        std::vector<uint8_t> Shrink(const uint8_t* rgba, int pitch, int width, int height, int outWidth, int outHeight)
        {
            std::vector<uint8_t> out(size_t(outWidth) * outHeight * 4);
            for (int y = 0; y < outHeight; y++)
                for (int x = 0; x < outWidth; x++)
                {
                    int x0 = x * width / outWidth, x1 = std::max(x0 + 1, (x + 1) * width / outWidth);
                    int y0 = y * height / outHeight, y1 = std::max(y0 + 1, (y + 1) * height / outHeight);
                    double sum[4] = {};
                    for (int sy = y0; sy < y1; sy++)
                        for (int sx = x0; sx < x1; sx++)
                        {
                            const uint8_t* p = rgba + size_t(sy) * pitch + size_t(sx) * 4;
                            double a = p[3] / 255.0;
                            sum[0] += p[0] * a, sum[1] += p[1] * a, sum[2] += p[2] * a, sum[3] += a;
                        }
                    uint8_t* o = &out[(size_t(y) * outWidth + x) * 4];
                    double count = double(x1 - x0) * (y1 - y0);
                    for (int c = 0; c < 3; c++)
                        o[c] = uint8_t(sum[3] > 0 ? std::clamp(sum[c] / sum[3] + 0.5, 0.0, 255.0) : 0);
                    o[3] = uint8_t(std::clamp(sum[3] / count * 255 + 0.5, 0.0, 255.0));
                }
            return out;
        }

        // Eases out past the end and back (a short landing): 0 -> 1.
        float EaseOutBack(float t)
        {
            constexpr float C1 = 1.2f, C3 = C1 + 1;
            float u = t - 1;
            return 1 + C3 * u * u * u + C1 * u * u;
        }
    }

    ArtTexture LoadArt(SDL_Renderer* renderer, const std::string& path, bool crop, int maxWidth)
    {
        ArtTexture art;
        SDL_Surface* loaded = path.empty() ? nullptr : SDL_LoadPNG(path.c_str());
        if (loaded == nullptr)
            return art;
        SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loaded);
        if (rgba == nullptr)
            return art;
        const uint8_t* pixels = static_cast<const uint8_t*>(rgba->pixels);
        int left = 0, top = 0, right = rgba->w, bottom = rgba->h;
        if (crop)
        {
            left = rgba->w, top = rgba->h, right = 0, bottom = 0;
            for (int y = 0; y < rgba->h; y++)
                for (int x = 0; x < rgba->w; x++)
                    if (pixels[size_t(y) * rgba->pitch + size_t(x) * 4 + 3] > 8)
                        left = std::min(left, x), right = std::max(right, x + 1), top = std::min(top, y), bottom = std::max(bottom, y + 1);
            if (left >= right || top >= bottom)
                left = 0, top = 0, right = rgba->w, bottom = rgba->h;
        }
        int width = right - left, height = bottom - top;
        int outWidth = maxWidth > 0 && width > maxWidth ? maxWidth : width;
        int outHeight = std::max(1, int(std::lround(double(height) * outWidth / width)));
        std::vector<uint8_t> out = Shrink(pixels + size_t(top) * rgba->pitch + size_t(left) * 4, rgba->pitch, width, height, outWidth, outHeight);
        SDL_DestroySurface(rgba);
        art.texture = LoadRgba(renderer, out, outWidth, outHeight);
        art.aspect = float(width) / float(height);
        return art;
    }

    bool SystemReducesMotion()
    {
#if defined(__APPLE__)
        // System Settings > Accessibility > Display > Reduce motion.
        bool reduce = false;
        if (CFPropertyListRef value = CFPreferencesCopyAppValue(CFSTR("reduceMotion"), CFSTR("com.apple.universalaccess")))
        {
            reduce = CFGetTypeID(value) == CFBooleanGetTypeID() && CFBooleanGetValue(static_cast<CFBooleanRef>(value));
            CFRelease(value);
        }
        return reduce;
#elif defined(_WIN32)
        // Settings > Accessibility > Visual effects > Animation effects.
        BOOL animations = TRUE;
        return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) && !animations;
#else
        return false;
#endif
    }

    bool App::ReduceMotion() const
    {
        std::string value = SettingGet(m_config, "launcher", "reduce_motion", "auto");
        if (value == "auto" || value.empty())
            return SystemReducesMotion();
        return ParseBool(value);
    }

    const ArtTexture* App::CachedArt(const char* game, LauncherAssets::Role role, int maxWidth)
    {
        std::string key = fmt::format("{} {} {}", game, int(role), maxWidth);
        auto found = m_cachedArt.find(key);
        if (found == m_cachedArt.end())
        {
            LauncherAssets::Asset asset = LauncherAssets::Cached(game, role);
            found = m_cachedArt.emplace(key, asset.valid ? LoadArt(m_renderer, asset.path, role == LauncherAssets::Role::Logo, maxWidth) : ArtTexture{}).first;
        }
        return found->second ? &found->second : nullptr;
    }

    void App::ForgetCachedArt(const char* game)
    {
        std::string prefix = std::string(game) + " ";
        for (auto it = m_cachedArt.begin(); it != m_cachedArt.end();)
            if (it->first.starts_with(prefix))
            {
                it->second.Destroy();
                it = m_cachedArt.erase(it);
            }
            else
                ++it;
    }

    float App::LogoDrop()
    {
        // A new arrival on the game page (from another page, or another game shown), or the logo appearing while there
        // (the disc was being read): the logo falls in, once.
        uint64_t now = SDL_GetTicks();
        bool arrived = m_lastPage != Page::Play || m_logoDropSlot != m_s;
        bool appeared = m_s->logo && !m_logoDropHad;
        if (arrived || appeared)
            m_logoDropAt = ReduceMotion() ? 0 : now;
        m_logoDropSlot = m_s;
        m_logoDropHad = bool(m_s->logo);
        if (m_logoDropAt == 0)
            return 1;
        return std::clamp(float(now - m_logoDropAt) / LogoDropMs, 0.0f, 1.0f);
    }

    void App::DrawLogo(ImDrawList* draw, const ArtTexture& logo, ImVec2 at, ImVec2 size, float alpha, bool shadow)
    {
        if (!logo)
            return;
        float width = std::min(size.x, size.y * logo.aspect);
        // One limit for every game, so the logos have the same size on every page: the smallest logo on the discs (Swap
        // Force's, 872 px wide) at one texel per device pixel. Larger only blurs.
        constexpr float LogoMaxPixels = 872;
        width = std::min(width, LogoMaxPixels / std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x));
        float height = width / logo.aspect;
        ImVec2 min(at.x + (size.x - width) / 2, at.y + (size.y - height) / 2), max(min.x + width, min.y + height);
        if (shadow)
        {
            // A soft shadow under it: a few darkened copies, further and fainter.
            float step = std::max(1.5f, height * 0.012f);
            for (int i = 3; i >= 1; i--)
                draw->AddImage(Ref(logo.texture), ImVec2(min.x, min.y + step * i * 1.6f), ImVec2(max.x, max.y + step * i * 1.6f), logo.uv0, logo.uv1,
                    IM_COL32(4, 8, 30, int(alpha * (110 - 28 * i))));
        }
        draw->AddImage(Ref(logo.texture), min, max, logo.uv0, logo.uv1, IM_COL32(255, 255, 255, int(alpha * 255)));
    }

    void App::DrawHeroLogo(ImDrawList* draw, const ArtTexture& logo, ImVec2 at, ImVec2 size)
    {
        // It falls from LogoDropDistance above, quickly, lands with a short give, and fades in on the way.
        float t = LogoDrop(), fall = (1 - EaseOutBack(t)) * LogoDropDistance;
        DrawLogo(draw, logo, ImVec2(at.x, at.y - fall), size, std::clamp(t * 2.5f, 0.0f, 1.0f));
    }

    void App::DrawHeroSteps(ImVec2 at, ImVec2 size)
    {
        ImGui::SetCursorScreenPos(at);
        ui::BeginCard("##hero-steps", size, { .panel = true, .padding = ui::space::M });
        ui::Text(ui::TextStyle::Body, Tr("Three steps"), ui::color::Dim, -1, true);
        ImGui::Dummy(ImVec2(0, ui::space::M));
        const char* titles[] = { Tr("Choose your disc"), Tr("Install"), Tr("Play") };
        const std::string install = Tf("About {} min, {} GB", InstallMinutesFor(m_s->game.id), InstallGigabytes);
        const char* notes[] = { Tr("Your own copy, as an .iso"), install.c_str(), Tr("Controller, keyboard, real or virtual portal") };
        for (int i = 0; i < 3; i++)
            ui::GuideStep(i + 1, titles[i], notes[i], ImGui::GetContentRegionAvail().x, i == 0);
        ui::EndCard();
    }

    void App::DrawHeroInstall(ImVec2 at, float width)
    {
        const InstallProgress progress = m_testHero ? m_testHeroProgress : InstallSnapshot();
        const InstallChecklist list = MakeInstallChecklist(progress, SDL_GetTicks());
        const std::string action = list.current >= 0 ? list.rows[size_t(list.current)].title : Tr("Installing");
        const std::string percent = ui::FormatPercent(list.overall);
        const float percentW = ui::TextSize(ui::TextStyle::Title, percent.c_str()).x;
        ImGui::SetCursorScreenPos(at);
        ui::Text(ui::TextStyle::Label, action.c_str(), ui::color::Ink, width - percentW - ui::space::M, true);
        ImGui::SetCursorScreenPos(ImVec2(at.x + width - percentW, at.y));
        ui::Text(ui::TextStyle::Title, percent.c_str());
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + ui::type::Title + ui::space::S));
        ui::ProgressBar(list.overall, width);
        ImGui::SetCursorScreenPos(ImVec2(at.x, ImGui::GetItemRectMax().y + ui::space::S));
        const char* steps[] = { Tr("Disc checked"), Tr("Code"), Tr("Shaders"), Tr("Ready") };
        int current = 0;
        if (list.current >= 0)
        {
            const std::string& id = list.rows[size_t(list.current)].id;
            current = id == "kit" || id == "toolchain" || id == "disc" ? 0 : id == "recompile" ? 1 : id == "shaders" ? 2 : 3;
        }
        for (int i = 0; i < 4; i++)
        {
            if (i > 0) ImGui::SameLine(0, ui::space::XS);
            ui::StatusPill(i < current ? ui::Status::Success : i == current ? ui::Status::Busy : ui::Status::Neutral, steps[i]);
        }
        const ui::ButtonOptions cancel{ .size = ui::Size::Small };
        ImGui::SetCursorScreenPos(ImVec2(at.x + width - ui::ButtonWidth(Tr("Cancel"), cancel), at.y + 2 * ui::height::Normal));
        if (ui::Button(Tr("Cancel"), cancel))
        {
            m_s->installer.Cancel();
            m_s->kitDownload.Cancel();
        }
        TakeFocus(FocusTo::Page);
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + 2 * ui::height::Normal));
        if (ui::Button(Tr("Show details"), { .kind = ui::ButtonKind::Ghost, .size = ui::Size::Small }))
            m_s->installLog = true;
        ImGui::SameLine();
        if (ui::IconButton("##hero-install-settings", ui::Icon::Gear, Tr("Game settings"), { .size = ui::Size::Small, .overArt = true }))
            GoTo(Page::GameSettings);
    }

    void App::TestHeroScript(uint64_t now)
    {
        // Presentation-only fixtures: no installation, process launch or real figure is needed.
        if (m_testStep == 0 && m_testHeroState < 0)
        {
            m_page = Page::Play;
            m_focusTo = FocusTo::Page;
            if (!m_testDrop.empty()) StartVerify(m_testDrop);
            m_testHeroState = 0;
            return;
        }
        if (m_s->disc == DiscState::Verifying) return;
        // The Ready fixture represents a completed build, including its usual setup hint rules.
        m_s->installed.runtime = "test-built";
        m_s->installed.gameVersion = "99.0.0";
        if (m_testStep >= 8)
        {
            TestHeroNavigation();
            return;
        }
        m_testHeroState = m_testStep / 2;
        m_testHeroNoArt = m_testStep % 2 == 1;
        m_s->collection->TestHeroPortal(m_testHeroState == 2, m_testHeroState == 3);
        const char* states[] = { "ready", "needs-disc", "installing", "running" };
        const ArtTexture* background = CachedArt(m_s->game.id, LauncherAssets::Role::HeroBackground, BackdropArtW);
        const ArtTexture* logo = m_s->logo ? &m_s->logo : CachedArt(m_s->game.id, LauncherAssets::Role::Logo, 0);
        if (!m_testHeroNoArt && (background == nullptr || logo == nullptr))
        {
            fprintf(stderr, "FAIL hero %s-art: disc logo or background unavailable; fallback rendered\n", states[m_testHeroState]);
            m_testFailures++;
        }
        m_testHeroProgress.state = InstallProgress::State::Running;
        m_testHeroProgress.steps = { { "toolchain", "", now - 4000, now - 3000 }, { "disc", "", now - 3000, now - 2000 },
            { "recompile", "", now - 2000, 0 } };
        m_testHeroProgress.done = 42;
        m_testHeroProgress.total = 100;
        m_page = Page::Play;
        RequestFocus(FocusTo::Page);
        m_testShot = fmt::format("hero-{}-{}", states[m_testHeroState], m_testHeroNoArt ? "noart" : "art");
        m_testAt = now + 700;
    }

    void App::TestHeroNavigation()
    {
        const char* states[] = { "ready", "needs-disc", "installing", "running" };
        if (m_heroNavCase >= 8)
        {
            fprintf(stderr, "hero: %d failures\n", m_testFailures);
            m_running = false;
            return;
        }
        const bool pad = m_heroNavCase >= 4;
        if (m_heroNavPhase < 0)
        {
            m_testHeroState = m_heroNavCase % 4;
            m_testHeroNoArt = true;
            m_testPadInput = pad;
            m_s->collection->TestHeroPortal(m_testHeroState == 2, m_testHeroState == 3);
            RequestFocus(FocusTo::Page);
            m_focusShow = true;
            m_heroNavWarmup = 3;
            m_heroNavMoves = 0;
            m_heroNavSeen.clear();
            m_heroNavExpected.clear();
            m_heroNavEdges.clear();
            m_heroNavPhase = 0;
            return;
        }
        if (m_heroNavWarmup > 0)
        {
            m_heroNavWarmup--;
            return;
        }
        if (m_heroNavExpected.empty())
            m_heroNavExpected = m_testHeroControls;
        const uint32_t focused = ImGui::GetCurrentContext()->NavId;
        if (std::find(m_heroNavSeen.begin(), m_heroNavSeen.end(), focused) == m_heroNavSeen.end())
            m_heroNavSeen.push_back(focused);
        const ImGuiKey keys[] = { ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadLeft };
        ImGuiIO& io = ImGui::GetIO();
        if (m_heroNavPhase == 1)
        {
            io.AddKeyEvent(pad ? keys[m_heroNavDirection] : ImGuiKey_Tab, false);
            m_heroNavPhase = 2;
            return;
        }
        if (m_heroNavPhase == 2)
        {
            m_heroNavEdges[{m_heroNavFrom, m_heroNavDirection}] = focused;
            m_heroNavPhase = 0;
        }
        auto unexplored = [&](uint32_t id) {
            for (int i = 0; i < 4; i++)
                if (!m_heroNavEdges.contains({id, i})) return i;
            return -1;
        };
        int missing = 0;
        for (uint32_t id : m_heroNavExpected)
            if (std::find(m_heroNavSeen.begin(), m_heroNavSeen.end(), id) == m_heroNavSeen.end()) missing++;
        int direction = 0;
        if (pad && missing > 0)
        {
            direction = unexplored(focused);
            if (direction < 0)
            {
                // Follow known directional moves to a control with an unexplored neighbour.
                std::vector<std::pair<uint32_t, int>> queue{ {focused, -1} };
                std::vector<uint32_t> seen{focused};
                for (size_t at = 0; at < queue.size() && direction < 0; at++)
                {
                    const auto [id, first] = queue[at];
                    if (id != focused && unexplored(id) >= 0)
                    {
                        direction = first;
                        break;
                    }
                    for (int i = 0; i < 4; i++)
                    {
                        const auto edge = m_heroNavEdges.find({id, i});
                        if (edge == m_heroNavEdges.end()) continue;
                        const uint32_t to = edge->second;
                        if (std::find(seen.begin(), seen.end(), to) != seen.end()) continue;
                        seen.push_back(to);
                        queue.push_back({to, first < 0 ? i : first});
                    }
                }
            }
        }
        if (missing == 0 || m_heroNavMoves >= 160 || direction < 0)
        {
            fprintf(stderr, "%shero navigation %s %s: %zu controls, %d unreachable\n", missing > 0 ? "FAIL " : "",
                states[m_testHeroState], pad ? "pad" : "keyboard", m_heroNavExpected.size(), missing);
            if (missing > 0)
                for (uint32_t id : m_heroNavExpected)
                    if (std::find(m_heroNavSeen.begin(), m_heroNavSeen.end(), id) == m_heroNavSeen.end())
                        fprintf(stderr, "hero navigation missing: %s\n", ui::Ctx().testNavigationLabels[id].c_str());
            if (missing > 0)
            {
                m_testFailures++;
                if (pad)
                    for (const auto& [move, target] : m_heroNavEdges)
                        fprintf(stderr, "hero navigation edge: %s / %d -> %s\n", ui::Ctx().testNavigationLabels[move.first].c_str(), move.second,
                            ui::Ctx().testNavigationLabels[target].c_str());
            }
            m_heroNavCase++;
            m_heroNavPhase = -1;
            return;
        }
        m_heroNavFrom = focused;
        m_heroNavDirection = direction;
        io.AddKeyEvent(pad ? keys[direction] : ImGuiKey_Tab, true);
        m_heroNavMoves++;
        m_heroNavPhase = 1;
    }

    App::MainActionHit App::DrawMainAction(ImVec2 at, const MainAction& action)
    {
        // ui::MainAction (components/buttons.h) in the shown game's colours; it takes the page's focus when asked.
        ui::MainActionOptions options;
        options.label = action.label;
        options.enabled = action.enabled;
        options.progress = action.progress;
        options.indeterminate = action.indeterminate;
        options.style = &StyleOf(m_s->game.id);
        options.separated = true;
        options.icon = action.icon;
        options.danger = action.danger;
        options.width = action.width;
        const std::string tooltip = Tf("{} settings", m_s->game.name);
        options.gearTooltip = tooltip.c_str();
        options.takeFocus = m_focusTo == FocusTo::Page;
        options.showFocus = m_focusShow;
        if (options.takeFocus)
            m_focusTo = FocusTo::None;
        ImGui::SetCursorScreenPos(at);
        ui::MainActionResult result = ui::MainAction(options);
        m_testMainAt = result.mainCentre;
        m_testGearAt = result.gearCentre;
        return result.hit == ui::MainActionHit::Main ? MainActionHit::Main
            : result.hit == ui::MainActionHit::Settings ? MainActionHit::Settings : MainActionHit::None;
    }
}
