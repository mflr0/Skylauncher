// SkylauncherTest's checks for the text cut to a width with "..." (launcher/ui/text_fit.h): the text is kept when it fits,
// cut by whole characters otherwise, and a width too small for "..." gives an empty text. Measures one unit per UTF-8
// character. Called from launcher_test.cpp's main; returns the number of failures.
#include "launcher/ui/text_fit.h"
#include <cstdio>
#include <string>

namespace
{
    int g_failures = 0;

#define FIT_EXPECT(condition) \
    do { if (!(condition)) { fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition); g_failures++; } } while (0)

    float Width(const std::string& text)
    {
        float characters = 0;
        for (unsigned char c : text)
            characters += (c & 0xC0) != 0x80;
        return characters;
    }

    std::string Fit(const std::string& text, float width) { return launcher_ui::FitText(text, width, Width); }
}

int TextFitChecks()
{
    g_failures = 0;
    FIT_EXPECT(Fit("Ready", 5) == "Ready");
    FIT_EXPECT(Fit("Ready", 50) == "Ready");
    FIT_EXPECT(Fit("", 0) == "");
    FIT_EXPECT(Fit("Downloading the tools 79 %", 10) == "Downloa...");
    FIT_EXPECT(Fit("Downloading the tools 79 %", 4) == "D...");
    FIT_EXPECT(Fit("Downloading the tools 79 %", 3) == "...");
    // Accented text: whole characters, never half of one.
    FIT_EXPECT(Fit("T\xC3\xA9l\xC3\xA9" "chargement des outils 79 %", 8) == "T\xC3\xA9l\xC3\xA9" "c...");
    FIT_EXPECT(Fit("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 3) == "...");
    FIT_EXPECT(Fit("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 4) == "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9");
    FIT_EXPECT(Fit("\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC", 4) == "\xE2\x82\xAC...");
    // Narrower than "...", zero and negative widths: empty, and it returns.
    FIT_EXPECT(Fit("Downloading", 2) == "");
    FIT_EXPECT(Fit("Downloading", 0) == "");
    FIT_EXPECT(Fit("Downloading", -5) == "");
    FIT_EXPECT(Fit("T\xC3\xA9l\xC3\xA9", 1) == "");
    FIT_EXPECT(Fit("", -1) == "");
    // The same text twice gives the same cut: nothing carries over from one call to the next.
    FIT_EXPECT(Fit("Downloading the tools 79 %", 12) == Fit("Downloading the tools 79 %", 12));
    FIT_EXPECT(Fit("Downloading the tools 79 %", 12) == "Downloadi...");
    return g_failures;
}
