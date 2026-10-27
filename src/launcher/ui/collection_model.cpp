#include "launcher/ui/collection_model.h"
#include "launcher/launcher.h"
#include "shared/dir_list.h"
#include "shared/collection/datadir.h"
#include "shared/collection/json.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fmt/format.h>
#include <map>

namespace launcher_ui
{
    namespace fs = std::filesystem;

    std::string SessionFile(const collection::Game& game)
    {
        if (FindGame(game.id) == nullptr) // no runtime of ours yet
            return {};
        if (const char* saves = getenv("SSA_SAVE_DIR"))
            return std::string(saves) + "/portal.txt";
        // The game's folder in the data root, as host/portal.cpp asks for it.
        std::filesystem::path folder = datadir::Game(game.id);
        return folder.empty() ? std::string() : datadir::Text(folder / "portal.txt");
    }

    // Swap Force's portal has six slots: a player's swapper takes two (player 1 slots 0 and 1, player 2 slots 2 and 3), the
    // magic item the fifth, the adventure pack the sixth. Every other game: one slot per seat, in order.
    static bool TwoSlotSeats(const collection::Game& game) { return game.id == "swapforce"; }

    int SeatFirstSlot(const collection::Game& game, size_t seat)
    {
        if (!TwoSlotSeats(game))
            return int(seat);
        return seat < 2 ? int(seat) * 2 : int(seat) + 2;
    }

    int SlotSeat(const collection::Game& game, int slot)
    {
        if (!TwoSlotSeats(game))
            return slot;
        return slot < 4 ? slot / 2 : slot - 2;
    }

    bool SeatAccepts(const collection::Seat& seat, const collection::Figure& figure)
    {
        return figure.toy == nullptr || std::find(seat.accepts.begin(), seat.accepts.end(), figure.toy->category) != seat.accepts.end();
    }

    std::vector<std::string> ReadSeats(const collection::Game& game, const std::string& file)
    {
        std::vector<std::string> seats(game.seats.size());
        size_t size = 0;
        char* text = file.empty() ? nullptr : static_cast<char*>(SDL_LoadFile(file.c_str(), &size));
        if (text == nullptr)
            return seats;
        std::string lines(text, size);
        SDL_free(text);

        struct Placed { int slot; std::string path; collection::Figure figure; };
        std::vector<Placed> placed;
        for (size_t begin = 0; begin < lines.size();)
        {
            size_t end = std::min(lines.find('\n', begin), lines.size());
            std::string line = lines.substr(begin, end - begin);
            begin = end + 1;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            size_t space = line.find(' ');
            if (space == std::string::npos)
                continue;
            Placed p{ atoi(line.c_str()), collection::MigratedPath(line.substr(space + 1)), {} };
            if (collection::Read(p.path, p.figure))
                placed.push_back(std::move(p));
        }
        // A swapper is one seat: its bottom (the top's partner, when the top is placed too) is not a seat of its own.
        for (size_t i = 0; i < placed.size();)
        {
            const collection::Figure& half = placed[i].figure;
            bool follows = false;
            if (half.IsSwapBottom() && !half.partner.empty())
                for (const Placed& other : placed)
                    follows = follows || (other.figure.IsSwapTop() && other.figure.file == half.partner);
            if (follows)
                placed.erase(placed.begin() + i);
            else
                i++;
        }
        for (Placed& p : placed)
            p.slot = SlotSeat(game, p.slot);
        std::vector<bool> done(placed.size());
        for (size_t i = 0; i < placed.size(); i++)
        {
            int slot = placed[i].slot;
            if (slot >= 0 && size_t(slot) < seats.size() && seats[slot].empty() && SeatAccepts(game.seats[slot], placed[i].figure))
            {
                seats[slot] = placed[i].path;
                done[i] = true;
            }
        }
        for (size_t i = 0; i < placed.size(); i++)
        {
            if (done[i])
                continue;
            for (size_t s = 0; s < seats.size(); s++)
            {
                if (seats[s].empty() && SeatAccepts(game.seats[s], placed[i].figure))
                {
                    seats[s] = placed[i].path;
                    break;
                }
            }
        }
        return seats;
    }

    bool WriteSeats(const collection::Game& game, const std::string& file, const std::vector<std::string>& seats)
    {
        if (file.empty())
            return false;
        std::string text;
        for (size_t i = 0; i < seats.size(); i++)
        {
            if (seats[i].empty())
                continue;
            int slot = SeatFirstSlot(game, i);
            text += fmt::format("{} {}\n", slot, seats[i]);
            // A swapper's bottom goes in the slot after its top.
            collection::Figure top;
            if (TwoSlotSeats(game) && i < 2 && collection::Read(seats[i], top) && top.IsSwapTop() && !top.partner.empty())
                text += fmt::format("{} {}\n", slot + 1, collection::PathOf(top.partner));
        }
        std::error_code error;
        fs::create_directories(fs::path(file).parent_path(), error);
        return SDL_SaveFile(file.c_str(), text.data(), text.size());
    }

    std::string ShortGameName(const std::string& game)
    {
        const collection::Game* found = collection::Catalogue::Get().FindGame(game);
        if (found == nullptr)
            return game;
        std::string name = found->name;
        if (name.rfind("Skylanders: ", 0) == 0)
            name = name.substr(12);
        return name;
    }

    std::vector<std::string> MoveLauncherSettings(const collection::Game& game, const std::string& legacy,
        const std::vector<std::string>& favourites, const std::vector<std::string>& place)
    {
        // A file name of the old folder: its copy in the collection, else a dump of that name in the collection.
        auto copyOf = [&](const std::string& name) {
            std::error_code error;
            std::string full = name.find_first_of("/\\") != std::string::npos ? name : legacy + "/" + name;
            if (!legacy.empty() || full == name)
            {
                std::string copy = collection::MigratedPath(full);
                if (copy != full && fs::exists(copy, error))
                    return copy;
            }
            if (full == name && fs::exists(name, error))
                return name;
            std::string path = collection::PathOf(name);
            return fs::exists(path, error) ? path : std::string();
        };
        for (const std::string& name : favourites)
            if (std::string path = copyOf(name); !path.empty())
                collection::SetFavourite(path, true);

        std::vector<std::string> seats(game.seats.size());
        std::vector<std::string> current = ReadSeats(game, SessionFile(game));
        if (std::any_of(current.begin(), current.end(), [](const std::string& s) { return !s.empty(); }))
            return {};
        bool any = false;
        for (const std::string& name : place)
        {
            std::string path = name.rfind("new:", 0) == 0 ? std::string() : copyOf(name);
            collection::Figure figure;
            if (path.empty() || !collection::Read(path, figure))
                continue;
            for (size_t s = 0; s < seats.size(); s++)
            {
                if (seats[s].empty() && SeatAccepts(game.seats[s], figure))
                {
                    seats[s] = path;
                    any = true;
                    break;
                }
            }
        }
        return any ? seats : std::vector<std::string>{};
    }

    bool NameUnknownToy(uint16_t type, const std::string& name, collection::Category category, const std::string& game)
    {
        using collection::Json;
        std::string path = collection::Folder() + "/catalogue.json";
        Json root = Json::Object();
        size_t size = 0;
        if (char* data = static_cast<char*>(SDL_LoadFile(path.c_str(), &size)))
        {
            std::string error;
            Json parsed;
            if (Json::Parse(std::string(data, size), parsed, error) && parsed.IsObject())
                root = parsed;
            SDL_free(data);
        }
        Json toys = Json::Array();
        for (const Json& toy : root["toys"].Items())
            if (toy["type"].AsInt(-1) != type)
                toys.Push(toy);
        Json toy = Json::Object();
        toy.Set("type", int(type));
        toy.Set("name", name);
        toy.Set("category", collection::CategoryId(category));
        toy.Set("game", game);
        toys.Push(toy);
        root.Set("format", 1);
        root.Set("toys", toys);
        std::string text = root.Dump();
        std::error_code error;
        fs::create_directories(collection::Folder(), error);
        return SDL_SaveFile(path.c_str(), text.data(), text.size());
    }

    // ---- CollectionModel ------------------------------------------------------------------------------------------

    const collection::Game* CollectionModel::Game() const
    {
        return collection::Catalogue::Get().FindGame(gameId);
    }

    void CollectionModel::Load()
    {
        figures = collection::List();
        const collection::Game* game = Game();
        seats = game != nullptr ? ReadSeats(*game, SessionFile(*game)) : std::vector<std::string>{};
        m_signature = Signature();
    }

    std::string CollectionModel::Signature() const
    {
        // Names, sizes and times of the folder's files (dumps, locks, collection.json), the session file's time, and
        // whether each lock's process still runs: a game that quits leaves its locks behind (docs/collection.md).
        std::string signature;
        std::error_code error;
        for (const fs::directory_entry& entry : ListDirectory(collection::Folder(), error))
        {
            if (!entry.is_regular_file(error))
                continue;
            std::string name = entry.path().filename().string();
            signature += fmt::format("{}|{}|{};", name, entry.file_size(error), entry.last_write_time(error).time_since_epoch().count());
            if (name.size() > 5 && name.ends_with(".lock"))
                signature += collection::GetLock(entry.path().string().substr(0, entry.path().string().size() - 5)).Busy() ? "busy;" : "free;";
        }
        if (const collection::Game* game = Game())
        {
            std::string session = SessionFile(*game);
            if (!session.empty() && fs::exists(session, error))
                signature += fmt::format("session|{};", fs::last_write_time(session, error).time_since_epoch().count());
        }
        return signature;
    }

    bool CollectionModel::Poll(uint64_t now, uint64_t interval)
    {
        if (now < m_nextPoll)
            return false;
        m_nextPoll = now + interval;
        if (Signature() == m_signature)
            return false;
        Load();
        return true;
    }

    const collection::Figure* CollectionModel::Find(const std::string& path) const
    {
        for (const collection::Figure& figure : figures)
            if (figure.path == path)
                return &figure;
        return nullptr;
    }

    std::string CollectionModel::RunningGame() const
    {
        for (const collection::Figure& figure : figures)
            if (figure.lock.Busy() && figure.lock.game != "launcher")
                return figure.lock.game;
        return {};
    }

    bool CollectionModel::CanEditSeats() const
    {
        const collection::Game* game = Game();
        return game != nullptr && !SessionFile(*game).empty() && RunningGame() != gameId;
    }

    bool CollectionModel::SetSeat(size_t seat, const std::string& path)
    {
        if (!CanEditSeats() || seat >= seats.size())
            return false;
        if (!path.empty())
            for (std::string& other : seats)
                if (other == path)
                    other.clear();
        seats[seat] = path;
        if (!WriteSeats(*Game(), SessionFile(*Game()), seats))
            return false;
        m_signature = Signature();
        return true;
    }

    int CollectionModel::SeatOf(const std::string& path) const
    {
        for (size_t i = 0; i < seats.size(); i++)
            if (!path.empty() && seats[i] == path)
                return int(i);
        return -1;
    }

    bool CollectionModel::Delete(const std::string& path, std::string* error)
    {
        if (!collection::Delete(path, "deleted", error))
            return false;
        if (int seat = SeatOf(path); seat >= 0)
            SetSeat(size_t(seat), "");
        Load();
        return true;
    }

    bool CollectionModel::DeleteCard(const std::string& path, std::string* error)
    {
        const collection::Figure* figure = Find(path);
        std::string other = figure != nullptr && !figure->partner.empty() ? collection::PathOf(figure->partner) : std::string();
        if (!Delete(path, error))
            return false;
        // Deleting the first half cut the link; the other half goes too, and its seat.
        return other.empty() || Find(other) == nullptr || Delete(other, error);
    }

    std::vector<collection::Section> CollectionModel::Shelf(const ShelfFilter& filter, std::vector<collection::Figure>& shown, int* hidden) const
    {
        const collection::Game* game = Game();
        const collection::Catalogue& catalogue = collection::Catalogue::Get();
        auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(tolower(c)); });
            return text;
        };
        std::string text = lower(std::string(Trimmed(filter.text)));
        std::vector<std::string> seated = OnPortal();
        shown.clear();
        if (hidden != nullptr)
            *hidden = 0;
        // A swapper is one card: a half the filters let through brings the other half with it.
        std::vector<char> passes(figures.size(), 0), hiddenStarter(figures.size(), 0);
        for (size_t i = 0; i < figures.size(); i++)
        {
            const collection::Figure& figure = figures[i];
            if (std::find(seated.begin(), seated.end(), figure.path) == seated.end() && collection::UntouchedStarter(figure))
            {
                hiddenStarter[i] = 1;
                if (hidden != nullptr)
                    (*hidden)++; // the game made it on its own: not the player's (Manage > Tidy)
                continue;
            }
            const collection::Toy* toy = figure.toy;
            if (filter.seat >= 0)
            {
                if (game == nullptr || size_t(filter.seat) >= game->seats.size() || !SeatAccepts(game->seats[filter.seat], figure) ||
                    (toy != nullptr && !catalogue.Accepts(game->id, figure.type)))
                    continue;
            }
            if ((filter.element >= 0 && (toy == nullptr || int(toy->element) != filter.element)) ||
                (filter.category >= 0 && (toy == nullptr || int(toy->category) != filter.category)) ||
                (!filter.game.empty() && (toy == nullptr || toy->game != filter.game)))
                continue;
            if (!text.empty())
            {
                std::string words = figure.label + "\n" + figures::NicknameUtf8(figure.stats) + "\n" + figure.DisplayName() + "\n" +
                    (toy != nullptr ? toy->name : std::string()) + "\n" + figure.file;
                if (lower(words).find(text) == std::string::npos)
                    continue;
            }
            passes[i] = 1;
        }
        std::map<std::string, size_t> byFile;
        for (size_t i = 0; i < figures.size(); i++)
            if (!figures[i].partner.empty())
                byFile[figures[i].file] = i;
        for (size_t i = 0; i < figures.size(); i++)
        {
            bool partnerPasses = false;
            if (!figures[i].partner.empty())
                if (auto other = byFile.find(figures[i].partner); other != byFile.end())
                    partnerPasses = passes[other->second] != 0;
            if (!hiddenStarter[i] && (passes[i] || partnerPasses))
                shown.push_back(figures[i]);
        }
        return collection::Group(shown, collection::GroupBy::Smart, collection::OrderBy::Recent, gameId);
    }

    std::vector<std::string> CollectionModel::OnPortal() const
    {
        std::vector<std::string> paths;
        for (const std::string& seat : seats)
            if (!seat.empty())
                paths.push_back(seat);
        return paths;
    }

    std::string Trimmed(std::string_view text)
    {
        size_t first = text.find_first_not_of(" \t"), last = text.find_last_not_of(" \t");
        return first == std::string_view::npos ? std::string() : std::string(text.substr(first, last - first + 1));
    }

    figures::FigureStats MergeEditedStats(const figures::FigureStats& now, const figures::FigureStats& base, const figures::FigureStats& edited,
        const std::string& baseNickname, const std::string& nickname)
    {
        figures::FigureStats stats = now;
        if (edited.experience != base.experience)
            stats.experience = edited.experience;
        if (edited.giantsExperience != base.giantsExperience)
            stats.giantsExperience = edited.giantsExperience;
        if (edited.hat != base.hat)
            stats.hat = edited.hat;
        if (edited.gold != base.gold)
            stats.gold = edited.gold;
        if (edited.heroPoints != base.heroPoints)
            stats.heroPoints = edited.heroPoints;
        if (edited.upgrades != base.upgrades)
            stats.upgrades = edited.upgrades;
        if (edited.wowPow != base.wowPow)
            stats.wowPow = edited.wowPow;
        if (nickname != baseNickname)
            figures::SetNicknameUtf8(stats, nickname);
        return stats;
    }
}
