#pragma once

// Hold to confirm: a button that acts once it has been held for a second (reset, delete...). The state lives here,
// without ImGui, so SkylauncherTest can drive it; App::HoldButton draws it. One state for the window: one button is held
// at a time.
#include <algorithm>
#include <cstdint>

namespace launcher_ui
{
    class HoldConfirm
    {
    public:
        static constexpr uint64_t HoldMs = 1000;

        // Once a frame for every hold button drawn. `id` names the button (its ImGui ID: labels can repeat), `held` is
        // true while it is pressed and, for the mouse, under the pointer, `inputDown` while the mouse button or an
        // activate key (Space, Enter, the pad's A) is down. Returns how full the button is (0..1); `done` is true on the
        // one frame it acts. A hold cancelled by leaving the button, or one that acted, needs a new press.
        float Update(uint32_t id, bool held, bool inputDown, uint64_t now, bool& done)
        {
            done = false;
            if (m_latched)
            {
                if (inputDown)
                    return m_fired && id == m_id ? 1.0f : 0.0f;
                m_latched = m_fired = false;
                m_id = 0;
            }
            if (!held)
            {
                if (id == m_id)
                {
                    m_id = 0;
                    m_latched = inputDown; // the pointer left while pressing: cancelled until released
                }
                return 0.0f;
            }
            if (id != m_id)
            {
                m_id = id;
                m_since = now;
            }
            float fill = std::min(1.0f, float(now - m_since) / float(HoldMs));
            if (fill >= 1.0f)
                done = m_fired = m_latched = true;
            return fill;
        }

    private:
        uint32_t m_id = 0;
        uint64_t m_since = 0;
        bool m_fired = false;   // it acted: nothing more until the input is released
        bool m_latched = false; // it acted or was cancelled while the input is still down
    };
}
