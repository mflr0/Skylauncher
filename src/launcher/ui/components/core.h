#pragma once

// What every component shares: the context (fonts, the focus colour, the pad family, motion), the state model, the text
// styles, colour helpers, the focus ring and the icons. Components draw with these and theme/tokens.h only
// (docs/launcher_ui.md, "Components").
#include "launcher/ui/hold_confirm.h"
#include "launcher/ui/theme/tokens.h"
#include <cstdint>
#include <imgui.h>
#include <string>
#include <vector>
#include <map>

namespace ui
{
    // Directional navigation in a two-column page. Tab keeps ImGui's usual submission order.
    class NavigationColumns
    {
    public:
        void Begin(bool move = true);
        void Column(int column) { m_column = column; }
        void End();
        void Item(uint32_t id, ImVec2 min, ImVec2 max);
    private:
        struct Entry { uint32_t id; ImVec2 min, max; int column; };
        std::vector<Entry> m_previous, m_current;
        uint32_t m_target = 0;
        int m_column = 0;
    };
    struct Fonts
    {
        ImFont* text = nullptr;  // Outfit Regular
        ImFont* bold = nullptr;  // Outfit SemiBold
        ImFont* title = nullptr; // Bungee
        ImFont* mono = nullptr;
        ImFont* icons = nullptr; // Lucide
    };

    // Whose names the pad legend and the key caps show: the keyboard's keys or a pad family's buttons.
    enum class Glyphs { Keyboard, Xbox, PlayStation, Nintendo };

    // One per window, set by the window before it draws (App::Frame).
    struct Context
    {
        Fonts fonts;
        unsigned accent = color::Focus;  // the focus ring: the shown game's accent on its pages, the launcher's blue elsewhere
        Glyphs glyphs = Glyphs::Keyboard;
        bool reduceMotion = false;
        uint64_t now = 0;                // this frame's time, ms
        launcher_ui::HoldConfirm hold;   // the hold-to-confirm buttons' state (one is held at a time)
        uint32_t testHeld = 0;           // the screenshot test holds this hold button down (its ImGui ID)
        bool animating = false;          // something drawn last frame moves (a spinner, a held button): draw again now
        uint32_t holdHintId = 0;         // the hold button whose "Hold to confirm" shows after a tap too short
        uint64_t holdHintUntil = 0;
        std::vector<uint32_t>* testNavigationItems = nullptr; // screenshot checks: enabled controls in the game page
        std::map<uint32_t, std::string> testNavigationLabels;
        NavigationColumns* navigation = nullptr;
    };
    Context& Ctx();
    // Once a frame, before any component: the time, and `animating` cleared for this frame.
    void NewFrame(uint64_t now);

    // ---- The state model: every component draws these, in this order of precedence: disabled, loading, pressed, hover;
    // the focus ring is drawn on top of any of them. ----
    struct State
    {
        bool hovered = false;
        bool focused = false;  // the keyboard or pad focus, with its cursor shown (the mouse hides it)
        bool pressed = false;
        bool disabled = false;
        bool loading = false;
        float holdFill = -1;   // the gallery: a hold button this far into its hold
        bool preview = false;  // forced by PreviewState (a tooltip then shows at once)
    };
    // The gallery: the next item a component reads its state from (its first part: the main half of the main action, the
    // first tab) looks as if in `state`, whatever the input does.
    void PreviewState(const State& state);
    // The last item's state, with what PreviewState forces (and then forgets).
    State ItemState(bool disabled = false, bool loading = false);

    // ---- Text styles (tokens.h, "Type"). ----
    ImFont* FontOf(TextStyle style, bool bold = false);
    float SizeOf(TextStyle style);
    void PushText(TextStyle style, bool bold = false);
    void PopText();
    // Where a line of text drawn at y lies: the middle of its capitals (an icon beside it is centred there, a row's centre
    // line goes through it), their top, and its baseline (two sizes on one line share it), as offsets from y. Lines stacked
    // in a row are centred from the first one's capitals to the last one's baseline. `size` 0: the style's.
    float CapMiddle(TextStyle style, bool bold = false, float size = 0);
    float CapTop(TextStyle style, bool bold = false, float size = 0);
    float Baseline(TextStyle style, bool bold = false, float size = 0);
    ImVec2 TextSize(TextStyle style, const char* text, const char* end = nullptr, bool bold = false, float wrap = -1);
    // A text item at the cursor, wrapped at `wrap` points (0: the available width; < 0: no wrap).
    void Text(TextStyle style, const char* text, unsigned rgb = color::Ink, float wrap = 0, bool bold = false);
    // Text drawn at `at` without an item, in the current font.
    void DrawText(ImDrawList* draw, ImVec2 at, ImU32 color, const char* text, const char* end = nullptr);
    // Where the shown part of "Label##id" ends.
    const char* LabelEnd(const char* label);
    // `text` in the current font cut to `width` with "..." (the launcher's one ellipsis: three dots), whole if it fits.
    std::string Ellipsize(const char* text, float width);
    // The size, at most `size`, at which `text` in `font` fits in `width`.
    float FitSize(ImFont* font, float size, const char* text, float width);

    // ---- Colour. ----
    unsigned Mix(unsigned a, unsigned b, float t);
    unsigned Lift(unsigned rgb, float amount); // towards white (> 0) or black (< 0)
    float Luminance(unsigned rgb);
    float Contrast(unsigned a, unsigned b);
    unsigned InkOn(unsigned fill, unsigned dark); // white when it gets 3:1 on `fill` (large bold text), else `dark`
    // A colour for the draw list, faded with ImGui's style alpha (a disabled control fades with it).
    ImU32 Col(unsigned rgb, float alpha = 1.0f);
    float Snap(float v); // on whole output pixels

    // The pressable area of a component (an InvisibleButton in the keyboard and pad navigation). ImGui's own square focus
    // cursor is left out: the component draws the ring. True when pressed.
    bool Hit(const char* id, ImVec2 size);
    // A settings row's one alignment rule: every control ends on the control column's right edge. A control `width` wide that
    // starts the column's line moves there (Button, Pill, IconButton, Toggle, Checkbox, Segmented, KeyButton and KeyCap call
    // it); one that fills the column is already there, and the controls of a row placed with AlignRight stay where they are
    // put. Nothing outside a settings row (layout.cpp).
    void SettingControl(float width);

    // ---- Drawing. ----
    // The keyboard and pad focus ring around min..max (focus::Width, focus::Offset, Context::accent).
    void FocusRing(ImVec2 min, ImVec2 max, float rounding, ImDrawFlags flags = 0);
    // The ring around the last item when `state.focused`.
    void FocusRingIf(const State& state, float rounding, ImDrawFlags flags = 0);
    void SoftShadow(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, float offsetY, float spread, float alpha);
    // A rounded rectangle filled with a vertical gradient, alpha included (`flags`: which corners are rounded).
    void GradientRect(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom, float rounding, ImDrawFlags flags = 0);
    // A short text in a bubble under min..max (above it when there is no room below), kept inside the window: the tooltip
    // of a control and the hint of a hold button. Drawn this frame only.
    void Bubble(const char* text, ImVec2 min, ImVec2 max);
    // Whether the last item's tooltip shows: under the pointer (after ImGui's delay) or with the keyboard / pad focus.
    bool WantsTooltip(const State& state);
    // Takes the keyboard and pad focus on the last item (and shows its cursor when `show`).
    void FocusLastItem(bool show);

    // ---- Icons: Lucide's (resources/fonts/Lucide.ttf, tools/lucide_subset.py), one family, outlined, centred on `centre` in a
    // square of `size` points (icon::Small, Normal or Large). ----
    enum class Icon
    {
        None, Check, Cross, Plus, Minus, ChevronDown, ChevronUp, ChevronLeft, ChevronRight, ArrowUp, ArrowDown,
        Search, Gear, Info, Warning, Error, Download, Trash, Folder, Disc, Play, Hold, More, Star,
        Success, Update, Pause, Unknown, Loader, // a state: done, an update, stopped, not known yet; the spinner's
        Gamepad, Circle, Square, Triangle, // with Cross: PlayStation's face buttons
        Trophy, External // External: opens a web page
    };
    void DrawIcon(ImDrawList* draw, Icon icon, ImVec2 centre, float size, ImU32 color);
    // The loader icon turning (an indeterminate wait); still when motion is reduced.
    void DrawSpinner(ImDrawList* draw, ImVec2 centre, float size, ImU32 color);
    // The time of the moving parts: 0..1 over `periodMs`, 0 with reduced motion. Marks the frame as animating.
    float Phase(int periodMs);
}
