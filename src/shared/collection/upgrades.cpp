// See upgrades.h. The file's format (written by art/game_text.cpp):
//   { "format": 1, "game": "giants", "toys": { "16": {
//       "paths": [ { "name": { "en": ..., "fr": ... }, "description": { ... } }, { ... } ],
//       "upgrades": [ { "bit": 2, "path": 0, "price": 500, "name": { "en": ..., "fr": ... }, "description": { ... } }, ... ] } } }
#include "upgrades.h"
#include "datadir.h"
#include "json.h"
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>

namespace collection
{
    namespace
    {
        // Toy types a game defines as another's alternate ("Portal - Define Alt Character", Giants' Init_Setup.bld [V]):
        // they share their base toy's upgrades (the Giants archives of 28, 404-430 list the base toy's names [V]).
        uint16_t BaseToy(uint16_t type)
        {
            switch (type)
            {
            case 28: case 416: return 16;
            case 404: return 4;
            case 419: return 19;
            case 430: return 30;
            default: return type;
            }
        }

        // The prices both games use, in this order: the basic upgrades, a path's three, the Soul Gem, the Wow Pow [V]
        // (SSA's upgrade records, Giants' Minion_UpgradeCosts).
        constexpr int Prices[] = { 500, 700, 900, 1200, 1700, 2200, 3000, 4000, 5000 };

        UpgradeTable Plain(const std::string& language)
        {
            bool fr = language == "fr";
            UpgradeTable table;
            auto add = [&](int bit, UpgradePath path, int price, std::string name) {
                table.upgrades.push_back({ bit, path, price, std::move(name), {} });
            };
            for (int i = 0; i < 4; i++)
                add(2 + i, UpgradePath::Basic, Prices[i], (fr ? "Amélioration " : "Upgrade ") + std::to_string(i + 1));
            for (int path = 1; path <= 2; path++)
                for (int i = 0; i < 3; i++)
                    add(6 + i, UpgradePath(path), Prices[4 + i],
                        (fr ? "Voie " : "Path ") + std::to_string(path) + (fr ? ", amélioration " : ", upgrade ") + std::to_string(i + 1));
            add(UpgradeSoulGemBit, UpgradePath::SoulGem, Prices[7], fr ? "Gemme d'âme" : "Soul Gem");
            for (int path = 0; path < 2; path++)
                table.pathNames[path] = (fr ? "Voie " : "Path ") + std::to_string(path + 1);
            return table;
        }

        std::string Text(const Json& texts, const std::string& language)
        {
            const Json& wanted = texts[language];
            return wanted.IsString() && !wanted.AsString().empty() ? wanted.AsString() : texts["en"].AsString();
        }

        // The parsed file of a game, read again when it changes.
        std::shared_ptr<const Json> Load(const std::string& game)
        {
            static std::mutex mutex;
            static std::map<std::string, std::pair<std::filesystem::file_time_type, std::shared_ptr<const Json>>> cache;
            std::lock_guard lock(mutex);
            std::filesystem::path path = UpgradeFile(game);
            std::error_code error;
            if (path.empty() || !std::filesystem::is_regular_file(path, error))
            {
                cache.erase(game);
                return nullptr;
            }
            auto time = std::filesystem::last_write_time(path, error);
            auto it = cache.find(game);
            if (it == cache.end() || it->second.first != time)
            {
                std::ifstream in(path, std::ios::binary);
                std::stringstream text;
                text << in.rdbuf();
                Json json;
                std::string parseError;
                if (!Json::Parse(text.str(), json, parseError) || json["format"].AsInt() != 1)
                    return nullptr;
                it = cache.insert_or_assign(game, std::pair{ time, std::make_shared<const Json>(std::move(json)) }).first;
            }
            return it->second.second;
        }
    }

    std::filesystem::path UpgradeFile(std::string_view game)
    {
        std::filesystem::path data = datadir::Game(game);
        return data.empty() ? data : data / "upgrades.json";
    }

    UpgradeTable Upgrades(const Toy& toy, const std::string& game, const std::string& language)
    {
        std::shared_ptr<const Json> file = Load(game);
        const Json* entry = nullptr;
        if (file != nullptr)
            for (uint16_t type : { toy.type, BaseToy(toy.type) })
                if (const Json& found = (*file)["toys"][std::to_string(type)]; found.IsObject() && entry == nullptr)
                    entry = &found;
        if (entry == nullptr)
            return Plain(language);
        UpgradeTable table;
        table.fromDisc = true;
        for (const Json& item : (*entry)["upgrades"].Items())
            table.upgrades.push_back({ int(item["bit"].AsInt()), UpgradePath(item["path"].AsInt()), int(item["price"].AsInt()),
                Text(item["name"], language), Text(item["description"], language) });
        const std::vector<Json>& paths = (*entry)["paths"].Items();
        for (size_t i = 0; i < 2 && i < paths.size(); i++)
        {
            table.pathNames[i] = Text(paths[i]["name"], language);
            table.pathDescriptions[i] = Text(paths[i]["description"], language);
        }
        return table;
    }
}
