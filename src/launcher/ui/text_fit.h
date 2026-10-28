#pragma once

// Cuts a UTF-8 text to a width with "..." at its end. Each try starts from the original text, so it always ends: whole
// characters are dropped one at a time until the text and its "..." fit, and "..." is added once. `measure` gives a
// text's width (ImGui::CalcTextSize(...).x in the window). A text that fits is returned as it is; when not even "..."
// fits, the result is empty.
#include <string>
#include <string_view>

namespace launcher_ui
{
    template <typename Measure>
    std::string FitText(std::string_view text, float width, Measure&& measure)
    {
        if (measure(std::string(text)) <= width)
            return std::string(text);
        size_t end = text.size();
        while (end > 0)
        {
            --end;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
                --end; // back to the start of a character
            std::string cut = std::string(text.substr(0, end)) + "...";
            if (measure(cut) <= width)
                return cut;
        }
        return measure("...") <= width ? "..." : "";
    }
}
