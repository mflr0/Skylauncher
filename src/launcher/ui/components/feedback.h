#pragma once

// What tells the player something: Badge, Chip (filter and sort), StatusPill, ProgressBar, IndeterminateBar, Spinner, Toasts,
// KeyCap and PadLegend.
#include "launcher/ui/components/core.h"
#include <span>
#include <string>
#include <vector>

namespace ui
{
    // ---- Badge: a word on a card (Legendary, Series 2, P1), not pressable. Its ink is chosen for contrast when 0. ----
    ImVec2 BadgeSize(const char* text);
    ImVec2 DrawBadge(ImDrawList* draw, ImVec2 at, const char* text, unsigned fill, unsigned ink = 0); // returns its size
    void Badge(const char* text, unsigned fill, unsigned ink = 0); // at the cursor, as an item

    // ---- Chip: a filter that is on or off (a check when on), or the sort order (an arrow; never removable, never a filter). ----
    enum class ChipKind { Filter, Sort };
    struct ChipOptions
    {
        ChipKind kind = ChipKind::Filter;
        bool selected = false; // a filter that is on
        unsigned color = 0;    // a filter's fill when on (an element's colour); 0: the accent
        int direction = 1;     // a sort: 1 ascending (arrow up), -1 descending
        bool disabled = false;
    };
    bool Chip(const char* label, const ChipOptions& options = {});

    // ---- Status: a state in a word and a shape, never a colour alone. ----
    enum class Status { Neutral, Info, Success, Warning, Error, Busy };
    unsigned ColorOf(Status status);
    Icon IconOf(Status status); // Busy: a spinner is drawn instead
    // The shape alone, `size` points, in the status's colour: its icon (IconOf), or `icon` (a state's own: a disc, an update);
    // Busy turns the loader.
    void DrawStatusIcon(ImDrawList* draw, Status status, ImVec2 centre, float size);
    void DrawStatusIcon(ImDrawList* draw, Status status, Icon icon, ImVec2 centre, float size);
    // The icon and the text in a tinted pill, PillHeight high (`icon`: the state's own, else the status's).
    inline constexpr float PillHeight = 24;
    void StatusPill(Status status, const char* text, Icon icon = Icon::None);

    // ---- Progress. ----
    // A determinate bar (`fraction` 0..1) with its percentage after it when `showPercent`.
    void ProgressBar(float fraction, float width = -1, bool showPercent = false);
    // A wait with no known end: a band sweeping along the same track (still, half full, with reduced motion).
    void IndeterminateBar(float width = -1);
    void Spinner(float size = type::Body);

    // ---- Toasts: short messages at the bottom right, newest at the bottom. Info and Success go by themselves; an Error stays
    // until it is closed: a click, or the keyboard and pad (it takes the focus when they are in use; A, B, Enter, Escape). ----
    enum class ToastKind { Info, Success, Error };
    class Toasts
    {
    public:
        struct Item
        {
            std::string text;
            ToastKind kind = ToastKind::Info;
            uint64_t until = 0; // when it goes; 0: when it is closed
            uint32_t serial = 0; // tells two messages apart (the one that took the focus)
        };
        static constexpr size_t Max = 4;
        void Push(std::string text, ToastKind kind);
        void Clear() { m_items.clear(); }
        const std::vector<Item>& Items() const { return m_items; }
        // Draws the stack, its bottom `bottomInset` points above the window's bottom. Returns true when the player closed
        // one with the keyboard or the pad (the caller puts the focus back on its page).
        bool Draw(float bottomInset, float maxTextWidth = 420);
        bool HasFocus() const { return m_focused; } // it answers B / Escape this frame
        int NextWakeMs(uint64_t now) const;         // until the next one goes by itself, -1 for none
        void PreviewFocus(bool on) { m_previewFocus = on; } // the gallery: the newest error drawn with the focus

    private:
        std::vector<Item> m_items;
        bool m_focused = false;
        bool m_previewFocus = false;
        uint32_t m_serial = 0;
        uint32_t m_focusedSerial = 0; // the error that took the focus (once)
    };

    // ---- Key caps and the pad legend. ----
    // A pad button by its place (South is A on Xbox, Cross on PlayStation, B on Nintendo).
    enum class PadButton { South, East, West, North, LeftBumper, RightBumper, Start, Back };
    const char* PadButtonLabel(PadButton button, Glyphs glyphs);
    // A pad button's cap (`glyphs` != Keyboard) or a keyboard key's (`key`: "Enter", "Esc", "F6"), drawn at `at` (its left
    // edge, its vertical centre); returns its size.
    ImVec2 DrawKeyCap(ImDrawList* draw, ImVec2 at, PadButton button, Glyphs glyphs);
    ImVec2 DrawKeyCap(ImDrawList* draw, ImVec2 at, const char* key);
    float KeyCapWidth(PadButton button, Glyphs glyphs);
    float KeyCapWidth(const char* key);
    float KeyCapHeight();
    void KeyCap(PadButton button, Glyphs glyphs); // at the cursor, as an item
    void KeyCap(const char* key);

    struct PadHint
    {
        PadButton button;
        const char* key;   // the keyboard's key for it ("Enter"); nullptr: not shown on the keyboard
        const char* label; // what it does ("Select", "Back"), translated
    };
    // The bottom hint bar (height::Normal, the available width): each hint's cap and its word, in the glyphs in use.
    void PadLegend(std::span<const PadHint> hints, Glyphs glyphs, float width = -1);
}
