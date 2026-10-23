#pragma once

#include "platform/platform.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Read-only view of the game disc's XDVDFS file system, read straight from the ISO.
class DiscFileSystem
{
public:
    struct Entry
    {
        uint64_t offset; // byte offset in the ISO
        uint32_t size;
        bool directory;
    };

    DiscFileSystem() = default;
    DiscFileSystem(const DiscFileSystem&) = delete;
    DiscFileSystem& operator=(const DiscFileSystem&) = delete;
    ~DiscFileSystem();

    bool Open(const char* isoPath);

    // `path` is relative to the disc root, any case, '\' or '/' separators. Empty means the root.
    const Entry* Find(const std::string& path) const;
    uint32_t Read(const Entry& entry, uint64_t position, void* buffer, uint32_t size) const;
    std::vector<uint8_t> ReadAll(const std::string& path) const;

    static std::string Normalize(std::string path);

private:
    PlatformFile m_file = InvalidPlatformFile;
    uint64_t m_partition = 0;
    std::unordered_map<std::string, Entry> m_entries;

    void IndexDirectory(uint32_t sector, uint32_t size, const std::string& prefix);
};

extern DiscFileSystem g_disc;
