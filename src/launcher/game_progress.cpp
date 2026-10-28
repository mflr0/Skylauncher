#include "launcher/game_progress.h"
#include "launcher/i18n.h"
#include "shared/art/game_art.h"
#include <ctime>
#include <fmt/format.h>

namespace
{
    std::tm LocalTime(int64_t time)
    {
        std::time_t t = std::time_t(time);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &t);
#else
        localtime_r(&t, &local);
#endif
        return local;
    }

    // Days since 1970-01-01 of a civil date (proleptic Gregorian).
    int64_t DaysFromCivil(int64_t y, int64_t m, int64_t d)
    {
        y -= m <= 2;
        int64_t era = (y >= 0 ? y : y - 399) / 400;
        int64_t yoe = y - era * 400;
        int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    int64_t LocalDay(int64_t time)
    {
        std::tm local = LocalTime(time);
        return DaysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    }
}

std::filesystem::path AchievementCacheFolder(std::string_view game)
{
    std::filesystem::path cache = art::CacheFolder(game);
    return cache.empty() ? cache : cache / "achievements";
}

GameProgress BuildProgress(const achievements::List& list, const std::vector<progress::Unlock>& unlocks,
    const std::filesystem::path& imageFolder)
{
    GameProgress result;
    result.hasList = !list.items.empty();
    for (const achievements::Info& info : list.items)
    {
        AchievementRow row;
        row.id = info.id;
        row.gamerscore = info.gamerscore;
        row.hidden = info.hidden;
        for (const progress::Unlock& unlock : unlocks)
            if (unlock.id == info.id)
            {
                row.unlocked = true;
                row.time = unlock.time;
            }
        // The game's own text for a locked one; a hidden one keeps its name to itself until it is unlocked.
        row.name = info.hidden && !row.unlocked ? Tr("Hidden achievement") : info.name;
        row.description = row.unlocked || info.lockedDescription.empty() ? info.unlockedDescription : info.lockedDescription;
        if (!imageFolder.empty() && info.imageId != 0)
        {
            std::filesystem::path image = achievements::CachedImage(imageFolder, info.imageId);
            std::error_code error;
            if (std::filesystem::exists(image, error))
                row.image = image;
        }
        result.totalGamerscore += info.gamerscore;
        if (row.unlocked)
        {
            result.unlocked++;
            result.gamerscore += info.gamerscore;
        }
        result.rows.push_back(std::move(row));
    }
    return result;
}

GameProgress ReadGameProgress(std::string_view game, std::string_view language)
{
    std::filesystem::path images = AchievementCacheFolder(game);
    GameProgress result;
    std::filesystem::path folder = progress::Folder(game);
    std::vector<progress::Unlock> unlocks;
    if (!folder.empty())
        unlocks = progress::ReadUnlocks(folder);
    if (!images.empty())
        result = BuildProgress(achievements::ReadCache(images, language), unlocks, images);
    if (!folder.empty())
        result.play = progress::ReadPlayTime(folder);
    return result;
}

std::string FormatPlayTime(int64_t seconds)
{
    int64_t minutes = seconds / 60;
    if (minutes <= 0)
        return Tr("less than a minute");
    if (minutes < 60)
        return Tf("{} min", minutes);
    return fmt::format("{} h {:02}", minutes / 60, minutes % 60);
}

std::string FormatDate(int64_t time)
{
    std::tm local = LocalTime(time);
    return fmt::format("{:04}-{:02}-{:02}", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

std::string FormatLastPlayed(int64_t time, int64_t now)
{
    int64_t days = LocalDay(now) - LocalDay(time);
    if (days <= 0)
        return Tr("today");
    if (days == 1)
        return Tr("yesterday");
    if (days < 31)
        return Tf("{} days ago", days);
    return FormatDate(time);
}

std::string PlayTimeLine(const progress::PlayTime& play, int64_t now)
{
    if (play.sessions == 0 && play.totalSeconds == 0)
        return {};
    std::string line = Tf("Play time: {}", FormatPlayTime(play.totalSeconds));
    if (play.lastEnd > 0)
        line += " \xC2\xB7 " + Tf("Last played: {}", FormatLastPlayed(play.lastEnd, now));
    return line;
}
