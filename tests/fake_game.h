#pragma once

// Re-exec the test binary in this mode to exercise process ownership without a game or a compiler at test time.
#include <SDL3/SDL.h>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <string>
#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace fake_game
{
#if !defined(_WIN32)
    inline volatile std::sig_atomic_t quit = 0;
    inline void Terminate(int) { quit = 1; }
#endif
    inline int Run()
    {
        const char* mode = std::getenv("SKY_FAKE_GAME_MODE");
        const bool stubborn = mode != nullptr && std::strcmp(mode, "stubborn") == 0;
#if defined(_WIN32)
        WNDCLASSW type{};
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"LauncherSessionCheck";
        type.lpfnWndProc = [](HWND window, UINT message, WPARAM w, LPARAM l) -> LRESULT {
            if (message == WM_CLOSE)
            {
                const char* mode = std::getenv("SKY_FAKE_GAME_MODE");
                if (mode == nullptr || std::strcmp(mode, "stubborn") != 0) PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcW(window, message, w, l);
        };
        RegisterClassW(&type);
        HWND window = CreateWindowW(type.lpszClassName, L"Session check", WS_OVERLAPPEDWINDOW,
            0, 0, 100, 100, nullptr, nullptr, type.hInstance, nullptr);
        if (window == nullptr) return 2;
        const unsigned long pid = GetCurrentProcessId();
#else
        std::signal(SIGTERM, stubborn ? SIG_IGN : Terminate);
        const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
        if (const char* ready = std::getenv("SKY_FAKE_GAME_READY")) std::ofstream(ready) << pid;
        bool ended = false;
        for (uint64_t start = SDL_GetTicks(); SDL_GetTicks() - start < 15000 && !ended; SDL_Delay(10))
        {
#if defined(_WIN32)
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT) ended = true;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
#else
            ended = quit != 0;
#endif
        }
#if defined(_WIN32)
        DestroyWindow(window);
#endif
        if (!ended) return 4;
        if (const char* ready = std::getenv("SKY_FAKE_GAME_READY")) std::ofstream(std::string(ready) + ".closed") << pid;
        return 0;
    }
}
