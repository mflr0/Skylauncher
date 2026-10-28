// A game's achievements (the Achievements button on its Play page): the list the disc check cached, what the game recorded as
// unlocked, and the play time (game_progress.h). docs/launcher_ui.md, "Achievements and play time".
#include "launcher/ui/launcher_app.h"
#include "launcher/game_progress.h"
#include <algorithm>
#include <ctime>
#include <fmt/format.h>

namespace launcher_ui
{
    namespace
    {
        constexpr float Picture = ui::height::Large - 2 * ui::space::XXS; // an achievement's picture, as a list row's image
        constexpr double RefreshMs = 1500;
    }

    // The progress of the shown game and the launcher's language, read again every 1.5 s (the game may be running and unlocking).
    void App::RefreshProgress(bool force)
    {
        const uint64_t now = SDL_GetTicks();
        if (!force && m_progressSlot == m_s && m_progressLanguage == i18n::Current() && now - m_progressAt < RefreshMs)
            return;
        m_progress = ReadGameProgress(m_s->game.id, i18n::Current());
        m_progressSlot = m_s;
        m_progressLanguage = i18n::Current();
        m_progressAt = now;
    }

    const ArtTexture* App::AchievementIcon(const std::filesystem::path& image)
    {
        if (image.empty())
            return nullptr;
        const std::string key = image.string();
        auto found = m_achievementIcons.find(key);
        if (found == m_achievementIcons.end())
            found = m_achievementIcons.emplace(key, LoadArt(m_renderer, key, false)).first;
        return found->second ? &found->second : nullptr;
    }

    // A settings page's frame (the game's name, Back), one card: the totals over a bar, the play time, then a row per
    // achievement: its picture, its name over its description, its score on the name's baseline and the day it was unlocked
    // under it. The rows are not pressed: the keyboard's and the pad's up and down scroll the page.
    void App::DrawAchievements()
    {
        RefreshProgress();
        const GameProgress& progress = m_progress;
        const char* title = GameName(m_s->game.id, GameNameForm::Full);
        BeginSettingsPage("##achievementscolumn", title != nullptr ? title : m_s->game.name, true);
        if (!ImGui::IsAnyItemActive())
        {
            const float step = ui::height::Large;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickDown))
                ImGui::SetScrollY(ImGui::GetScrollY() + step);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickUp))
                ImGui::SetScrollY(ImGui::GetScrollY() - step);
        }
        BeginCard("##achievements", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        if (!progress.hasList)
        {
            ui::SectionHeader(Tr("Achievements"), Tr("The list comes from your disc: it appears once the game's disc image is checked."), false);
            EndCard();
            EndSettingsPage();
            return;
        }
        ui::SectionHeader(Tr("Achievements"), Tf("{} of {} unlocked · {} of {} G", progress.unlocked, progress.rows.size(), progress.gamerscore,
            progress.totalGamerscore).c_str(), false);
        ImGui::Dummy(ImVec2(0, ui::space::XXS));
        ui::ProgressBar(progress.totalGamerscore > 0 ? float(progress.gamerscore) / float(progress.totalGamerscore) : 0.0f);
        if (std::string line = PlayTimeLine(progress.play, int64_t(std::time(nullptr))); !line.empty())
            ui::Text(ui::TextStyle::Caption, line.c_str(), ui::color::InkSoft);
        ImGui::Dummy(ImVec2(0, ui::space::XS));

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float cardWidth = ImGui::GetContentRegionAvail().x;
        const float rightW = 4 * ui::height::Small; // the score and the date
        const float textX = Picture + ui::space::M, textW = cardWidth - textX - rightW - ui::space::M;
        for (size_t i = 0; i < progress.rows.size(); i++)
        {
            const AchievementRow& row = progress.rows[i];
            const float descriptionH = ui::TextSize(ui::TextStyle::Caption, row.description.c_str(), nullptr, false, textW).y;
            const float blockH = ui::type::Body + ui::space::XXS + descriptionH;
            const float rowH = std::max(Picture, blockH) + 2 * ui::space::S;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(cardWidth, rowH));
            if (i > 0)
                draw->AddLine(ImVec2(at.x, at.y + 0.5f), ImVec2(at.x + cardWidth, at.y + 0.5f), ui::Col(ui::color::Hairline, 110.0f / 255), ui::stroke::Hairline);

            // The picture (a locked one dimmed) and the text, each centred on the row.
            const float pictureTop = std::round(at.y + (rowH - Picture) / 2), top = std::round(at.y + (rowH - blockH) / 2);
            if (const ArtTexture* icon = AchievementIcon(row.image))
                draw->AddImageRounded(Ref(icon->texture), ImVec2(at.x, pictureTop), ImVec2(at.x + Picture, pictureTop + Picture), icon->uv0, icon->uv1,
                    row.unlocked ? IM_COL32_WHITE : IM_COL32(90, 90, 98, 255), ui::radius::Control);
            else
                draw->AddRectFilled(ImVec2(at.x, pictureTop), ImVec2(at.x + Picture, pictureTop + Picture), ui::Col(ui::color::Inset), ui::radius::Control);
            ui::PushText(ui::TextStyle::Body, true);
            ui::DrawText(draw, ImVec2(at.x + textX, top), ui::Col(row.unlocked ? ui::color::Ink : ui::color::Dim), row.name.c_str());
            ui::PopText();
            ui::PushText(ui::TextStyle::Caption);
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + textX, top + ui::type::Body + ui::space::XXS),
                ui::Col(row.unlocked ? ui::color::InkSoft : ui::color::Dim), row.description.c_str(), nullptr, textW);
            ui::PopText();

            // The score on the name's baseline, the day under it on the description's first line.
            const std::string score = fmt::format("{} G", row.gamerscore);
            const float right = at.x + cardWidth, baseline = top + ui::Baseline(ui::TextStyle::Body, true);
            ui::PushText(ui::TextStyle::Body, true);
            ui::DrawText(draw, ImVec2(right - ImGui::CalcTextSize(score.c_str()).x, baseline - ui::Baseline(ui::TextStyle::Body, true)),
                ui::Col(row.unlocked ? ui::color::AccentFill : ui::color::Dim), score.c_str());
            ui::PopText();
            if (row.unlocked && row.time > 0)
            {
                const std::string day = ui::FormatDate(row.time);
                ui::PushText(ui::TextStyle::Caption);
                ui::DrawText(draw, ImVec2(right - ImGui::CalcTextSize(day.c_str()).x, top + ui::type::Body + ui::space::XXS), ui::Col(ui::color::InkSoft), day.c_str());
                ui::PopText();
            }
        }
        EndCard();
        if (m_testAchievements && m_testStep >= 3)
            ImGui::SetScrollY(ImGui::GetScrollMaxY()); // the screenshot test's last rows
        EndSettingsPage();
    }
}
