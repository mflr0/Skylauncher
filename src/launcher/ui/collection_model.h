#pragma once

// The Collection page's data, with no UI: the shared collection (SSARecomp's library, src/shared/collection), the seats
// of the game's next session, and what changes on disk while a game runs (docs/collection_ui.md).
#include "shared/collection/collection.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace launcher_ui
{
    // The file the game's emulated portal starts from: portal.txt in its save folder (SSA_SAVE_DIR, else its runtime's
    // data folder), "<slot> <path>" lines, rewritten by the game at every change. Empty for a game with no runtime.
    std::string SessionFile(const collection::Game& game);

    // Whether a figure can sit on the seat. Unknown toys fit every seat: the game decides.
    bool SeatAccepts(const collection::Seat& seat, const collection::Figure& figure);

    // One path per seat of `game` (empty: a free seat), from the session file: a figure goes to the seat of its slot when
    // it fits there, else to the first free seat that takes it, as the game's Toy Box does. Paths of the old figure
    // folder lead to their copies in the collection.
    std::vector<std::string> ReadSeats(const collection::Game& game, const std::string& file);
    // Seat i in slot i; Swap Force's six slots: player 1 on 0 and 1, player 2 on 2 and 3, the item on 4, the adventure pack on
    // 5, a swapper's bottom (the top's partner) in the slot after its top. False if the file cannot be written.
    bool WriteSeats(const collection::Game& game, const std::string& file, const std::vector<std::string>& seats);
    // The portal slot a seat starts at, and the seat of a slot, for the game.
    int SeatFirstSlot(const collection::Game& game, size_t seat);
    int SlotSeat(const collection::Game& game, int slot);

    // "Spyro's Adventure" for "ssa": the game's name without the series' prefix; the id itself if unknown.
    std::string ShortGameName(const std::string& game);

    // The launcher's own state from before the shared collection, moved once: favourites ([collection] favourites, file
    // names) become collection.json's, and the figures that started on the portal ([portal] place) the next session's
    // seats, unless the session file already names some. `legacy` is the old figure folder (LegacyFolder()).
    // Returns the seats to write, empty if nothing changes.
    std::vector<std::string> MoveLauncherSettings(const collection::Game& game, const std::string& legacy,
        const std::vector<std::string>& favourites, const std::vector<std::string>& place);

    // Names a toy the catalogue does not know: an entry in the collection's catalogue.json (by toy type), which the
    // catalogue adds at the next start, as the game's Toy Box does.
    bool NameUnknownToy(uint16_t type, const std::string& name, collection::Category category, const std::string& game);

    // A figure's name as typed, without the spaces around it: a name of spaces is no name (the toy's shows).
    std::string Trimmed(std::string_view text);

    // Edit stats' Save: the figure as it is `now` (a game may have played it since the editor opened on `base`), with
    // only the fields the player changed (`edited`, and the nickname from `baseNickname` to `nickname`).
    figures::FigureStats MergeEditedStats(const figures::FigureStats& now, const figures::FigureStats& base, const figures::FigureStats& edited,
        const std::string& baseNickname, const std::string& nickname);

    // The bar's filters on the shelf (docs/collection_ui.md); -1 or empty: all.
    struct ShelfFilter
    {
        int element = -1;      // a collection::Element
        std::string game;      // the game a toy came with ("giants")
        int category = -1;     // a collection::Category
        std::string text;      // in the name, the nickname, the toy's name or the file name, any case
        int seat = -1;         // choosing for this seat of the model's game: only what goes there and the game reads
    };

    class CollectionModel
    {
    public:
        std::vector<collection::Figure> figures; // List(): every dump of the collection
        std::string gameId = "ssa";               // the game whose seats, views and sections are shown
        std::vector<std::string> seats;           // per seat of Game()

        const collection::Game* Game() const;
        void Load();
        // Every `interval` ms: reloads when a dump, a lock, collection.json or the session file changed, or a lock's
        // process ended (the game quit). True when it reloaded.
        bool Poll(uint64_t now, uint64_t interval = 1500);
        const collection::Figure* Find(const std::string& path) const;
        // The game holding a figure right now ("ssa"), empty when none: its portal is live, the seats are read-only.
        std::string RunningGame() const;
        bool CanEditSeats() const;
        // Puts the figure on the seat (taking it off any other seat), or clears the seat with an empty path, and writes
        // the session file. False when the seats cannot be changed now.
        bool SetSeat(size_t seat, const std::string& path);
        int SeatOf(const std::string& path) const; // -1 if on no seat
        // Moves the figure to the trash (collection::Delete), takes it off its seat and reloads. False, with the reason,
        // when the trash refuses it: then nothing has changed.
        bool Delete(const std::string& path, std::string* error);
        // A swapper's card: both halves go to the trash.
        bool DeleteCard(const std::string& path, std::string* error);
        std::vector<std::string> OnPortal() const; // the seated paths
        // The figures the page shows, into `shown`, cut by collection::Group's shelf for Game(): every figure once,
        // favourites first, then what the game took (newest first), then by element; unknown toys and the toys the game
        // does not read after (none of those when choosing for a seat). The untouched starters the game made on its own
        // are left out unless seated; `hidden` (optional) counts them.
        std::vector<collection::Section> Shelf(const ShelfFilter& filter, std::vector<collection::Figure>& shown, int* hidden = nullptr) const;

    private:
        std::string Signature() const;
        std::string m_signature;
        uint64_t m_nextPoll = 0;
    };
}
