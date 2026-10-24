#pragma once

// Reading and editing what Spyro's Adventure stores on a figure. Layout (docs/figures.md):
// - sector 0 (blocks 0-3) is plain: serial, toy type, variant, header CRC; blocks 4-7 are plain and unused;
// - blocks 8-63 except the sector trailers are AES-128 encrypted, each with its own key:
//   MD5(block 0 | block 1 | block number | " Copyright (C) 2010 Activision. All Rights Reserved. "); zero blocks stay zero;
// - the progress is stored twice, in area A (blocks 8-16) and area B (blocks 36-44), 7 data blocks each; the title
//   writes the area it did not read last, with the sequence number + 1, so a write cut short keeps the other one.
#include "figure_data.h"
#include <array>
#include <cstdint>
#include <string>

namespace figures
{
    // Plain (decrypted) image of a tag; sector 0 and trailers are copied as they are.
    FigureData Decrypt(const FigureData& tag);
    FigureData Encrypt(const FigureData& plain);

    uint16_t ToyType(const FigureData& tag);
    uint16_t Variant(const FigureData& tag);

    // Puts the MIFARE key A of every sector into its trailer, as tag dumps store it.
    void WriteSectorKeys(FigureData& tag);

    // A never-played figure of the given toy type, as a complete dump (keys included).
    FigureData CreateFigure(uint16_t toyType, uint16_t variant = 0);

    struct Date
    {
        uint8_t minute = 0, hour = 0, day = 0, month = 0;
        uint16_t year = 0;
    };

    // Progress fields of one area. Offsets are from the area's first block, trailers included.
    struct FigureStats
    {
        uint32_t experience = 0;       // 0x00, 24 bits
        uint16_t gold = 0;             // 0x03
        uint32_t playSeconds = 0;      // 0x05
        uint8_t sequence = 0;          // 0x09, which area is newer
        uint16_t upgrades = 0;         // 0x10, bit field of bought upgrades and the chosen path
        uint8_t platforms = 0;         // 0x13, consoles the figure was used on (2 = Xbox 360)
        uint16_t hat = 0;              // 0x14 (hats exist from Giants on)
        std::array<uint8_t, 8> owner{}; // 0x18, the first owner's id, written by the title
        std::u16string nickname;       // 0x20 and 0x40, 16 UTF-16 code units, zero padded
        Date lastPlaced;               // 0x50
        uint32_t heroicChallenges = 0; // 0x56, one bit per challenge
        uint16_t heroPoints = 0;       // 0x5A
        Date lastReset;                // 0x60
    };

    enum class AreaState { Blank, Valid, Damaged };

    // Reads the newest area whose checksums are right. Blank: never written (the title will initialise it).
    // Damaged: data present but no area passes its checksums.
    AreaState ReadStats(const FigureData& tag, FigureStats& stats);

    // Writes `stats` into both areas (sequence numbers stats.sequence + 1 and + 2) with fresh checksums.
    void WriteStats(FigureData& tag, const FigureStats& stats);

    // Experience needed for each level (index 0 = level 1); the title caps characters at level 10.
    constexpr int MaxLevel = 10;
    extern const uint32_t LevelExperience[MaxLevel];
    int LevelForExperience(uint32_t experience);

    constexpr uint16_t MaxGold = 65000;

    std::string NicknameUtf8(const FigureStats& stats);
    void SetNicknameUtf8(FigureStats& stats, const std::string& text); // cut to 16 UTF-16 units
}
