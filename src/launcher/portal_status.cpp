// The portal status: the VID:PID table, the line the launcher shows, and the background watcher (portal_status.h).
#include "launcher/portal_status.h"
#include "launcher/i18n.h"
#include <SDL3/SDL.h>
#include <cstdio>

namespace launcher_portal
{
    namespace
    {
        struct Model
        {
            uint16_t productId;
            const char* name;
        };

        // Activision's portals, by product id (the vendor is always 0x1430).
        constexpr Model Models[] = {
            { PortalOfPower, "Portal of Power" },
            { XboxPortal, "Xbox 360 portal" },
        };

        constexpr uint64_t ScanIntervalMs = 2000;
        constexpr uint64_t PollIntervalMs = 250; // how often the change count is read, and how quickly Stop returns
    }

    std::string ModelName(uint16_t vendorId, uint16_t productId)
    {
        if (vendorId != Activision)
            return {};
        for (const Model& model : Models)
            if (model.productId == productId)
                return model.name;
        char name[64];
        snprintf(name, sizeof(name), "Skylanders portal (%04X:%04X)", vendorId, productId);
        return name;
    }

    std::string Status::Line() const
    {
        if (!available)
            return {};
        if (connected)
            return Tf("Portal: {}", model);
        return Tr("Portal: not detected");
    }

    Status MakeStatus(const Scan& scan)
    {
        Status status;
        status.available = scan.available;
        if (!scan.available)
            return status;
        // The first portal of the list: the launcher only names one, even with several plugged in.
        for (const Device& device : scan.devices)
            if (std::string model = ModelName(device.vendorId, device.productId); !model.empty())
            {
                status.connected = true;
                status.model = std::move(model);
                break;
            }
        return status;
    }

    Status Query(const Enumerator& enumerate)
    {
        return MakeStatus(enumerate());
    }

    Scan EnumerateHid()
    {
        // All HID devices, not only game controllers (SDL's default); done once, before the first enumeration.
        static const bool ready = [] {
            SDL_SetHint(SDL_HINT_HIDAPI_ENUMERATE_ONLY_CONTROLLERS, "0");
            return SDL_hid_init() == 0;
        }();
        Scan scan;
        if (!ready)
            return scan;
        scan.available = true;
        SDL_hid_device_info* devices = SDL_hid_enumerate(Activision, 0);
        for (SDL_hid_device_info* device = devices; device != nullptr; device = device->next)
            scan.devices.push_back({ device->vendor_id, device->product_id });
        SDL_hid_free_enumeration(devices);
        return scan;
    }

    void Watcher::Start()
    {
        if (m_thread.joinable())
            return;
        m_stop = false;
        m_thread = std::thread([this] { Run(); });
    }

    void Watcher::Stop()
    {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
    }

    Status Watcher::Current() const
    {
        std::lock_guard lock(m_mutex);
        return m_status;
    }

    void Watcher::Run()
    {
        {
            Scan scan = EnumerateHid();
            std::lock_guard lock(m_mutex);
            m_status = MakeStatus(scan);
            if (!scan.available)
                return; // HID could not start: no error, and no point polling
        }

        uint32_t changes = SDL_hid_device_change_count();
        uint64_t scannedAt = SDL_GetTicks();
        while (!m_stop)
        {
            SDL_Delay(PollIntervalMs);
            uint32_t now = SDL_hid_device_change_count();
            uint64_t at = SDL_GetTicks();
            if (now == changes || at - scannedAt < ScanIntervalMs)
                continue;
            changes = now;
            scannedAt = at;
            Status status = MakeStatus(EnumerateHid());
            std::lock_guard lock(m_mutex);
            m_status = std::move(status);
        }
    }
}
