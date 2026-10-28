#pragma once

// What the game page shows of a player's progress in a game: the achievements (the list read from the player's disc and cached
// by the disc check, with what the game recorded as unlocked) and the play time. docs/launcher_ui.md, "Achievements and play time".
#include "shared/collection/achievements.h"
#include "shared/collection/progress.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct AchievementRow
{
    uint32_t id = 0;
    std::string name;        // a hidden one that is still locked shows "Hidden achievement"
    std::string description; // unlocked: what it was for; locked: the game's own text for it
    uint32_t gamerscore = 0;
    bool hidden = false;
    bool unlocked = false;
    int64_t time = 0; // unix time of the unlock
    std::filesystem::path image; // the cached picture; empty when there is none
};

struct GameProgress
{
    bool hasList = false; // the cached list exists (the disc was checked by this version)
    std::vector<AchievementRow> rows;
    uint32_t unlocked = 0;
    uint32_t gamerscore = 0;      // of the unlocked ones
    uint32_t totalGamerscore = 0;
    progress::PlayTime play;
};

// The folder of a game's cached achievements (a game_art cache folder).
std::filesystem::path AchievementCacheFolder(std::string_view game);

// Rows from a list and the unlocks, in the list's order; the texts are those of the list already (its language).
GameProgress BuildProgress(const achievements::List& list, const std::vector<progress::Unlock>& unlocks,
    const std::filesystem::path& imageFolder);

// The cached list in `language` (the launcher's language code) with the game's unlocks and play time.
GameProgress ReadGameProgress(std::string_view game, std::string_view language);

// "12 h 34", "34 min", "less than a minute".
std::string FormatPlayTime(int64_t seconds);
// "today", "yesterday", "3 days ago", else the date "2026-10-28". `now` and `time` are unix times, compared by local day.
std::string FormatLastPlayed(int64_t time, int64_t now);
// "2026-10-28" in local time.
std::string FormatDate(int64_t time);
// "Play time: 12 h 34 · Last played: today"; empty when the game was never played.
std::string PlayTimeLine(const progress::PlayTime& play, int64_t now);
