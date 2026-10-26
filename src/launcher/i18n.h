#pragma once

// The launcher's own text in the player's language (docs/translations.md). English is the source: the key of a string is
// its English text, written in the code as Tr("..."). A language is one file, resources/lang/<code>.txt, built into the
// executable; a string a language doesn't have shows in English.
#include <fmt/format.h>
#include <string>
#include <string_view>
#include <vector>

#define N_(text) text // marks an English string that is translated later, where it is used (a table, a constant)

namespace i18n
{
    struct Language
    {
        std::string code; // "fr": the code the settings file and the game use (language.h)
        std::string name; // in its own language: "Français"
    };

    // The languages with a file, English first.
    const std::vector<Language>& Languages();

    // The language the text is in now ("en" when none). Cheap to call every frame; the text changes at once.
    void SetLanguage(std::string_view code);
    const std::string& Current();

    // The translation of an English string, or the string itself. The pointer is valid until the language changes:
    // keep a std::string, not the pointer, past the frame.
    const char* Tr(const char* english);

    // A formatted string: its translation, with `{}` or `{0}`, `{1}`... in place of the arguments (a language may put them in
    // another order, with the numbered form). A translation that doesn't fit the arguments is replaced by the English one.
    template<typename... Args>
    std::string Tf(const char* english, Args&&... args)
    {
        try
        {
            return fmt::format(fmt::runtime(Tr(english)), args...);
        }
        catch (const fmt::format_error&)
        {
            return fmt::format(fmt::runtime(english), args...);
        }
    }

    // Which language to show: `setting` is [launcher] language ("game": the game's text language; a code; empty: the
    // system's), `gameLanguage` the game's text language code (empty: none chosen), `system` the system's language codes in
    // order of preference. Returns a code of Languages(), "en" when none fits.
    std::string Resolve(std::string_view setting, std::string_view gameLanguage, const std::vector<std::string>& system);
    bool Has(std::string_view code);

    // The language files: for the tests, which read the files of a source tree.
    struct Catalog
    {
        std::string code, name;
        std::vector<std::pair<std::string, std::string>> entries; // English, translation
    };
    bool ParseCatalog(std::string_view text, Catalog& out);
}

using i18n::Tr;
using i18n::Tf;
