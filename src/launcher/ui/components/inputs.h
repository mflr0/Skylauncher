#pragma once

// Controls that choose or enter a value: Tabs, Segmented, Select (a combo), Toggle, Checkbox, Radio, Slider, TextField and
// SearchField. Each takes the keyboard and pad focus and shows it.
#include "launcher/ui/components/core.h"
#include <cstddef>
#include <span>

namespace ui
{
    // A row of words, the selected one bold with an accent line under it. LB / RB (the pad's bumpers) change tab, and their
    // key caps show at the row's ends while a pad is in use.
    struct TabsOptions
    {
        bool bumpers = true;       // LB / RB change tab
        bool focusSelected = false; // the selected tab takes the keyboard and pad focus this frame
        bool showFocus = false;     // and shows its ring
    };
    enum class TabChange { None, Click, Bumper };
    TabChange Tabs(const char* id, std::span<const char* const> labels, int* current, const TabsOptions& options = {});

    // Two to four exclusive choices side by side, in one track (a short list that fits: "30 / 60", "Grid / List").
    bool Segmented(const char* id, std::span<const char* const> labels, int* current, float width = 0, bool disabled = false);

    // A closed box with the chosen value and a chevron; its list opens under it. Between BeginSelect (true while open) and
    // EndSelect, one SelectOption per choice: the chosen one has a check.
    struct SelectOptions
    {
        float width = -1; // < 0: the available width
        bool disabled = false;
    };
    bool BeginSelect(const char* id, const char* preview, const SelectOptions& options = {});
    float SelectWidth(const char* longest); // a select as narrow as can show `longest` uncut
    bool SelectOption(const char* label, bool selected, bool disabled = false);
    void EndSelect();

    // A switch: on (accent) or off, its label after it ("##id" for none: a settings row's label is the row's).
    bool Toggle(const char* label, bool* value, bool disabled = false);
    // A box with a check, its label after it: one of several independent options.
    bool Checkbox(const char* label, bool* value, bool disabled = false);
    // A round choice among several, its label after it; true when chosen.
    bool Radio(const char* label, bool selected, bool disabled = false);

    // A value on a track with a round knob; Left / Right (the arrows, the D-pad) step it when focused. `showValue` writes it
    // after the track.
    struct SliderOptions
    {
        float width = -1;
        bool disabled = false;
        bool showValue = true;
    };
    bool Slider(const char* id, int* value, int min, int max, const SliderOptions& options = {});

    // A one-line text field (height::Normal), its hint in the empty box.
    struct TextFieldOptions
    {
        const char* hint = nullptr;
        float width = -1;
        bool disabled = false;
        ImGuiInputTextFlags flags = 0;
        ImGuiInputTextCallback callback = nullptr;
    };
    bool TextField(const char* id, char* buffer, size_t size, const TextFieldOptions& options = {});
    // A text field with a magnifier and, when not empty, a cross that clears it (Escape clears it too).
    bool SearchField(const char* id, char* buffer, size_t size, const char* hint, float width = -1);
}
