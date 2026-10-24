#pragma once

// The part of the game runtime's platform layer the disc reader needs: read-only files for positional reads.
#include <cstdint>

// Read-only file for positional reads, safe to use from several threads at once. -1 is never a valid file.
using PlatformFile = intptr_t;
constexpr PlatformFile InvalidPlatformFile = -1;

// `path` is UTF-8.
PlatformFile PlatformOpenFile(const char* path);
// Bytes read (0 at end of file), -1 on error.
int64_t PlatformReadFile(PlatformFile file, void* buffer, uint32_t size, uint64_t offset);
void PlatformCloseFile(PlatformFile file);

// Whether a process with this id is running (a figure lock's owner, for one). False for 0.
bool PlatformProcessAlive(uint32_t pid);
