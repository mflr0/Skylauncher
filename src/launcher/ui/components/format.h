#pragma once

// One helper for each kind of value a screen writes, in the player's language: sizes, percentages, numbers, dates and
// times (docs/launcher_ui.md, "Consistency"). A screen never formats these itself. Game names: game_names.h.
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

namespace ui
{
    std::string FormatSize(uint64_t bytes);       // "8.1 GB", "640 MB", "12 KB", "512 bytes"; French "8,1 Go"
    std::string FormatPercent(float fraction);    // 0.46 -> "46%"; French "46 %" (rounded down: 100% only when done)
    std::string FormatNumber(long long value);    // "12,345"; French "12 345"
    std::string FormatDate(std::time_t when);     // "28 Oct 2026"; French "28 oct. 2026" (local time)
    std::string FormatTime(std::time_t when);     // "15:05" (local time, 24 hours)
    // "Today 15:05", "Yesterday 15:05", else the date: when something happened (a backup, a deletion).
    std::string FormatWhen(std::time_t when, std::time_t now);
    // An ISO 8601 UTC time as the collection writes it ("2026-10-24T10:15:00Z"); 0 when it can't be read.
    std::time_t ParseIsoUtc(std::string_view iso);
}
