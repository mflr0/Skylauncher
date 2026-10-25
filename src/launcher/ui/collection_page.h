#pragma once

// The Collection page: the seats of the game's next session, My Skylanders in sections, the View menu, the Figure page,
// Add a figure and Manage (docs/collection_ui.md). The figures are the shared collection's (SSARecomp's library); the
// look is the launcher's. Drawn by App's widgets (a friend), in collection_page.cpp, collection_figure.cpp,
// collection_add.cpp and collection_manage.cpp.
#include "launcher/ui/collection_model.h"
#include "shared/figures/figure_icons.h"
#include <SDL3/SDL.h>
#include <imgui.h>
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
    bool HasProgress(const collection::Figure& figure); // a character: it has a level, gold...

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
        ~CollectionPage() { ClearIcons(); }

        void Draw();
        // The pictures come from the disc once it is verified: drop the cached textures.
        void ClearIcons();
        // A dropped file or folder: figure dumps join the collection. False if it holds none (a disc image).
        bool Drop(const std::string& path);
        std::string Folder() const;
        // B / Escape belongs to the page: an overlay, the View menu or a search is open.
        bool TakesBack() const { return m_overlay != Overlay::None || m_viewMenu || m_search[0] != 0; }

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
        void DrawHeader(float width);
        void DrawSeats();
        void DrawSeatMenu();
        void TakeOff(size_t seat);
        void DrawList();
        void DrawViewMenu(ImVec2 anchor);
        bool DrawCard(const collection::Figure& figure, float width, bool dim);
        void DrawOverlay();
        void DrawFigurePage(ImVec2 size);
        void DrawStats(const collection::Figure& figure, float width);
        void DrawEditStats(const collection::Figure& figure);
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
        void Toast(const std::string& text);

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
        std::string SectionName(const collection::Section& section) const;
        std::string LiveGame(const collection::Figure& figure) const; // "Spyro's Adventure" when a game holds it

        App& m_app;
        CollectionModel m_model;
        bool m_opened = false;
        std::map<std::string, FigureIconTexture> m_icons;

        // The list: filtered and cut into sections when something it depends on changed.
        int m_generation = 0;
        std::string m_listKey;
        std::vector<collection::Figure> m_list;
        std::vector<collection::Section> m_sections;
        int m_hiddenStarters = 0;
        size_t m_seat = 0;              // the focused seat: the list shows what goes there
        int m_navSeat = -1;             // the seat with the keyboard or pad focus this frame, -1: none
        int m_hoverSeat = -1;           // the seat under the mouse this frame
        bool m_navEdgeCard = false;     // the focused card is the first of its row (Left goes to the seats)
        int m_focusSeat = -1;           // the D-pad crosses between the columns: focus this seat next frame,
        bool m_focusCard = false;       // or the list's first card
        int m_seatMenu = -1;            // the seat whose right-click menu is open
        std::string m_selected;         // the card with the focus (double click or Enter: put on the seat)
        std::string m_hovered;          // the card under the mouse or the focus (X: its Figure page)
        char m_search[64] = {};
        collection::View m_view;
        bool m_foreignOpen = false;     // "Not for this game" unfolded
        int m_jumpTo = -1;              // a section to scroll to (strip, LB/RB)
        int m_currentSection = 0;
        std::vector<float> m_sectionY;  // where each section starts in the list (last frame)
        bool m_viewMenu = false;
        bool m_viewFocus = false;       // the View menu just opened (focus it) or closed (focus its button)

        // The overlays.
        Overlay m_overlay = Overlay::None;
        std::string m_figure;           // the Figure page's dump
        bool m_editing = false;
        figures::FigureStats m_edit;
        char m_nickname[48] = {};
        char m_label[64] = {};
        bool m_renaming = false;
        std::string m_confirm;          // "restore:<backup>"
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
