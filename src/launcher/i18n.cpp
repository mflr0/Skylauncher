#include "launcher/i18n.h"
#include <atomic>
#include <unordered_map>

// Generated from resources/lang/*.txt (cmake/embed_languages.cmake): the code of each file and its bytes.
struct EmbeddedLanguage
{
    const char* code;
    const unsigned char* data;
    size_t size;
};
extern const EmbeddedLanguage g_embeddedLanguages[];
extern const size_t g_embeddedLanguageCount;

namespace i18n
{
    namespace
    {
        struct State
        {
            std::vector<Language> languages{ { "en", "English" } };
            std::string current = "en";
            using Table = std::unordered_map<std::string, std::string>;
            std::atomic<const Table*> table{ nullptr }; // the current language's entries; Tr is also called from worker threads
            std::unordered_map<std::string, Table> tables; // never changed once built, so the pointer above stays valid
        };

        State& Get()
        {
            static State state;
            static const bool loaded = [] {
                for (size_t i = 0; i < g_embeddedLanguageCount; i++)
                {
                    const EmbeddedLanguage& file = g_embeddedLanguages[i];
                    Catalog catalog;
                    if (!ParseCatalog(std::string_view(reinterpret_cast<const char*>(file.data), file.size), catalog) || catalog.code.empty())
                        continue;
                    state.languages.push_back({ catalog.code, catalog.name.empty() ? catalog.code : catalog.name });
                    auto& table = state.tables[catalog.code];
                    for (auto& [english, translation] : catalog.entries)
                        table[english] = translation;
                }
                return true;
            }();
            (void)loaded;
            return state;
        }

        // "\n", "\t" and "\\" in a catalog line.
        std::string Unescape(std::string_view text)
        {
            std::string out;
            for (size_t i = 0; i < text.size(); i++)
            {
                if (text[i] == '\\' && i + 1 < text.size())
                {
                    char c = text[++i];
                    out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
                }
                else
                    out += text[i];
            }
            return out;
        }

        std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\r'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r'))
                s.remove_suffix(1);
            return s;
        }
    }

    // A catalog: "@code fr" and "@name Français" lines, '#' comments, then one "English<TAB>translation" per line.
    bool ParseCatalog(std::string_view text, Catalog& out)
    {
        size_t at = 0;
        while (at < text.size())
        {
            size_t end = text.find('\n', at);
            if (end == std::string_view::npos)
                end = text.size();
            std::string_view line = text.substr(at, end - at);
            at = end + 1;
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (line.empty() || line.front() == '#')
                continue;
            if (line.front() == '@')
            {
                size_t space = line.find(' ');
                std::string_view key = line.substr(1, space == std::string_view::npos ? line.npos : space - 1);
                std::string value(space == std::string_view::npos ? std::string_view() : Trim(line.substr(space)));
                if (key == "code")
                    out.code = value;
                else if (key == "name")
                    out.name = value;
                continue;
            }
            size_t tab = line.find('\t');
            if (tab == std::string_view::npos)
                return false;
            out.entries.emplace_back(Unescape(line.substr(0, tab)), Unescape(line.substr(tab + 1)));
        }
        return true;
    }

    const std::vector<Language>& Languages() { return Get().languages; }

    bool Has(std::string_view code)
    {
        for (const Language& language : Get().languages)
            if (language.code == code)
                return true;
        return false;
    }

    void SetLanguage(std::string_view code)
    {
        State& s = Get();
        if (code == s.current)
            return;
        auto it = s.tables.find(std::string(code));
        s.current = it == s.tables.end() ? "en" : std::string(code);
        s.table = it == s.tables.end() ? nullptr : &it->second;
    }

    const std::string& Current() { return Get().current; }

    const char* Tr(const char* english)
    {
        const State::Table* table = Get().table;
        if (table == nullptr)
            return english;
        auto it = table->find(english);
        return it == table->end() || it->second.empty() ? english : it->second.c_str();
    }

    std::string Resolve(std::string_view setting, std::string_view gameLanguage, const std::vector<std::string>& system)
    {
        if (setting == "game")
            return Has(gameLanguage) ? std::string(gameLanguage) : "en";
        if (!setting.empty())
            return Has(setting) ? std::string(setting) : "en";
        for (const std::string& code : system)
            if (Has(code))
                return code;
        return "en";
    }
}
