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
        constexpr float IconSize = 56, RowPadY = 12, RowGap = 16;
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

    void App::DrawAchievements()
    {
        RefreshProgress();
        const GameProgress& progress = m_progress;

        float available = ImGui::GetContentRegionAvail().x;
        float width = std::min(available, 940.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) / 2);
        BeginPageColumn("##achievementscolumn", width, true);

        Title(m_s->game.name, HeadlineSize, Text);
        ImGui::SameLine(ImGui::GetContentRegionMax().x - PillWidth(Tr("Back"), 120));
        if (PillButton(Tr("Back"), false, 120))
            GoTo(Page::Play);
        ImGui::Dummy(ImVec2(0, 4));

        Title(Tr("Achievements"), TitleSize, Text);
        if (!progress.hasList)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextWrapped("%s", Tr("The list comes from your disc: it appears once the game's disc image is checked."));
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            EndPageColumn();
            return;
        }

        // The totals, a bar, and the play time.
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
        ImGui::TextUnformatted(Tf("{} of {} unlocked · {} of {} G", progress.unlocked, progress.rows.size(), progress.gamerscore,
            progress.totalGamerscore).c_str());
        if (std::string line = PlayTimeLine(progress.play, int64_t(std::time(nullptr))); !line.empty())
            ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();
        {
            ImVec2 at = ImGui::GetCursorScreenPos();
            float barWidth = ImGui::GetContentRegionAvail().x, filled = progress.totalGamerscore > 0
                ? barWidth * float(progress.gamerscore) / float(progress.totalGamerscore) : 0.0f;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(ImVec2(at.x, at.y + 6), ImVec2(at.x + barWidth, at.y + 14), Rgb(Field), 4);
            if (filled > 0)
                draw->AddRectFilled(ImVec2(at.x, at.y + 6), ImVec2(at.x + std::max(filled, 8.0f), at.y + 14), Rgb(Accent), 4);
            ImGui::Dummy(ImVec2(0, 22));
        }

        BeginCard("##achievementslist", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        const float cardWidth = ImGui::GetContentRegionAvail().x;
        const float textX = IconSize + RowGap, rightW = 120, textW = cardWidth - textX - rightW - RowGap;
        for (size_t i = 0; i < progress.rows.size(); i++)
        {
            const AchievementRow& row = progress.rows[i];
            ImFont* font = ImGui::GetFont();
            const float size = ImGui::GetFontSize();
            const float descriptionH = ImGui::CalcTextSize(row.description.c_str(), nullptr, false, textW).y;
            const float nameH = size + 4;
            const float rowH = std::max(IconSize, nameH + descriptionH) + 2 * RowPadY;

            ImGui::PushID(int(row.id));
            ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##row", ImVec2(cardWidth, rowH), ImGuiButtonFlags_EnableNav);
            FocusRing(RadiusSmall, 0);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            if (i > 0)
                draw->AddLine(ImVec2(at.x, at.y), ImVec2(at.x + cardWidth, at.y), Rgb(Border, 110));

            // The picture (the locked ones dimmed), the name, the description.
            const float top = at.y + RowPadY;
            if (const ArtTexture* icon = AchievementIcon(row.image))
                draw->AddImageRounded(Ref(icon->texture), ImVec2(at.x, top), ImVec2(at.x + IconSize, top + IconSize), icon->uv0, icon->uv1,
                    row.unlocked ? IM_COL32_WHITE : IM_COL32(90, 90, 98, 255), 8);
            else
                draw->AddRectFilled(ImVec2(at.x, top), ImVec2(at.x + IconSize, top + IconSize), Rgb(Field), 8);
            const unsigned ink = row.unlocked ? Text : TextDim;
            AddBoldText(draw, ImVec2(at.x + textX, top), Rgb(ink), row.name.c_str());
            draw->AddText(font, size, ImVec2(at.x + textX, top + nameH), Rgb(row.unlocked ? TextSecondary : TextDim), row.description.c_str(),
                nullptr, textW);

            // The score, and the day it was unlocked.
            const std::string score = fmt::format("{} G", row.gamerscore);
            const float scoreW = ImGui::CalcTextSize(score.c_str()).x, right = at.x + cardWidth;
            AddBoldText(draw, ImVec2(right - scoreW, top), Rgb(row.unlocked ? Accent : TextDim), score.c_str());
            if (row.unlocked && row.time > 0)
            {
                const std::string day = FormatDate(row.time);
                draw->AddText(font, SmallSize, ImVec2(right - ImGui::CalcTextSize(day.c_str()).x * SmallSize / size, top + nameH),
                    Rgb(TextSecondary), day.c_str());
            }
            ImGui::PopID();
        }
        EndCard();
        if (m_testAchievements && m_testStep >= 3)
            ImGui::SetScrollY(ImGui::GetScrollMaxY()); // the screenshot test's last rows
        EndPageColumn();
    }
}
