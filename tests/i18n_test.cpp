// ArkeyanLauncherTest's checks for the launcher's languages: every string the code translates has an entry in every language file
// (the key coverage test), the entries keep their placeholders, no entry is left over, no visible string is written without
// Tr, and the language is chosen as docs/translations.md says. Reads the source tree (SKYLAUNCHER_SOURCE_DIR). Called from
// launcher_test.cpp's main; returns the number of failures.
#include "launcher/i18n.h"
#include "launcher/env.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    int g_failures = 0;

#define I18N_EXPECT(condition) \
    do { if (!(condition)) { fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition); g_failures++; } } while (0)

    void Fail(const std::string& message)
    {
        fprintf(stderr, "FAIL %s\n", message.c_str());
        g_failures++;
    }

    std::string Read(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }

    bool IsIdentifier(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

    // The C++ string literal at `at` (after any spaces and comments), with the literals that follow it joined as the compiler
    // does. `at` ends after the last one. False when there is none.
    bool ReadLiterals(const std::string& text, size_t& at, std::string& out)
    {
        out.clear();
        bool any = false;
        for (;;)
        {
            size_t p = at;
            for (;;)
            {
                while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p])))
                    p++;
                if (text.compare(p, 2, "//") == 0)
                    p = text.find('\n', p) == std::string::npos ? text.size() : text.find('\n', p);
                else if (text.compare(p, 2, "/*") == 0)
                    p = text.find("*/", p) == std::string::npos ? text.size() : text.find("*/", p) + 2;
                else
                    break;
            }
            if (p >= text.size() || text[p] != '"')
                return any;
            for (p++; p < text.size() && text[p] != '"'; p++)
            {
                if (text[p] != '\\')
                {
                    out += text[p];
                    continue;
                }
                char c = text[++p];
                if (c == 'n') out += '\n';
                else if (c == 't') out += '\t';
                else if (c == 'x')
                {
                    int value = 0;
                    while (p + 1 < text.size() && std::isxdigit(static_cast<unsigned char>(text[p + 1])))
                        value = value * 16 + (std::isdigit(static_cast<unsigned char>(text[p + 1])) ? text[p + 1] - '0' : (std::tolower(text[p + 1]) - 'a' + 10)), p++;
                    out += char(value);
                }
                else out += c;
            }
            at = p + 1;
            any = true;
        }
    }

    // The files whose strings the launcher translates: all of them.
    std::vector<fs::path> SourceFiles(const fs::path& root)
    {
        std::vector<fs::path> files;
        for (const fs::path& base : { root / "src" / "launcher", root / "src" / "main.cpp" })
        {
            std::vector<fs::path> found;
            if (fs::is_directory(base))
                for (const auto& entry : fs::recursive_directory_iterator(base))
                    found.push_back(entry.path());
            else
                found.push_back(base);
            for (const fs::path& file : found)
            {
                std::string name = file.filename().string(), extension = file.extension().string();
                if ((extension == ".cpp" || extension == ".h") && name != "i18n.h" && name != "i18n.cpp")
                    files.push_back(file);
            }
        }
        return files;
    }

    // Every string written as Tr("..."), Tf("...") or N_("..."): the keys.
    std::set<std::string> TranslatedKeys(const std::vector<fs::path>& files)
    {
        std::set<std::string> keys;
        for (const fs::path& file : files)
        {
            std::string text = Read(file);
            for (size_t i = 0; i + 2 < text.size(); i++)
            {
                if (IsIdentifier(i > 0 ? text[i - 1] : ' '))
                    continue;
                size_t length = text.compare(i, 3, "Tr(") == 0 || text.compare(i, 3, "Tf(") == 0 ? 3 : text.compare(i, 3, "N_(") == 0 ? 3 : 0;
                if (length == 0)
                    continue;
                size_t at = i + length;
                std::string key;
                if (ReadLiterals(text, at, key) && !key.empty())
                    keys.insert(key);
            }
        }
        return keys;
    }

    // Calls that put their first argument on the screen: a literal there that doesn't go through Tr is untranslated.
    void CheckNoRawText(const std::vector<fs::path>& files)
    {
        static const char* const Calls[] = { "PillButton(", "Title(", "BoldText(", "Toast(", "Chip(", "HoldButton(", "SettingRow(",
            "ImGui::Button(", "ImGui::TextUnformatted(", "ImGui::Selectable(", "ImGui::BeginCombo(", "ImGui::TextWrapped(", "ImGui::SetTooltip(",
            "ImGui::Text(" };
        for (const fs::path& file : files)
        {
            std::string text = Read(file);
            for (const char* call : Calls)
            {
                std::string name = call;
                for (size_t at = text.find(name); at != std::string::npos; at = text.find(name, at + 1))
                {
                    if (at > 0 && (IsIdentifier(text[at - 1]) || (text[at - 1] == ':' && name.find("::") == std::string::npos)))
                        continue;
                    size_t p = at + name.size();
                    // BeginCombo's first argument is its id; the shown text is the second.
                    if (name == "ImGui::BeginCombo(")
                    {
                        size_t comma = text.find(',', p);
                        if (comma == std::string::npos)
                            continue;
                        p = comma + 1;
                    }
                    std::string literal;
                    size_t end = p;
                    if (!ReadLiterals(text, end, literal) || literal.rfind("##", 0) == 0 || literal == "SKYLAUNCHER" || literal == "Skylauncher" || literal == "Arkeyan Launcher" || literal == "ARKEYAN LAUNCHER" || literal == "%s")
                        continue;
                    bool letters = false;
                    for (char c : literal)
                        letters = letters || std::isalpha(static_cast<unsigned char>(c));
                    if (letters)
                        Fail(file.filename().string() + ": " + name + "\"" + literal.substr(0, 50) + "\" is not translated (wrap it in Tr)");
                }
            }
        }
    }

    // The placeholders of a format string: for each argument, its format spec. {} count from 0 in order; braces around a name are text.
    std::map<int, std::string> Placeholders(const std::string& text, bool& valid)
    {
        std::map<int, std::string> found;
        int next = 0;
        valid = true;
        for (size_t i = 0; i < text.size(); i++)
        {
            if (text[i] == '{' && i + 1 < text.size() && text[i + 1] == '{') { i++; continue; }
            if (text[i] == '}' && i + 1 < text.size() && text[i + 1] == '}') { i++; continue; }
            if (text[i] == '}') { valid = false; continue; }
            if (text[i] != '{')
                continue;
            size_t close = text.find('}', i);
            if (close == std::string::npos) { valid = false; return found; }
            std::string field = text.substr(i + 1, close - i - 1), spec;
            if (size_t colon = field.find(':'); colon != std::string::npos)
            {
                spec = field.substr(colon);
                field.resize(colon);
            }
            i = close;
            if (field.find_first_not_of("0123456789") != std::string::npos)
                continue; // braces in a text that is never formatted ("{fmt}" in the credits)
            found[field.empty() ? next++ : std::atoi(field.c_str())] = spec;
        }
        return found;
    }

    // SKY_LAUNCHER_TEST_I18N_ALL lists every missing string (a new language starts from that list); SKY_LAUNCHER_TEST_I18N_DUMP=<file>
    // writes every string of the code in the language files' format, the English with an empty translation.
    void CheckLanguage(const fs::path& file, const std::set<std::string>& keys)
    {
        const int MaxListed = skyenv::Get("SKY_LAUNCHER_TEST_I18N_ALL") != nullptr ? 100000 : 25;
        i18n::Catalog catalog;
        std::string name = file.filename().string();
        if (!i18n::ParseCatalog(Read(file), catalog))
        {
            Fail(name + ": a line has no tab between the English and the translation");
            return;
        }
        if (catalog.code != file.stem().string() || catalog.name.empty())
            Fail(name + ": needs @code " + file.stem().string() + " and @name");
        std::map<std::string, std::string> entries;
        for (const auto& [english, translation] : catalog.entries)
        {
            if (!entries.emplace(english, translation).second)
                Fail(name + ": twice: " + english.substr(0, 60));
            if (translation.empty())
                Fail(name + ": empty translation: " + english.substr(0, 60));
        }
        int missing = 0;
        for (const std::string& key : keys)
        {
            auto it = entries.find(key);
            if (it == entries.end())
            {
                if (missing++ < MaxListed)
                    Fail(name + ": missing: " + key.substr(0, 80));
                continue;
            }
            bool englishValid, translatedValid;
            std::map<int, std::string> english = Placeholders(key, englishValid), translated = Placeholders(it->second, translatedValid);
            if (!translatedValid || english != translated)
                Fail(name + ": the placeholders differ from the English: " + key.substr(0, 60));
        }
        if (missing > MaxListed)
            Fail(name + ": " + std::to_string(missing) + " strings missing in all");
        for (const auto& [english, translation] : entries)
            if (!keys.count(english))
                Fail(name + ": no such string in the code (changed or removed?): " + english.substr(0, 60));
        printf("Language %s: %zu strings, %zu in the code\n", catalog.code.c_str(), entries.size(), keys.size());
    }
}

int I18nChecks()
{
    const fs::path root = SKYLAUNCHER_SOURCE_DIR;
    const std::vector<fs::path> sources = SourceFiles(root);
    I18N_EXPECT(sources.size() > 20);
    const std::set<std::string> keys = TranslatedKeys(sources);
    I18N_EXPECT(keys.size() > 300);
    CheckNoRawText(sources);
    if (const char* dump = skyenv::Get("SKY_LAUNCHER_TEST_I18N_DUMP"))
    {
        std::ofstream out(dump, std::ios::binary | std::ios::trunc);
        for (const std::string& key : keys)
        {
            for (char c : key)
                out << (c == '\n' ? "\\n" : c == '\t' ? "\\t" : c == '\\' ? "\\\\" : std::string(1, c));
            out << '\t' << '\n';
        }
    }

    // Each file of resources/lang is built in, and covers every string.
    int files = 0;
    for (const auto& entry : fs::directory_iterator(root / "resources" / "lang"))
        if (entry.path().extension() == ".txt")
        {
            files++;
            CheckLanguage(entry.path(), keys);
            I18N_EXPECT(i18n::Has(entry.path().stem().string()));
        }
    I18N_EXPECT(files >= 1 && i18n::Languages().size() == size_t(files) + 1); // and English, which is the code itself

    // The text follows the language, and a string a language lacks is English.
    i18n::SetLanguage("fr");
    I18N_EXPECT(i18n::Current() == "fr" && std::string(i18n::Tr("Cancel")) != "Cancel");
    I18N_EXPECT(std::string(i18n::Tr("A string no language has")) == "A string no language has");
    // A failure toast is known in any language (it gets the red ring and the cross).
    I18N_EXPECT(i18n::StartsLike(i18n::Tr("Couldn't save the settings"), "Couldn't"));
    I18N_EXPECT(i18n::StartsLike("Couldn't do it", "Couldn't") && !i18n::StartsLike(i18n::Tr("Log copied"), "Couldn't"));
    I18N_EXPECT(i18n::Tf("{} min ago", 5) != "5 min ago");
    i18n::SetLanguage("zz"); // a language without a file: English
    I18N_EXPECT(i18n::Current() == "en" && std::string(i18n::Tr("Cancel")) == "Cancel");
    I18N_EXPECT(i18n::Tf("about {} min left", 3) == "about 3 min left");
    i18n::SetLanguage("en");

    // The choice: a code is that language, nothing is the system's.
    const std::vector<std::string> system = { "ja", "fr", "en" };
    I18N_EXPECT(i18n::Resolve("fr", system) == "fr");
    I18N_EXPECT(i18n::Resolve("en", system) == "en");
    I18N_EXPECT(i18n::Resolve("", system) == "fr"); // the first system language the launcher has
    I18N_EXPECT(i18n::Resolve("", { "ja" }) == "en");
    I18N_EXPECT(i18n::Resolve("xx", system) == "en");

    // The catalog format: escapes, comments, the header.
    i18n::Catalog catalog;
    I18N_EXPECT(i18n::ParseCatalog("# note\n@code xx\n@name Test\n\nA\\nB\tC\\tD\n", catalog));
    I18N_EXPECT(catalog.code == "xx" && catalog.name == "Test" && catalog.entries.size() == 1);
    I18N_EXPECT(catalog.entries[0].first == "A\nB" && catalog.entries[0].second == "C\tD");
    I18N_EXPECT(!i18n::ParseCatalog("no tab here\n", catalog));

    printf("Languages: %d failures\n", g_failures);
    return g_failures;
}
