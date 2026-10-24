#pragma once

// A figure file: the 1 KB memory of the toy's MIFARE Classic tag, as the portal reads it (.sky, .bin and .dmp dumps all
// use this layout).
//
//   blocks 0-1   header, never encrypted: tag UID, toy type (+0x10), trading card id (+0x14), variant (+0x1C) and a
//                CRC-16 of the first 30 bytes (+0x1E)
//   every 4th    sector trailers (keys and access bits), never encrypted
//   blocks 8+    two copies of the toy's progress ("areas", at blocks 0x08 and 0x24): the game writes the next save into
//                the older copy and bumps its sequence number, so a cut-off write never loses the figure
//
// Data blocks are AES-128-ECB encrypted with a per-block key: MD5 of blocks 0-1, the block number and Activision's
// copyright line. An all-zero block is stored as zeros (a new toy has never been written).
//
// Progress is only trusted when every checksum of an area matches; anything else is reported as unreadable and never
// rewritten, so an unexpected layout can't be damaged by the launcher.
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace figures
{
    constexpr size_t FigureSize = 1024;
    using FigureData = std::array<uint8_t, FigureSize>;

    struct FigureHeader
    {
        uint32_t uid = 0;          // the tag's 4-byte UID, as stored (byte 0 lowest)
        uint16_t toyType = 0;
        uint16_t variantId = 0;
        uint64_t tradingCard = 0;
        bool checksumValid = false;
    };

    struct FigureStats
    {
        enum class State : uint8_t { Blank, Valid, Unreadable };
        State state = State::Blank;   // Blank: never played (a new toy); Unreadable: checksums don't match
        uint32_t experience = 0;
        int level = 1;                // from the experience (Spyro's Adventure's 10 levels)
        uint16_t gold = 0;
        uint32_t playSeconds = 0;
        uint32_t upgrades = 0;        // upgrade flags
        uint16_t hat = 0;             // 0: none (Spyro's Adventure has no hats)
        std::string nickname;         // UTF-8, empty when the player never named it
        uint8_t sequence = 0;
        int area = -1;                // the copy the stats come from
    };

    FigureHeader ReadHeader(const FigureData& data);
    FigureStats ReadStats(const FigureData& data);

    // A new toy: the header for `toyType` and `variantId` with a random UID, the sector keys, and no progress yet.
    FigureData CreateFigure(uint16_t toyType, uint16_t variantId);

    // Renames the toy: writes the nickname into the older copy of its progress and makes it the current one.
    // Only for a figure whose progress reads as Valid; at most 15 characters. False when it can't be written.
    bool SetNickname(FigureData& data, std::string_view utf8);

    // Wipes the progress (both copies): the toy is back to how it left the box.
    void ResetProgress(FigureData& data);

    int LevelFromExperience(uint32_t experience);

    // Building blocks, exposed for the tests.
    uint16_t Crc16(std::span<const uint8_t> bytes);           // CRC-16/CCITT-FALSE
    std::array<uint8_t, 16> Md5(std::span<const uint8_t> bytes);
}
