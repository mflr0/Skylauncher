#include "shared/figures/figure_file.h"
#include <aes.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

namespace figures
{
    namespace
    {
        constexpr int BlockSize = 16;
        constexpr int BlockCount = int(FigureSize / BlockSize);
        constexpr int AreaStart[2] = { 0x08, 0x24 };
        constexpr int AreaBlocks = 0x1C; // 7 sectors

        // Appended to the key material of every data block (the leading and trailing spaces are part of it).
        constexpr char Copyright[] = " Copyright (C) 2010 Activision. All Rights Reserved. ";

        // Spyro's Adventure: experience needed for levels 2 to 10.
        constexpr uint32_t LevelExperience[] = { 1000, 2200, 3800, 6000, 9000, 13000, 18200, 24800, 33000 };

        bool IsTrailer(int block) { return block % 4 == 3; }
        bool IsEncrypted(int block) { return block >= 8 && !IsTrailer(block); }

        uint8_t* Block(FigureData& data, int block) { return data.data() + block * BlockSize; }
        const uint8_t* Block(const FigureData& data, int block) { return data.data() + block * BlockSize; }

        bool AllZero(const uint8_t* bytes, size_t size)
        {
            return std::all_of(bytes, bytes + size, [](uint8_t b) { return b == 0; });
        }

        uint16_t Get16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
        uint32_t Get24(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16); }
        uint32_t Get32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }
        void Put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }

        std::array<uint8_t, 16> BlockKey(const FigureData& data, int block)
        {
            uint8_t material[32 + 1 + sizeof(Copyright) - 1];
            memcpy(material, data.data(), 32);
            material[32] = uint8_t(block);
            memcpy(material + 33, Copyright, sizeof(Copyright) - 1);
            return Md5(material);
        }

        void DecryptBlock(const FigureData& data, int block, uint8_t* out)
        {
            memcpy(out, Block(data, block), BlockSize);
            if (!IsEncrypted(block) || AllZero(out, BlockSize))
                return;
            std::array<uint8_t, 16> key = BlockKey(data, block);
            AES_ctx context;
            AES_init_ctx(&context, key.data());
            AES_ECB_decrypt(&context, out);
        }

        void EncryptBlock(FigureData& data, int block, const uint8_t* plain)
        {
            uint8_t* out = Block(data, block);
            memcpy(out, plain, BlockSize);
            if (!IsEncrypted(block) || AllZero(out, BlockSize))
                return;
            std::array<uint8_t, 16> key = BlockKey(data, block);
            AES_ctx context;
            AES_init_ctx(&context, key.data());
            AES_ECB_encrypt(&context, out);
        }

        // One copy of the progress, decrypted: the area's data blocks in order, trailers skipped.
        struct Area
        {
            uint8_t blocks[AreaBlocks][BlockSize] = {};

            const uint8_t* At(int k) const { return blocks[k]; }
            uint8_t* At(int k) { return blocks[k]; }
        };

        Area ReadArea(const FigureData& data, int area)
        {
            Area result;
            for (int k = 0; k < AreaBlocks; k++)
                if (!IsTrailer(AreaStart[area] + k))
                    DecryptBlock(data, AreaStart[area] + k, result.At(k));
            return result;
        }

        void WriteArea(FigureData& data, int area, const Area& plain)
        {
            for (int k = 0; k < AreaBlocks; k++)
                if (!IsTrailer(AreaStart[area] + k))
                    EncryptBlock(data, AreaStart[area] + k, plain.At(k));
        }

        bool AreaIsZero(const Area& area)
        {
            return AllZero(&area.blocks[0][0], sizeof(area.blocks));
        }

        // The three checksums at the start of an area: +0x0E covers the first block itself (with 05 00 in its own
        // place), +0x0C the blocks 1, 2 and 4, +0x0A the blocks 5, 6, 8 and 9 padded with zeros to 0x110 bytes.
        uint16_t HeaderChecksum(const Area& area)
        {
            uint8_t bytes[BlockSize];
            memcpy(bytes, area.At(0), BlockSize);
            bytes[0x0E] = 0x05;
            bytes[0x0F] = 0x00;
            return Crc16(bytes);
        }

        uint16_t DataChecksum(const Area& area)
        {
            uint8_t bytes[3 * BlockSize];
            int blocks[] = { 1, 2, 4 };
            for (int i = 0; i < 3; i++)
                memcpy(bytes + i * BlockSize, area.At(blocks[i]), BlockSize);
            return Crc16(bytes);
        }

        uint16_t ExtraChecksum(const Area& area)
        {
            uint8_t bytes[0x110] = {};
            int blocks[] = { 5, 6, 8, 9 };
            for (int i = 0; i < 4; i++)
                memcpy(bytes + i * BlockSize, area.At(blocks[i]), BlockSize);
            return Crc16(bytes);
        }

        bool AreaValid(const Area& area)
        {
            const uint8_t* head = area.At(0);
            return Get16(head + 0x0E) == HeaderChecksum(area) && Get16(head + 0x0C) == DataChecksum(area) &&
                Get16(head + 0x0A) == ExtraChecksum(area);
        }

        void SealArea(Area& area)
        {
            uint8_t* head = area.At(0);
            Put16(head + 0x0A, ExtraChecksum(area));
            Put16(head + 0x0C, DataChecksum(area));
            Put16(head + 0x0E, HeaderChecksum(area));
        }

        // The area holding the current progress, -1 when neither copy checks out.
        int CurrentArea(const Area (&areas)[2], const bool (&valid)[2])
        {
            if (valid[0] && valid[1])
            {
                uint8_t ahead = uint8_t(areas[1].At(0)[0x09] - areas[0].At(0)[0x09]);
                return ahead != 0 && ahead < 0x80 ? 1 : 0;
            }
            return valid[0] ? 0 : valid[1] ? 1 : -1;
        }

        std::string Utf16ToUtf8(const uint16_t* units, size_t count)
        {
            std::string out;
            for (size_t i = 0; i < count && units[i] != 0; i++)
            {
                uint32_t c = units[i];
                if (c >= 0xD800 && c < 0xDC00 && i + 1 < count && units[i + 1] >= 0xDC00 && units[i + 1] < 0xE000)
                    c = 0x10000 + ((c - 0xD800) << 10) + (units[++i] - 0xDC00);
                if (c < 0x80)
                    out += char(c);
                else if (c < 0x800)
                    out += { char(0xC0 | c >> 6), char(0x80 | (c & 0x3F)) };
                else if (c < 0x10000)
                    out += { char(0xE0 | c >> 12), char(0x80 | (c >> 6 & 0x3F)), char(0x80 | (c & 0x3F)) };
                else
                    out += { char(0xF0 | c >> 18), char(0x80 | (c >> 12 & 0x3F)), char(0x80 | (c >> 6 & 0x3F)), char(0x80 | (c & 0x3F)) };
            }
            return out;
        }

        std::u16string Utf8ToUtf16(std::string_view text)
        {
            std::u16string out;
            for (size_t i = 0; i < text.size();)
            {
                uint8_t lead = uint8_t(text[i]);
                int extra = lead < 0x80 ? 0 : (lead >> 5) == 0x6 ? 1 : (lead >> 4) == 0xE ? 2 : 3;
                uint32_t c = extra == 0 ? lead : extra == 1 ? lead & 0x1F : extra == 2 ? lead & 0x0F : lead & 0x07;
                for (int k = 1; k <= extra && i + k < text.size(); k++)
                    c = c << 6 | (uint8_t(text[i + k]) & 0x3F);
                i += extra + 1;
                if (c >= 0x10000)
                {
                    c -= 0x10000;
                    out += char16_t(0xD800 + (c >> 10));
                    out += char16_t(0xDC00 + (c & 0x3FF));
                }
                else
                    out += char16_t(c);
            }
            return out;
        }

        // The nickname: 8 UTF-16 characters in block 2 of the area, 8 more in block 4.
        std::string ReadNickname(const Area& area)
        {
            uint16_t units[16];
            for (int i = 0; i < 8; i++)
            {
                units[i] = Get16(area.At(2) + i * 2);
                units[8 + i] = Get16(area.At(4) + i * 2);
            }
            return Utf16ToUtf8(units, 16);
        }

        // Key A of each sector: a fixed key for sector 0, else a CRC-48 of the UID and the sector number.
        uint64_t SectorKey(const FigureData& data, int sector)
        {
            if (sector == 0)
                return 73ull * 2017ull * 560381651ull;
            uint8_t bytes[5] = { data[0], data[1], data[2], data[3], uint8_t(sector) };
            uint64_t crc = 2ull * 2ull * 3ull * 1103ull * 12868356821ull;
            for (uint8_t byte : bytes)
            {
                crc ^= uint64_t(byte) << 40;
                for (int bit = 0; bit < 8; bit++)
                {
                    crc = (crc & 0x800000000000ull) ? (crc << 1) ^ 0x42F0E1EBA9EA3693ull : crc << 1;
                    crc &= 0xFFFFFFFFFFFFull;
                }
            }
            return crc;
        }
    }

    uint16_t Crc16(std::span<const uint8_t> bytes)
    {
        uint16_t crc = 0xFFFF;
        for (uint8_t byte : bytes)
        {
            crc ^= uint16_t(byte) << 8;
            for (int bit = 0; bit < 8; bit++)
                crc = (crc & 0x8000) ? uint16_t(crc << 1 ^ 0x1021) : uint16_t(crc << 1);
        }
        return crc;
    }

    std::array<uint8_t, 16> Md5(std::span<const uint8_t> bytes)
    {
        static const uint32_t Shift[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9,
            14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
            6, 10, 15, 21 };
        static const auto Constants = [] {
            std::array<uint32_t, 64> k{};
            for (int i = 0; i < 64; i++)
                k[i] = uint32_t(std::fabs(std::sin(double(i + 1))) * 4294967296.0);
            return k;
        }();

        std::vector<uint8_t> message(bytes.begin(), bytes.end());
        uint64_t bits = uint64_t(bytes.size()) * 8;
        message.push_back(0x80);
        while (message.size() % 64 != 56)
            message.push_back(0);
        for (int i = 0; i < 8; i++)
            message.push_back(uint8_t(bits >> (8 * i)));

        uint32_t h[4] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476 };
        for (size_t chunk = 0; chunk < message.size(); chunk += 64)
        {
            uint32_t w[16];
            for (int i = 0; i < 16; i++)
                w[i] = Get32(&message[chunk + i * 4]);
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
            for (int i = 0; i < 64; i++)
            {
                uint32_t f;
                int g;
                if (i < 16) { f = (b & c) | (~b & d); g = i; }
                else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
                else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
                else { f = c ^ (b | ~d); g = (7 * i) % 16; }
                uint32_t rotated = a + f + Constants[i] + w[g];
                a = d;
                d = c;
                c = b;
                b += rotated << Shift[i] | rotated >> (32 - Shift[i]);
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        }
        std::array<uint8_t, 16> digest;
        for (int i = 0; i < 16; i++)
            digest[i] = uint8_t(h[i / 4] >> (8 * (i % 4)));
        return digest;
    }

    int LevelFromExperience(uint32_t experience)
    {
        int level = 1;
        for (uint32_t needed : LevelExperience)
            if (experience >= needed)
                level++;
        return level;
    }

    FigureHeader ReadHeader(const FigureData& data)
    {
        FigureHeader header;
        header.uid = Get32(data.data());
        header.toyType = Get16(&data[0x10]);
        for (int i = 7; i >= 0; i--)
            header.tradingCard = header.tradingCard << 8 | data[0x14 + i];
        header.variantId = Get16(&data[0x1C]);
        header.checksumValid = Get16(&data[0x1E]) == Crc16(std::span(data.data(), 0x1E));
        return header;
    }

    FigureStats ReadStats(const FigureData& data)
    {
        FigureStats stats;
        Area areas[2] = { ReadArea(data, 0), ReadArea(data, 1) };
        if (AreaIsZero(areas[0]) && AreaIsZero(areas[1]))
            return stats; // Blank
        bool valid[2] = { !AreaIsZero(areas[0]) && AreaValid(areas[0]), !AreaIsZero(areas[1]) && AreaValid(areas[1]) };
        int current = CurrentArea(areas, valid);
        if (current < 0)
        {
            stats.state = FigureStats::State::Unreadable;
            return stats;
        }
        const Area& area = areas[current];
        stats.state = FigureStats::State::Valid;
        stats.area = current;
        stats.experience = Get24(area.At(0));
        stats.level = LevelFromExperience(stats.experience);
        stats.gold = Get16(area.At(0) + 0x03);
        stats.playSeconds = Get32(area.At(0) + 0x05);
        stats.sequence = area.At(0)[0x09];
        stats.upgrades = Get32(area.At(1));
        stats.nickname = ReadNickname(area);
        return stats;
    }

    FigureData CreateFigure(uint16_t toyType, uint16_t variantId)
    {
        FigureData data{};
        std::random_device random;
        for (int i = 0; i < 4; i++)
            data[i] = uint8_t(random());
        data[4] = data[0] ^ data[1] ^ data[2] ^ data[3]; // block check character
        data[5] = 0x81;                                  // ATQA
        data[6] = 0x01;
        data[7] = 0x0F;                                  // SAK
        Put16(&data[0x10], toyType);
        Put16(&data[0x1C], variantId);
        Put16(&data[0x1E], Crc16(std::span(data.data(), 0x1E)));

        for (int sector = 0; sector < BlockCount / 4; sector++)
        {
            uint8_t* trailer = Block(data, sector * 4 + 3);
            uint64_t key = SectorKey(data, sector);
            for (int i = 0; i < 6; i++)
                trailer[i] = uint8_t(key >> (8 * (5 - i)));
            static const uint8_t First[4] = { 0x0F, 0x0F, 0x0F, 0x69 }, Other[4] = { 0x7F, 0x0F, 0x08, 0x69 };
            memcpy(trailer + 6, sector == 0 ? First : Other, 4);
            // Key B (the last 6 bytes) stays zero.
        }
        return data;
    }

    bool SetNickname(FigureData& data, std::string_view utf8)
    {
        Area areas[2] = { ReadArea(data, 0), ReadArea(data, 1) };
        bool valid[2] = { !AreaIsZero(areas[0]) && AreaValid(areas[0]), !AreaIsZero(areas[1]) && AreaValid(areas[1]) };
        int current = CurrentArea(areas, valid);
        std::u16string units = Utf8ToUtf16(utf8);
        if (current < 0 || units.size() > 15)
            return false;

        Area next = areas[current];
        memset(next.At(2), 0, BlockSize);
        memset(next.At(4), 0, BlockSize);
        for (size_t i = 0; i < units.size(); i++)
            Put16((i < 8 ? next.At(2) : next.At(4)) + (i % 8) * 2, uint16_t(units[i]));
        next.At(0)[0x09] = uint8_t(next.At(0)[0x09] + 1);
        SealArea(next);
        WriteArea(data, 1 - current, next);
        return true;
    }

    void ResetProgress(FigureData& data)
    {
        Area empty;
        WriteArea(data, 0, empty);
        WriteArea(data, 1, empty);
    }
}
