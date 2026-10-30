#pragma once

// Window operations for a process started by GameSession. Never search by executable name or window title.
#include <SDL3/SDL.h>
#include <cstdint>
#include <algorithm>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <objc/message.h>
#include <objc/runtime.h>
#elif defined(__linux__) && __has_include(<X11/Xlib.h>)
#define LAUNCHER_X11_WINDOW_CONTROL
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <cstring>
#include <vector>
#endif

namespace game_window
{
#if defined(_WIN32)
    inline HWND Find(uint32_t pid, bool visibleOnly = true)
    {
        struct Search { DWORD pid; HWND window; bool visibleOnly; } search{ DWORD(pid), nullptr, visibleOnly };
        EnumWindows([](HWND window, LPARAM data) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(data);
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner == search.pid && (!search.visibleOnly || (IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr)))
            {
                search.window = window;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.window;
    }
    inline bool Available(uint32_t pid) { return Find(pid) != nullptr; }
    inline bool Show(uint32_t pid)
    {
        HWND window = Find(pid);
        if (window == nullptr) return false;
        ShowWindowAsync(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
        return SetForegroundWindow(window) != 0;
    }
    inline bool Close(uint32_t pid)
    {
        HWND window = Find(pid);
        if (window == nullptr) window = Find(pid, false);
        return window != nullptr && PostMessageW(window, WM_CLOSE, 0, 0) != 0;
    }
#elif defined(__APPLE__)
    inline id Application(uint32_t pid)
    {
        return reinterpret_cast<id (*)(id, SEL, int)>(objc_msgSend)(
            reinterpret_cast<id>(objc_getClass("NSRunningApplication")), sel_registerName("runningApplicationWithProcessIdentifier:"), int(pid));
    }
    inline bool Available(uint32_t pid)
    {
        // Include minimized windows, but exclude non-windowed command line processes.
        CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
        bool found = false;
        if (windows != nullptr)
        {
            for (CFIndex i = 0; i < CFArrayGetCount(windows); i++)
            {
                auto window = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, i));
                int owner = 0, layer = -1;
                auto number = static_cast<CFNumberRef>(CFDictionaryGetValue(window, kCGWindowOwnerPID));
                if (number != nullptr) CFNumberGetValue(number, kCFNumberIntType, &owner);
                number = static_cast<CFNumberRef>(CFDictionaryGetValue(window, kCGWindowLayer));
                if (number != nullptr) CFNumberGetValue(number, kCFNumberIntType, &layer);
                if (owner == int(pid) && layer == 0) { found = true; break; }
            }
            CFRelease(windows);
        }
        return found && Application(pid) != nil;
    }
    inline bool Show(uint32_t pid)
    {
        id app = Application(pid);
        if (app == nil) return false;
        reinterpret_cast<BOOL (*)(id, SEL)>(objc_msgSend)(app, sel_registerName("unhide"));
        // NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps.
        return reinterpret_cast<BOOL (*)(id, SEL, unsigned long)>(objc_msgSend)(app, sel_registerName("activateWithOptions:"), 3);
    }
#elif defined(LAUNCHER_X11_WINDOW_CONTROL)
    // Load Xlib only on X11. Wayland deliberately offers no cross-process window activation API.
    struct X11
    {
        SDL_SharedObject* library = SDL_LoadObject("libX11.so.6");
        decltype(&XOpenDisplay) open = nullptr;
        decltype(&XCloseDisplay) close = nullptr;
        decltype(&XDefaultRootWindow) rootOf = nullptr;
        decltype(&XInternAtom) atomOf = nullptr;
        decltype(&XGetWindowProperty) property = nullptr;
        decltype(&XFree) freeData = nullptr;
        decltype(&XSendEvent) send = nullptr;
        decltype(&XFlush) flush = nullptr;
        decltype(&XSync) sync = nullptr;
        decltype(&XSetErrorHandler) setErrorHandler = nullptr;
        XErrorHandler previousErrorHandler = nullptr;
        Display* display = nullptr;
        Window root = 0;
        X11()
        {
            const char* driver = SDL_GetCurrentVideoDriver();
            if (library == nullptr || driver == nullptr || std::strcmp(driver, "x11") != 0) return;
#define WINDOW_LOAD(member, name) member = reinterpret_cast<decltype(member)>(SDL_LoadFunction(library, name))
            WINDOW_LOAD(open, "XOpenDisplay"); WINDOW_LOAD(close, "XCloseDisplay");
            WINDOW_LOAD(rootOf, "XDefaultRootWindow"); WINDOW_LOAD(atomOf, "XInternAtom");
            WINDOW_LOAD(property, "XGetWindowProperty"); WINDOW_LOAD(freeData, "XFree");
            WINDOW_LOAD(send, "XSendEvent"); WINDOW_LOAD(flush, "XFlush");
            WINDOW_LOAD(sync, "XSync"); WINDOW_LOAD(setErrorHandler, "XSetErrorHandler");
#undef WINDOW_LOAD
            if (open && close && rootOf && atomOf && property && freeData && send && flush && sync && setErrorHandler)
                display = open(nullptr);
            if (display != nullptr)
            {
                // A window may disappear between reading the client list and its PID. Ignore that stale window.
                previousErrorHandler = setErrorHandler([](Display*, XErrorEvent*) { return 0; });
                root = rootOf(display);
            }
        }
        ~X11()
        {
            if (display)
            {
                sync(display, False);
                close(display);
                setErrorHandler(previousErrorHandler);
            }
            if (library) SDL_UnloadObject(library);
        }
        std::vector<unsigned long> Read(Window window, const char* name, Atom type)
        {
            Atom actual = 0;
            int format = 0;
            unsigned long count = 0, remaining = 0;
            unsigned char* bytes = nullptr;
            std::vector<unsigned long> values;
            if (property(display, window, atomOf(display, name, False), 0, 65536, False, type,
                &actual, &format, &count, &remaining, &bytes) == Success && actual == type && format == 32 && bytes != nullptr)
                values.assign(reinterpret_cast<unsigned long*>(bytes), reinterpret_cast<unsigned long*>(bytes) + count);
            if (bytes != nullptr) freeData(bytes);
            return values;
        }
        Window Find(uint32_t pid)
        {
            if (display == nullptr) return 0;
            const Atom active = atomOf(display, "_NET_ACTIVE_WINDOW", False);
            const auto supported = Read(root, "_NET_SUPPORTED", XA_ATOM);
            if (std::find(supported.begin(), supported.end(), active) == supported.end()) return 0;
            for (Window window : Read(root, "_NET_CLIENT_LIST", XA_WINDOW))
            {
                const auto owner = Read(window, "_NET_WM_PID", XA_CARDINAL);
                if (owner.size() == 1 && owner.front() == pid) return window;
            }
            return 0;
        }
    };
    inline bool Available(uint32_t pid) { X11 x; return x.Find(pid) != 0; }
    inline bool Show(uint32_t pid)
    {
        X11 x;
        Window window = x.Find(pid);
        if (window == 0) return false;
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.display = x.display;
        event.xclient.window = window;
        event.xclient.message_type = x.atomOf(x.display, "_NET_ACTIVE_WINDOW", False);
        event.xclient.format = 32;
        event.xclient.data.l[0] = 2; // a launcher asking on the player's behalf
        event.xclient.data.l[1] = CurrentTime;
        bool sent = x.send(x.display, x.root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event) != 0;
        x.flush(x.display);
        return sent;
    }
#else
    inline bool Available(uint32_t) { return false; }
    inline bool Show(uint32_t) { return false; }
#endif
}
