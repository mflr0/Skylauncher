#pragma once

// The Collection page: the portal at the next start (the seats of the chosen game), the bar (search, Filters), the shelf (every figure once, favourites first), the figure sheet, Add a figure and Manage
// (docs/collection_ui.md). The figures are the shared collection's (SSARecomp's library); the
// look is the launcher's. Drawn by App's widgets (a friend), in collection_page.cpp, collection_figure.cpp,
// collection_add.cpp and collection_manage.cpp.
#include "launcher/ui/collection_model.h"
#include "launcher/ui/components/components.h"
#include "shared/figures/figure_icons.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace launcher_ui
{
    class App;

    // Small helpers the collection's files share.
    std::string PlayTime(uint32_t seconds);
    // When something happened, from an ISO 8601 UTC time ("Today 15:05", "28 Oct 2026"): the launcher's format helpers.
    std::string When(const std::string& iso);
    std::string ElementName(collection::Element element);
    std::string CategoryLabel(collection::Category category);   // the type's name in the launcher's language
    std::string KindLabel(collection::VariantKind kind);        // the version's name in the launcher's language
    bool French();                                              // the launcher shows French (the library's own French texts)
    unsigned ElementInk(collection::Element element); // 0xRRGGBB
    std::string Lower(std::string text);
    // A line of text at `at`, drawn without an item, cut to `room` with "...".
    void PutText(ImDrawList* draw, ImVec2 at, ui::TextStyle style, bool bold, unsigned color, const std::string& text, float room);
    // What scrolls inside an overlay is this tall: the window less the dialog's margins and `chrome` (its title, footer...),
    // between `least` and `most`.
    float OverlayBody(float chrome, float least, float most);
    // The footer of a view inside the overlay: Cancel, then the confirm button (primary), bottom right on the current line
    // (ui::ModalActions' layout; it closes the dialog, this does not). `confirm` null: Cancel alone.
    ui::ModalResult OverlayActions(const char* cancel, const char* confirm = nullptr, bool confirmDisabled = false);
    // "Doom Stone" for "Doom Stone (top)" or "(bottom)": a swapper's halves are named after it.
    std::string WithoutHalf(std::string name);

    // A figure's picture: the disc's portrait, or a generated icon (the page writes the initials over those).
    struct FigureIconTexture
    {
        SDL_Texture* texture = nullptr;
        bool generated = false;
        float width = 0, height = 0; // the picture's own size (an artwork is not square)
    };

    class CollectionPage
    {
    public:
        explicit CollectionPage(App& app) : m_app(app) {}
        void SelectGame(const std::string& id) { m_model.gameId = id; } // before the page is first opened
        ~CollectionPage() { ClearIcons(); }

        void Draw();
        // The pictures come from the disc once it is verified: drop the cached textures.
        void ClearIcons();
        // A dropped file or folder: figure dumps join the collection. False if it holds none (a disc image).
        bool Drop(const std::string& path);
        std::string Folder() const;
        // B / Escape belongs to the page: an overlay is open, a seat is being chosen for, or a search is typed.
        bool TakesBack() const { return m_overlay != Overlay::None || m_choosing >= 0 || m_search[0] != 0; }

        // SKY_LAUNCHER_TEST_SEATX: the X of every seat, pressed by mouse, keyboard and pad (App::TestSeatXScript).
        std::vector<std::string> TestSeats() const { return m_model.seats; }
        void TestSetSeats(const std::vector<std::string>& seats); // the seats as given, written to the session file
        void TestFocusSeat(size_t seat) { m_focusSeat = int(seat); }
        ImVec2 TestCrossAt(size_t seat) const { return seat < m_crossAt.size() ? m_crossAt[seat] : ImVec2(-1, -1); }

        // Screenshot test (SKY_LAUNCHER_TEST_COLLECTION): every screen, in a scratch collection.
        void Seed();
        void TestScreen(int step);
        static const char* TestShotName(int step);
        static bool TestPad(); // SKY_LAUNCHER_TEST_COLLECTION=pad: the shots show the page as a pad player sees it (the legend)
        static int TestShotCount();

    private:
        enum class Overlay { None, Figure, Add, Manage };

        void Open();
        void Reload();
        void Rebuild();
        void SelectPortalGame(const std::string& id);
        void DrawHeader(float width);
        void DrawSeats(float width);
        void DrawSeatMenu();
        void TakeOff(size_t seat);
        void Choose(int seat);  // the next card picked goes on this seat; -1: stop choosing
        void DrawBar();
        void DrawFilters(int active);
        void DrawTags();
        void ClearFilters();
        int FoundCount() const;
        void DrawList();
        bool DrawCard(const collection::Figure& figure, float width, bool dim);
        void DrawOverlay();
        void DrawFigurePage();
        void DrawFigureMenu(const collection::Figure& figure, bool writable);
        void DrawConfirm(const collection::Figure& figure);
        void DrawFigureFacts(const collection::Figure& figure);
        void DrawDetails(const collection::Figure& figure);
        void DrawEditStats(const collection::Figure& figure);
        void DrawChangeHalf();
        void ChangeHalf(const collection::Figure& keep, const collection::Figure& replacement);
        void DrawUpgradeEditor(const collection::Figure& figure);
        void OpenEdit(const collection::Figure& figure);
        void DeleteFigure(const std::string& path);
        void DrawAdd();
        void DrawManage();
        void DrawTrash();
        void DrawBackups();
        void DrawImportExport();
        void DrawUnknown();
        void DrawTidy();
        void HandleShortcuts();
        void DrawIcon(ImDrawList* draw, const collection::Toy* toy, collection::VariantKind kind, ImVec2 at, float size, bool large);
        // The best picture of a toy (its variant's artwork when the game has one, else a portrait), fitted into the square
        // `box` at `at` and centred; the drawn icon when there is none.
        void DrawPicture(ImDrawList* draw, const collection::Toy* toy, collection::VariantKind kind, uint16_t variant, ImVec2 at, float box);
        // A swapper's picture, its two halves joined as the game's swap screen shows them (shared/collection/icons.h,
        // SwapperPicture), fitted into the square `box` at `at` and centred. False when a half has no such picture from
        // a disc: the caller draws the top's picture with the bottom small in a corner (DrawBottom).
        bool DrawSwapper(ImDrawList* draw, const collection::Toy* top, uint16_t topVariant, const collection::Toy* bottom, uint16_t bottomVariant, ImVec2 at, float box);
        bool DrawSwapper(ImDrawList* draw, const collection::Card& card, ImVec2 at, float box);
        const FigureIconTexture& Icon(const collection::Toy* toy, collection::VariantKind kind, bool large);
        // A swapper (Swap Force) is one card: the top stands for it, the bottom is drawn small at its corner.
        collection::Card CardOf(const collection::Figure& figure) const;
        bool Busy(const collection::Figure& figure) const { return CardOf(figure).Busy(); }
        void DrawBottom(ImDrawList* draw, const collection::Figure& bottom, ImVec2 at, float size);
        void CloseOverlay();
        void Toast(const std::string& text, ui::ToastKind kind = ui::ToastKind::Info);

        // What Manage and the Figure page show from disk, read again only once the collection changed (m_generation):
        // drawn every frame, they read and decrypted every dump and backup at the display's rate.
        struct BackupRow
        {
            std::string path;
            std::string label; // "28 Oct 2026"
            std::string detail; // "level 6 · 2400 gold"
        };
        void RefreshDisk();
        const std::vector<BackupRow>& Backups(const std::string& figure);

        void PutOn(size_t seat, const std::string& path);
        void OpenFigure(const std::string& path);
        void Import(const std::vector<std::string>& paths);
        void ExportTo(const std::vector<std::string>& paths);
        bool Writable(const collection::Figure& figure) const;
        std::string Name(const collection::Figure& figure) const;
        std::string Detail(const collection::Figure& figure) const;
        std::string SeatName(size_t seat) const;
        std::string SeatTag(size_t seat) const; // "P1", "P2"; "On" for the shared seats
        std::string SectionName(const collection::Section& section) const;
        std::string LiveGame(const collection::Figure& figure) const; // "Spyro's Adventure" when a game holds it

        App& m_app;
        CollectionModel m_model;
        bool m_opened = false;
        std::map<std::string, FigureIconTexture> m_icons;

        // The shelf: filtered and cut into sections when something it depends on changed.
        int m_generation = 0;
        std::string m_listKey;
        std::vector<collection::Figure> m_list;
        std::vector<collection::Section> m_sections;
        int m_hiddenStarters = 0;
        ShelfFilter m_filter;           // Element, Game, Type, Sort (the search is m_search)
        char m_search[64] = {};
        bool m_openFilters = false;     // the screenshot test opens the Filters panel, and closes it at the next screen
        bool m_closeFilters = false;
        bool m_focusSearch = false;     // Y, F3: the search box takes the keyboard next frame
        int m_choosing = -1;            // the seat the next card goes on (a seat clicked, A, LB/RB); -1: browsing
        std::vector<ImVec2> m_crossAt;  // where each seat's X was drawn this frame (-1: none), for the test
        int m_navSeat = -1;             // the seat with the keyboard or pad focus this frame, -1: none
        int m_testMenu = 0;             // the screenshot test opens a menu on its own: 1 Manage's, 2 the figure sheet's, 3 its Put on the portal
        int m_hoverSeat = -1;           // the seat under the mouse this frame
        int m_focusSeat = -1;           // the D-pad crosses between the seats and the shelf: focus this seat next frame,
        bool m_focusCard = false;       // or the shelf's first card
        bool m_navTopCard = false;      // the focused card is in the shelf's first row (Up goes to the seats)
        bool m_navBar = false;          // the search or a filter of the bar has the focus (Up goes to the seats)
        int m_seatMenu = -1;            // the seat whose right-click menu is open
        std::string m_selected;         // the card with the focus
        std::string m_hovered;          // the card under the mouse or the focus (X: its sheet)

        // The overlays.
        Overlay m_overlay = Overlay::None;
        std::string m_figure;           // the figure sheet's dump
        bool m_details = false;         // the sheet's Details are open
        std::string m_changeKeep;       // "Change the bottom/top": the half the card keeps while the other is chosen
        bool m_editing = false;
        figures::FigureStats m_edit;
        figures::FigureStats m_editBase; // as read when Edit stats opened: Save writes back only what changed since
        char m_nickname[64] = {};        // 16 characters of up to 3 bytes, and the terminator
        char m_label[64] = {};
        std::string m_confirm;          // "reset", "delete": the question asked over the sheet
        std::string m_addGame = "ssa";
        uint16_t m_addToy = 0xFFFF;
        int m_addVersion = 0;           // 0: standard, else 1 + an index in the toy's variants
        int m_addCategory = -1;
        char m_addSearch[64] = {};
        char m_addLabel[64] = {};
        std::string m_created;          // the figure Add a figure just made
        int m_diskGeneration = -1;
        std::vector<collection::TrashEntry> m_trash;
        std::vector<std::string> m_trashNames; // the figure each trashed dump is (by m_trash's order)
        size_t m_starters = 0;          // untouched starters (Manage > Tidy)
        std::map<std::string, std::vector<BackupRow>> m_backups; // per figure, filled as asked
        int m_manageTab = 0;
        std::string m_backupsOf;
        uint16_t m_unknownType = 0xFFFF;
        char m_unknownName[64] = {};
    };
}
