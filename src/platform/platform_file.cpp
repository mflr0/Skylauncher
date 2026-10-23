#include "platform.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

namespace
{
    std::wstring Widen(const char* text)
    {
        int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
        if (length > 0)
            MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), length);
        return wide;
    }
}

PlatformFile PlatformOpenFile(const char* path)
{
    HANDLE file = CreateFile2(Widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
    return reinterpret_cast<PlatformFile>(file); // INVALID_HANDLE_VALUE is -1
}

int64_t PlatformReadFile(PlatformFile file, void* buffer, uint32_t size, uint64_t offset)
{
    // On a synchronous handle, ReadFile reads at the OVERLAPPED offset, so concurrent reads don't share a file pointer.
    OVERLAPPED overlapped{};
    overlapped.Offset = uint32_t(offset);
    overlapped.OffsetHigh = uint32_t(offset >> 32);
    DWORD read = 0;
    if (!ReadFile(reinterpret_cast<HANDLE>(file), buffer, size, &read, &overlapped))
        return GetLastError() == ERROR_HANDLE_EOF ? 0 : -1;
    return read;
}

void PlatformCloseFile(PlatformFile file)
{
    CloseHandle(reinterpret_cast<HANDLE>(file));
}

#else
#include <fcntl.h>
#include <unistd.h>

PlatformFile PlatformOpenFile(const char* path)
{
    return open(path, O_RDONLY);
}

int64_t PlatformReadFile(PlatformFile file, void* buffer, uint32_t size, uint64_t offset)
{
    return pread(int(file), buffer, size, off_t(offset));
}

void PlatformCloseFile(PlatformFile file)
{
    close(int(file));
}
#endif
