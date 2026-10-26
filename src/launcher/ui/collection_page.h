#pragma once

// The Collection page: the portal at the next start (the seats of the chosen game), the bar (search, Element, Game,
// Type, Add a figure), the shelf (every figure once, favourites first), the figure sheet, Add a figure and Manage
// (docs/collection_ui.md). The figures are the shared collection's (SSARecomp's library); the
// look is the launcher's. Drawn by App's widgets (a friend), in collection_page.cpp, collection_figure.cpp,
// collection_add.cpp and collection_manage.cpp.
#include "launcher/ui/collection_model.h"
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
    void DrawStar(ImDrawList* draw, ImVec2 center, float radius, ImU32 color);
    std::string PlayTime(uint32_t seconds);
    std::string ShortDate(const std::string& iso);    // "2026-10-24 10:15" from ISO 8601 UTC
    std::string ElementName(collection::Element element);
    unsigned ElementInk(collection::Element element); // 0xRRGGBB
    std::string Lower(std::string text);

    // A figure's picture: the disc's portrait, or a generated icon (the page writes the initials over those).
    struct FigureIconTexture
    {
        SDL_Texture* texture = nullptr;
        bool generated = false;
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

        // SSA_LAUNCHER_TEST_SEATX: the X of every seat, pressed by mouse, keyboard and pad (App::TestSeatXScript).
        std::vector<std::string> TestSeats() const { return m_model.seats; }
        void TestSetSeats(const std::vector<std::string>& seats); // the seats as given, written to the session file
        void TestFocusSeat(size_t seat) { m_focusSeat = int(seat); }
        ImVec2 TestCrossAt(size_t seat) const { return seat < m_crossAt.size() ? m_crossAt[seat] : ImVec2(-1, -1); }

        // Screenshot test (SSA_LAUNCHER_TEST_COLLECTION): every screen, in a scratch collection.
        void Seed();
        void TestScreen(int step);
        static const char* TestShotName(int step);
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
        bool FilterMenu(const char* id, const char* all, const std::string& current, float width, const std::function<void()>& items);
        void DrawList();
        bool DrawCard(const collection::Figure& figure, float width, bool dim);
        void DrawOverlay();
        void DrawFigurePage(ImVec2 size);
        void DrawFigureFacts(const collection::Figure& figure, float width);
        void DrawDetails(const collection::Figure& figure);
        void DrawEditStats(const collection::Figure& figure);
        void OpenEdit(const collection::Figure& figure);
        void DeleteFigure(const std::string& path);
        void DrawAdd(ImVec2 size);
        void DrawManage(ImVec2 size);
        void DrawTrash();
        void DrawBackups();
        void DrawImportExport();
        void DrawUnknown();
        void DrawTidy();
        void HandleShortcuts();
        void DrawIcon(ImDrawList* draw, const collection::Toy* toy, collection::VariantKind kind, ImVec2 at, float size, bool large);
        const FigureIconTexture& Icon(const collection::Toy* toy, collection::VariantKind kind, bool large);
        void CloseOverlay();
        void Toast(const std::string& text, bool error = false);

        // What Manage and the Figure page show from disk, read again only once the collection changed (m_generation):
        // drawn every frame, they read and decrypted every dump and backup at the display's rate.
        struct BackupRow
        {
            std::string path;
            std::string label; // "2026-10-24 10:15   level 6 · 2400 gold"
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
        ShelfFilter m_filter;           // Element, Game, Type (the search is m_search)
        char m_search[64] = {};
        bool m_focusSearch = false;     // Y, F3: the search box takes the keyboard next frame
        int m_choosing = -1;            // the seat the next card goes on (a seat clicked, A, LB/RB); -1: browsing
        std::vector<ImVec2> m_crossAt;  // where each seat's X was drawn this frame (-1: none), for the test
        int m_navSeat = -1;             // the seat with the keyboard or pad focus this frame, -1: none
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
        bool m_editing = false;
        figures::FigureStats m_edit;
        figures::FigureStats m_editBase; // as read when Edit stats opened: Save writes back only what changed since
        char m_nickname[64] = {};        // 16 characters of up to 3 bytes, and the terminator
        char m_label[64] = {};
        std::string m_confirm;          // "restore:<backup>", "reset", "delete": the bar over the sheet's actions
        std::string m_addGame = "ssa";
        uint16_t m_addToy = 0xFFFF;
        int m_addVersion = 0;           // 0: standard, else 1 + an index in the toy's variants
        int m_addCategory = -1;
        char m_addSearch[64] = {};
        char m_addLabel[64] = {};
        std::string m_created;          // the figure Add a figure just made
        int m_diskGeneration = -1;
        std::vector<collection::TrashEntry> m_trash;
        size_t m_starters = 0;          // untouched starters (Manage > Tidy)
        std::map<std::string, std::vector<BackupRow>> m_backups; // per figure, filled as asked
        int m_manageTab = 0;
        std::string m_backupsOf;
        uint16_t m_unknownType = 0xFFFF;
        char m_unknownName[64] = {};
    };
}
