#pragma once

// Whether a real Portal of Power is plugged in: the VID:PID table and the status line ("Portal: <model>" or
// "Portal: not detected"), then a watcher that re-enumerates SDL's HID devices on a worker thread when one is plugged in or
// out (docs/support.md). The watcher is started by the launcher window; SkylauncherTest checks the table and the status
// logic with fake enumerators.
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace launcher_portal
{
    inline constexpr uint16_t Activision = 0x1430; // every Skylanders portal's vendor
    inline constexpr uint16_t PortalOfPower = 0x0150; // Wii, PS3, Wii U, PC, Mac
    inline constexpr uint16_t XboxPortal = 0x1F17; // Xbox 360 (HID on some systems only)

    struct Device
    {
        uint16_t vendorId = 0;
        uint16_t productId = 0;
    };

    // One enumeration: `available` is false when SDL's HID API could not be started (then there is nothing to show).
    struct Scan
    {
        bool available = false;
        std::vector<Device> devices;
    };
    using Enumerator = std::function<Scan()>;

    // The model for a portal's ids, else "Skylanders portal (1430:XXXX)" for another Activision product, else empty.
    std::string ModelName(uint16_t vendorId, uint16_t productId);

    struct Status
    {
        bool available = false;
        bool connected = false;
        std::string model;
        // The line to show; empty when HID is unavailable (no error) or while the first scan runs.
        std::string Line() const;
    };

    Status MakeStatus(const Scan& scan);
    Status Query(const Enumerator& enumerate); // the status logic, with a fake enumerator in tests

    // SDL_hid_init and SDL_hid_enumerate(0x1430, 0), which can take tens of milliseconds: a worker thread only.
    Scan EnumerateHid();

    // Scans once at start, then only when SDL_hid_device_change_count() has changed, at most every 2 s. Current() never
    // blocks the UI.
    class Watcher
    {
    public:
        Watcher() = default;
        ~Watcher() { Stop(); }
        Watcher(const Watcher&) = delete;
        Watcher& operator=(const Watcher&) = delete;

        void Start();
        void Stop();
        Status Current() const;

    private:
        void Run();

        mutable std::mutex m_mutex;
        Status m_status;
        std::thread m_thread;
        std::atomic<bool> m_stop{ false };
    };
}
