// Values in the player's language (format.h).
#include "launcher/ui/components/format.h"
#include "launcher/i18n.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui
{
    namespace
    {
        // "8.1" with the language's decimal mark ("8,1").
        std::string Decimal(double value, int decimals)
        {
            std::string text = fmt::format("{:.{}f}", value, decimals);
            if (size_t dot = text.find('.'); dot != std::string::npos)
                text.replace(dot, 1, Tr(N_("."))); // the decimal mark: French ","
            return text;
        }

        bool LocalTime(std::time_t when, std::tm& out)
        {
#if defined(_WIN32)
            return localtime_s(&out, &when) == 0;
#else
            return localtime_r(&when, &out) != nullptr;
#endif
        }

        constexpr const char* Months[] = { N_("Jan"), N_("Feb"), N_("Mar"), N_("Apr"), N_("May"), N_("Jun"), N_("Jul"), N_("Aug"),
            N_("Sep"), N_("Oct"), N_("Nov"), N_("Dec") };
    }

    std::string FormatSize(uint64_t bytes)
    {
        constexpr double KB = 1024, MB = KB * 1024, GB = MB * 1024;
        if (bytes >= uint64_t(GB))
            return Tf("{} GB", Decimal(double(bytes) / GB, 1));
        if (bytes >= uint64_t(MB))
            return Tf("{} MB", Decimal(double(bytes) / MB, 0));
        if (bytes >= uint64_t(KB))
            return Tf("{} KB", Decimal(double(bytes) / KB, 0));
        return Tf("{} bytes", bytes);
    }

    std::string FormatPercent(float fraction)
    {
        return Tf("{}%", int(std::floor(std::clamp(fraction, 0.0f, 1.0f) * 100)));
    }

    std::string FormatNumber(long long value)
    {
        std::string digits = std::to_string(value < 0 ? -value : value), out;
        const std::string separator = Tr(N_(",")); // the thousands separator: French a narrow no-break space
        for (size_t i = 0; i < digits.size(); i++)
        {
            if (i > 0 && (digits.size() - i) % 3 == 0)
                out += separator;
            out += digits[i];
        }
        return value < 0 ? "-" + out : out;
    }

    std::string FormatDate(std::time_t when)
    {
        std::tm local{};
        if (!LocalTime(when, local))
            return {};
        return Tf("{0} {1} {2}", local.tm_mday, Tr(Months[local.tm_mon]), local.tm_year + 1900); // a language may reorder them
    }

    std::string FormatTime(std::time_t when)
    {
        std::tm local{};
        if (!LocalTime(when, local))
            return {};
        return fmt::format("{:02}:{:02}", local.tm_hour, local.tm_min);
    }

    std::string FormatWhen(std::time_t when, std::time_t now)
    {
        std::tm a{}, b{};
        if (!LocalTime(when, a) || !LocalTime(now, b))
            return {};
        std::tm yesterday = b;
        yesterday.tm_mday -= 1;
        std::time_t y = std::mktime(&yesterday);
        std::tm c{};
        LocalTime(y, c);
        if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday)
            return Tf("Today {}", FormatTime(when));
        if (a.tm_year == c.tm_year && a.tm_yday == c.tm_yday)
            return Tf("Yesterday {}", FormatTime(when));
        return FormatDate(when);
    }

    std::time_t ParseIsoUtc(std::string_view iso)
    {
        std::tm utc{};
        std::string text(iso);
        if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &utc.tm_year, &utc.tm_mon, &utc.tm_mday, &utc.tm_hour, &utc.tm_min, &utc.tm_sec) < 5)
            return 0;
        utc.tm_year -= 1900;
        utc.tm_mon -= 1;
#if defined(_WIN32)
        return _mkgmtime(&utc);
#else
        return timegm(&utc);
#endif
    }
}
