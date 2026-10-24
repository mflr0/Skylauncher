#include "shared/platform/platform.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool PlatformProcessAlive(uint32_t pid)
{
    if (pid == 0)
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr)
        return GetLastError() == ERROR_ACCESS_DENIED; // it exists, it just isn't ours to look at
    DWORD code = 0;
    bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
}

#else
#include <cerrno>
#include <signal.h>

bool PlatformProcessAlive(uint32_t pid)
{
    // Signal 0 checks that the process exists without touching it; EPERM means it exists but belongs to someone else.
    return pid != 0 && (kill(pid_t(pid), 0) == 0 || errno == EPERM);
}
#endif
