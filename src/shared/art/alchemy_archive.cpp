// See alchemy_archive.h. The formats are documented in games/giants/docs/shaders.md and games/giants/docs/art.md
// (reverse-engineered from the disc; the IGZ object tables cross-checked against the public notes of GiantsRecomp, MIT).
//
// The LZMA decoder below follows the public LZMA format description (range coder, literal / match / rep states); it
// decodes chunks of at most 0x8000 bytes into a buffer, so it needs no sliding window of its own.
#include "alchemy_archive.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace art::alchemy
{
    namespace
    {
        // --- LZMA --------------------------------------------------------------------------------------------------

    
        constexpr int LcBits = 3, LpBits = 0, PbBits = 2;
        constexpr int ProbBits = 11, MoveBits = 5;
        constexpr uint16_t ProbInit = 1 << (ProbBits - 1);
    
        struct RangeDecoder
        {
            const uint8_t* p;
            const uint8_t* end;
            uint32_t range = 0xFFFFFFFF;
            uint32_t code = 0;
    
            uint8_t Next() { return p < end ? *p++ : (p++, uint8_t(0)); }
    
            void Init()
            {
                Next(); // the first byte of a range coder stream is 0
                for (int i = 0; i < 4; i++)
                    code = (code << 8) | Next();
            }
    
            void Normalize()
            {
                if (range < (1u << 24))
                {
                    range <<= 8;
                    code = (code << 8) | Next();
                }
            }
    
            unsigned Bit(uint16_t& prob)
            {
                uint32_t bound = (range >> ProbBits) * prob;
                unsigned bit;
                if (code < bound)
                {
                    range = bound;
                    prob += ((1 << ProbBits) - prob) >> MoveBits;
                    bit = 0;
                }
                else
                {
                    range -= bound;
                    code -= bound;
                    prob -= prob >> MoveBits;
                    bit = 1;
                }
                Normalize();
                return bit;
            }
    
            unsigned Direct(int count)
            {
                unsigned result = 0;
                while (count--)
                {
                    range >>= 1;
                    code -= range;
                    uint32_t t = 0u - (code >> 31);
                    code += range & t;
                    result = (result << 1) + (t + 1);
                    Normalize();
                }
                return result;
            }
    
            unsigned Tree(uint16_t* probs, int bits)
            {
                unsigned m = 1;
                for (int i = 0; i < bits; i++)
                    m = (m << 1) + Bit(probs[m]);
                return m - (1u << bits);
            }
    
            unsigned ReverseTree(uint16_t* probs, int bits)
            {
                unsigned m = 1, symbol = 0;
                for (int i = 0; i < bits; i++)
                {
                    unsigned bit = Bit(probs[m]);
                    m = (m << 1) + bit;
                    symbol |= bit << i;
                }
                return symbol;
            }
        };
    
        struct LengthDecoder
        {
            uint16_t choice, choice2;
            uint16_t low[16][8], mid[16][8], high[256];
    
            void Init()
            {
                choice = choice2 = ProbInit;
                for (auto& row : low)
                    std::fill(std::begin(row), std::end(row), ProbInit);
                for (auto& row : mid)
                    std::fill(std::begin(row), std::end(row), ProbInit);
                std::fill(std::begin(high), std::end(high), ProbInit);
            }
    
            unsigned Decode(RangeDecoder& rc, unsigned posState)
            {
                if (!rc.Bit(choice))
                    return rc.Tree(low[posState], 3);
                if (!rc.Bit(choice2))
                    return 8 + rc.Tree(mid[posState], 3);
                return 16 + rc.Tree(high, 8);
            }
        };
    
        constexpr int States = 12, PosStates = 1 << PbBits, LenToPosStates = 4, EndPosModel = 14, FullDistances = 1 << (EndPosModel >> 1),
            AlignBits = 4;
    
        struct LzmaState
        {
            uint16_t isMatch[States][PosStates], isRep[States], isRepG0[States], isRepG1[States], isRepG2[States],
                isRep0Long[States][PosStates];
            uint16_t posSlot[LenToPosStates][64], posSpecial[1 + FullDistances - EndPosModel], align[1 << AlignBits];
            LengthDecoder len, repLen;
            uint16_t literal[0x300 << (LcBits + LpBits)];
    
            void Init()
            {
                auto fill = [](auto& array) {
                    uint16_t* begin = reinterpret_cast<uint16_t*>(&array);
                    std::fill(begin, begin + sizeof(array) / sizeof(uint16_t), ProbInit);
                };
                fill(isMatch);
                fill(isRep);
                fill(isRepG0);
                fill(isRepG1);
                fill(isRepG2);
                fill(isRep0Long);
                fill(posSlot);
                fill(posSpecial);
                fill(align);
                fill(literal);
                len.Init();
                repLen.Init();
            }
        };

        uint32_t Le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
        uint32_t ReadBe32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

        constexpr size_t ChunkSize = 0x8000;
        const uint8_t LzmaProps[5] = { 0x5D, 0x00, 0x80, 0x00, 0x00 };
    }

    size_t LzmaDecodeRaw(const uint8_t* input, size_t inputSize, uint8_t* out, size_t size)
    {
        static thread_local LzmaState state;
        state.Init();
        RangeDecoder rc{ input, input + inputSize };
        rc.Init();

        unsigned st = 0;
        uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
        size_t pos = 0;
        constexpr unsigned PosMask = PosStates - 1;
        while (pos < size)
        {
            unsigned posState = unsigned(pos) & PosMask;
            if (!rc.Bit(state.isMatch[st][posState]))
            {
                unsigned prev = pos ? out[pos - 1] : 0;
                uint16_t* probs = state.literal + 0x300 * (((unsigned(pos) & ((1u << LpBits) - 1)) << LcBits) + (prev >> (8 - LcBits)));
                unsigned symbol = 1;
                if (st >= 7)
                {
                    unsigned matchByte = pos > rep0 ? out[pos - rep0 - 1] : 0;
                    do
                    {
                        unsigned matchBit = (matchByte >> 7) & 1;
                        matchByte <<= 1;
                        unsigned bit = rc.Bit(probs[((1 + matchBit) << 8) + symbol]);
                        symbol = (symbol << 1) | bit;
                        if (matchBit != bit)
                            break;
                    } while (symbol < 0x100);
                }
                while (symbol < 0x100)
                    symbol = (symbol << 1) | rc.Bit(probs[symbol]);
                out[pos++] = uint8_t(symbol);
                st = st < 4 ? 0 : st < 10 ? st - 3 : st - 6;
                continue;
            }

            unsigned length;
            if (rc.Bit(state.isRep[st]))
            {
                if (pos == 0)
                    return pos;
                if (!rc.Bit(state.isRepG0[st]))
                {
                    if (!rc.Bit(state.isRep0Long[st][posState]))
                    {
                        st = st < 7 ? 9 : 11;
                        if (pos <= rep0)
                            return pos;
                        out[pos] = out[pos - rep0 - 1];
                        pos++;
                        continue;
                    }
                }
                else
                {
                    uint32_t distance;
                    if (!rc.Bit(state.isRepG1[st]))
                        distance = rep1;
                    else
                    {
                        if (!rc.Bit(state.isRepG2[st]))
                            distance = rep2;
                        else
                        {
                            distance = rep3;
                            rep3 = rep2;
                        }
                        rep2 = rep1;
                    }
                    rep1 = rep0;
                    rep0 = distance;
                }
                length = state.repLen.Decode(rc, posState);
                st = st < 7 ? 8 : 11;
            }
            else
            {
                rep3 = rep2;
                rep2 = rep1;
                rep1 = rep0;
                length = state.len.Decode(rc, posState);
                st = st < 7 ? 7 : 10;
                unsigned slot = rc.Tree(state.posSlot[std::min<unsigned>(length, LenToPosStates - 1)], 6);
                if (slot < 4)
                    rep0 = slot;
                else
                {
                    unsigned direct = (slot >> 1) - 1;
                    rep0 = (2 | (slot & 1)) << direct;
                    if (slot < EndPosModel)
                    {
                        // Reverse tree over posSpecial, based at rep0 - slot.
                        uint16_t* probs = state.posSpecial + rep0 - slot;
                        unsigned m = 1;
                        for (unsigned i = 0; i < direct; i++)
                        {
                            unsigned bit = rc.Bit(probs[m]);
                            m = (m << 1) + bit;
                            rep0 |= bit << i;
                        }
                    }
                    else
                    {
                        rep0 += rc.Direct(int(direct - AlignBits)) << AlignBits;
                        rep0 += rc.ReverseTree(state.align, AlignBits);
                        if (rep0 == 0xFFFFFFFF)
                            return pos; // end marker
                    }
                }
            }
            length += 2;
            if (rep0 >= pos)
                return pos;
            for (unsigned i = 0; i < length && pos < size; i++, pos++)
                out[pos] = out[pos - rep0 - 1];
        }
        return pos;
    }

    // --- IGA ----------------------------------------------------------------------------------------------------------

    namespace
    {
        // [Alchemy] Where each IGA version keeps its tables. The file starts "IGA\x1a", u32 version, u32 table size,
        // u32 entry count; then the header's other fields, u32 name hashes[n] and the entries { u32 offset, u32 size,
        // u32 mode }. v8 is Giants' [V: every archive of its disc]; v10 and v11 (the later games) are from public format
        // descriptions, never seen here [?]: v10 adds the counts of large, medium and small chunk blocks before the name
        // table, v11 makes the name table's position 64-bit.
        struct IgaLayout
        {
            uint32_t version;
            uint32_t header;     // header size: the name hashes follow it
            uint32_t namesAt;    // header offset of the name table's file position
            uint32_t namesSize;  // ... and of its size
            bool namesAt64;      // the position is 64-bit (little-endian)
            bool verified;
        };
        constexpr IgaLayout IgaLayouts[] = {
            { 8, 0x34, 0x1C, 0x20, false, true },
            { 10, 0x34, 0x28, 0x2C, false, false },
            { 11, 0x38, 0x28, 0x30, true, false },
        };
    }

    std::vector<FormatVersion> IgaVersions()
    {
        std::vector<FormatVersion> out;
        for (const IgaLayout& layout : IgaLayouts)
            out.push_back({ layout.version, layout.verified });
        return out;
    }

    std::optional<IgaArchive> IgaArchive::Open(Reader reader, uint64_t length)
    {
        uint8_t header[0x38] = {};
        if (length < 0x34 || !reader(0, size_t(std::min<uint64_t>(length, sizeof(header))), header) || memcmp(header, "IGA\x1a", 4) != 0)
            return std::nullopt;
        const IgaLayout* layout = nullptr;
        for (const IgaLayout& candidate : IgaLayouts)
            if (Le32(header + 4) == candidate.version)
                layout = &candidate;
        if (layout == nullptr || length < layout->header)
            return std::nullopt;
        uint32_t count = Le32(header + 0x0C), namesSize = Le32(header + layout->namesSize);
        uint64_t namesAt = Le32(header + layout->namesAt);
        if (layout->namesAt64)
            namesAt |= uint64_t(Le32(header + layout->namesAt + 4)) << 32;
        if (count == 0 || count > 0x100000 || layout->header + 16 * uint64_t(count) > length)
            return std::nullopt;
        // u32 hashes[n], then n x { u32 offset, u32 size, u32 mode }.
        std::vector<uint8_t> tables(16 * size_t(count));
        if (!reader(layout->header, tables.size(), tables.data()))
            return std::nullopt;
        IgaArchive archive;
        archive.m_reader = std::move(reader);
        archive.m_length = length;
        archive.m_version = layout->version;
        archive.m_verified = layout->verified;
        archive.m_entries.resize(count);
        for (uint32_t i = 0; i < count; i++)
        {
            const uint8_t* entry = &tables[4 * size_t(count) + 12 * size_t(i)];
            archive.m_entries[i] = { Le32(entry), Le32(entry + 4), Le32(entry + 8), {} };
        }
        // The name table: u32 offsets[n] from the table's start, then the NUL-terminated names.
        if (namesSize >= 4 * uint64_t(count) && namesSize < 0x4000000 && namesAt + namesSize <= length)
        {
            std::vector<uint8_t> names(namesSize);
            if (archive.m_reader(namesAt, names.size(), names.data()))
            {
                for (uint32_t i = 0; i < count; i++)
                {
                    uint32_t at = Le32(&names[4 * size_t(i)]);
                    if (at >= names.size())
                        continue;
                    const uint8_t* end = static_cast<const uint8_t*>(memchr(&names[at], 0, names.size() - at));
                    if (end != nullptr)
                        archive.m_entries[i].name.assign(reinterpret_cast<const char*>(&names[at]), reinterpret_cast<const char*>(end));
                }
            }
        }
        return archive;
    }

    std::vector<uint8_t> IgaArchive::Read(size_t index, size_t size) const
    {
        const Entry& entry = m_entries[index];
        size = size == 0 ? entry.size : std::min<size_t>(size, entry.size);
        auto read = [&](uint64_t offset, size_t count) {
            std::vector<uint8_t> out(count, 0); // bytes past the end read as zero (the LZMA decoder looks a few bytes beyond)
            if (offset < m_length)
            {
                size_t available = size_t(std::min<uint64_t>(count, m_length - offset));
                if (!m_reader(offset, available, out.data()))
                    out.clear();
            }
            return out;
        };
        if (entry.flags == 0xFFFFFFFF)
            return read(entry.offset, size);
        // The mode's top 4 bits are the compression: 2 is LZMA [V: v8]. The later versions also have other ones (zlib,
        // LZ4 [?]), which this reader does not decode.
        if (!m_verified && entry.flags >> 28 != 2)
            return {};

        // Chunked: 0x8000 bytes of output per chunk, each chunk at a 0x100-aligned offset, [u16 n][LZMA props][n bytes]
        // or stored (a chunk that doesn't start with the props).
        std::vector<uint8_t> out;
        out.reserve(size);
        uint64_t p = entry.offset;
        while (out.size() < size)
        {
            size_t want = std::min(ChunkSize, size - out.size());
            std::vector<uint8_t> head = read(p, 7);
            if (head.size() < 7)
                return {};
            size_t n = head[0] | (head[1] << 8);
            if (memcmp(&head[2], LzmaProps, 5) == 0)
            {
                std::vector<uint8_t> body = read(p + 7, n + 8);
                size_t at = out.size();
                out.resize(at + want);
                if (body.empty() || LzmaDecodeRaw(body.data(), body.size(), out.data() + at, want) != want)
                    return {};
                p = (p + 7 + n + 0xFF) & ~uint64_t(0xFF);
            }
            else
            {
                std::vector<uint8_t> raw = read(p, want);
                if (raw.empty())
                    return {};
                out.insert(out.end(), raw.begin(), raw.end());
                p = (p + want + 0xFF) & ~uint64_t(0xFF);
            }
        }
        return out;
    }

    std::optional<size_t> IgaArchive::Find(const std::string& part) const
    {
        auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        };
        std::string wanted = lower(part);
        for (size_t i = 0; i < m_entries.size(); i++)
            if (lower(m_entries[i].name).find(wanted) != std::string::npos)
                return i;
        return std::nullopt;
    }

    // --- IGZ ----------------------------------------------------------------------------------------------------------

    namespace
    {
        // [Alchemy] What changes between IGZ versions, as far as pictures go. The file starts "IGZ\x01" and a BE32
        // version, then a table of sections (section 0 holds the object tables, the "fixups"; the others are memory
        // pools). v6 is Giants' [V: every archive of its disc]. v7 to v9 (the later games) are from public format
        // descriptions, never seen here [?]: fixup records named by a four-character tag (TMET, EXID, RVTB, RHND...)
        // with a 16-byte header from the section's start, sections listed as { id, offset, size, alignment }, a
        // pointer's pool in its top 5 bits; igImage2's fields are assumed where v6 has them.
        struct IgzLayout
        {
            uint32_t version;
            uint32_t sectionOffset, sectionSize; // word index of a section entry's offset and size
            bool tagged;                         // records named by a four-character tag (else a number)
            bool ownHeader;                      // section 0 starts with its own header, its length at +0x0C (v6)
            uint32_t countAt, lengthAt, startAt; // a record's count, length and data start (from the record)
            uint32_t tables[4];                  // types, external ids, objects, memory handles: v6's numbers or tags
            uint32_t poolShift;                  // a pointer's pool index is above this bit
            uint32_t imageName, imageWidth, imageFormat, imageHandle, imageSize; // igImage2's fields
            bool verified;
        };
        enum Table { Types, ExternalIds, Objects, Handles };
        constexpr uint32_t Tag(const char (&t)[5]) { return uint32_t(uint8_t(t[0])) << 24 | uint32_t(uint8_t(t[1])) << 16 | uint32_t(uint8_t(t[2])) << 8 | uint8_t(t[3]); }
        constexpr uint32_t TaggedTables[4] = { Tag("TMET"), Tag("EXID"), Tag("RVTB"), Tag("RHND") };
        constexpr IgzLayout IgzLayouts[] = {
            { 6, 0, 1, false, true, 12, 16, 20, { 0, 2, 5, 0xA }, 24, 0x08, 0x30, 0x3C, 0x48, 0x60, true },
            { 7, 1, 2, true, false, 4, 8, 12, { TaggedTables[0], TaggedTables[1], TaggedTables[2], TaggedTables[3] }, 27, 0x08, 0x30, 0x3C, 0x48, 0x60, false },
            { 8, 1, 2, true, false, 4, 8, 12, { TaggedTables[0], TaggedTables[1], TaggedTables[2], TaggedTables[3] }, 27, 0x08, 0x30, 0x3C, 0x48, 0x60, false },
            { 9, 1, 2, true, false, 4, 8, 12, { TaggedTables[0], TaggedTables[1], TaggedTables[2], TaggedTables[3] }, 27, 0x08, 0x30, 0x3C, 0x48, 0x60, false },
        };
    }

    std::vector<FormatVersion> IgzVersions()
    {
        std::vector<FormatVersion> out;
        for (const IgzLayout& layout : IgzLayouts)
            out.push_back({ layout.version, layout.verified });
        return out;
    }

    uint32_t IgzFile::Version() const { return IgzLayouts[m_layout].version; }
    bool IgzFile::Verified() const { return IgzLayouts[m_layout].verified; }

    std::optional<IgzFile> IgzFile::Parse(std::vector<uint8_t> bytes)
    {
        if (bytes.size() < 0x800 || memcmp(bytes.data(), "IGZ\x01", 4) != 0)
            return std::nullopt;
        IgzFile file;
        file.m_layout = SIZE_MAX;
        for (size_t i = 0; i < std::size(IgzLayouts); i++)
            if (IgzLayouts[i].version == ReadBe32(&bytes[4]))
                file.m_layout = i;
        if (file.m_layout == SIZE_MAX)
            return std::nullopt;
        const IgzLayout& layout = IgzLayouts[file.m_layout];
        file.m_bytes = std::move(bytes);
        const std::vector<uint8_t>& b = file.m_bytes;
        // Sections at 0x10, 16 bytes each, until an empty one.
        std::vector<std::pair<uint64_t, uint64_t>> sections;
        for (int i = 0; i < 16; i++)
        {
            uint32_t offset = ReadBe32(&b[0x10 + 16 * i + 4 * layout.sectionOffset]), size = ReadBe32(&b[0x10 + 16 * i + 4 * layout.sectionSize]);
            if (offset == 0 || size == 0)
                break;
            sections.emplace_back(offset, size);
        }
        if (sections.size() < 2 || sections[0].first + sections[0].second > b.size() || sections[0].second < 0x1C)
            return std::nullopt;
        for (size_t i = 1; i < sections.size(); i++)
            file.m_pools.push_back(sections[i].first);

        // Section 0: (v6: its own header, its length at +0x0C), then records { id, ..., count, length, data start }, the
        // data start counted from the record.
        size_t base = size_t(sections[0].first), end = base + size_t(sections[0].second);
        const uint32_t header = layout.startAt + 4;
        for (size_t p = base + (layout.ownHeader ? ReadBe32(&b[base + 0x0C]) : 0); p + header <= end;)
        {
            uint32_t id = ReadBe32(&b[p]), count = ReadBe32(&b[p + layout.countAt]), length = ReadBe32(&b[p + layout.lengthAt]),
                     start = ReadBe32(&b[p + layout.startAt]);
            if (length < header || p + length > end || start > length)
                break;
            for (uint32_t table = 0; table < 4; table++)
                if (id == layout.tables[table])
                    file.m_fixups.push_back({ table, Fixup{ count, p + start, p + length } });
            p += length;
        }
        // The class names, one NUL-terminated string each: an object's first word is its index here.
        if (const Fixup* types = file.FindFixup(Types))
        {
            size_t p = types->start;
            for (uint32_t i = 0; i < types->count && p < types->end; i++)
            {
                std::string name = file.String(p);
                p += name.size() + 1;
                file.m_types.push_back(std::move(name));
            }
        }
        if (file.m_types.empty() || file.FindFixup(Objects) == nullptr)
            return std::nullopt;
        return file;
    }

    uint32_t IgzFile::Be32(uint64_t at) const
    {
        return at + 4 <= m_bytes.size() ? ReadBe32(&m_bytes[size_t(at)]) : 0;
    }

    std::optional<uint64_t> IgzFile::Pointer(uint32_t value) const
    {
        // A serialized pointer: its pool in the top bits (v6: 8; pool n is section n + 1), the offset in the pool below.
        const uint32_t shift = IgzLayouts[m_layout].poolShift;
        uint32_t pool = value >> shift;
        if (pool >= m_pools.size())
            return std::nullopt;
        return m_pools[pool] + (value & ((1u << shift) - 1));
    }

    std::string IgzFile::String(uint64_t at) const
    {
        std::string out;
        while (at < m_bytes.size() && m_bytes[size_t(at)] != 0 && out.size() < 512)
            out += char(m_bytes[size_t(at++)]);
        return out;
    }

    const IgzFile::Fixup* IgzFile::FindFixup(uint32_t type) const
    {
        for (const auto& [id, fixup] : m_fixups)
            if (id == type)
                return &fixup;
        return nullptr;
    }

    std::vector<uint64_t> IgzFile::ObjectOffsets() const
    {
        // The object offsets: 4-bit nibbles, low one first (3 data bits and a continue bit, least significant group
        // first), each value the distance to the previous object in 4-byte units minus one.
        std::vector<uint64_t> out;
        const Fixup* objects = FindFixup(Objects);
        if (objects == nullptr)
            return out;
        size_t position = objects->start;
        bool high = false;
        auto nibble = [&]() -> uint32_t {
            if (position >= objects->end)
                return 0;
            uint32_t value = high ? m_bytes[position++] >> 4 : m_bytes[position] & 0xF;
            high = !high;
            return value;
        };
        uint32_t previous = 0;
        for (uint32_t i = 0; i < objects->count && position < objects->end; i++)
        {
            uint32_t current = nibble(), value = current & 7;
            for (int shift = 3; current & 8; shift += 3)
            {
                current = nibble();
                value |= (current & 7) << shift;
            }
            previous += value * 4 + 4;
            if (auto at = Pointer(previous))
                out.push_back(*at);
        }
        return out;
    }

    std::string IgzFile::TypeName(uint64_t object) const
    {
        uint32_t type = Be32(object);
        return type < m_types.size() ? m_types[type] : std::string();
    }

    std::vector<IgzFile::Picture> IgzFile::Pictures() const
    {
        std::vector<Picture> out;
        const IgzLayout& layout = IgzLayouts[m_layout];
        const uint32_t ImageName = layout.imageName, ImageWidth = layout.imageWidth, ImageFormat = layout.imageFormat;
        for (uint64_t object : ObjectOffsets())
        {
            uint32_t type = Be32(object);
            if (type >= m_types.size() || m_types[type] != "igImage2" || object + layout.imageSize > m_bytes.size())
                continue;
            Picture picture;
            picture.object = object;
            uint32_t size = Be32(object + ImageWidth);
            picture.width = size >> 16;
            picture.height = size & 0xFFFF;
            if (auto name = Pointer(Be32(object + ImageName)); name && Be32(object + ImageName) != 0)
                picture.name = String(*name);
            // The format: 0x80000000 | an index in the external ids, whose first word is the format name's hash.
            uint32_t format = Be32(object + ImageFormat);
            const Fixup* ids = FindFixup(ExternalIds);
            if (ids != nullptr && (format & 0x80000000) && (format & 0x7FFFFFFF) < ids->count)
                picture.format = Be32(ids->start + 8 * uint64_t(format & 0x7FFFFFFF));
            out.push_back(std::move(picture));
        }
        return out;
    }

    std::vector<uint8_t> IgzFile::Pixels(const Picture& picture) const
    {
        // The pixels are a memory handle: { u32 size | flags << 28, u32 pointer }, by index.
        const Fixup* handles = FindFixup(Handles);
        uint32_t handle = Be32(picture.object + IgzLayouts[m_layout].imageHandle);
        if (handles == nullptr || handle >= handles->count)
            return {};
        uint64_t record = handles->start + 8 * uint64_t(handle);
        uint32_t size = Be32(record) & 0x0FFFFFFF;
        auto at = Pointer(Be32(record + 4));
        if (!at || size == 0 || *at + size > m_bytes.size())
            return {};
        return std::vector<uint8_t>(m_bytes.begin() + ptrdiff_t(*at), m_bytes.begin() + ptrdiff_t(*at + size));
    }

    bool TextureFormatOf(uint32_t hash, Texture& texture)
    {
        // Format names hashed by the engine (external ids). Seen on Giants' disc (2026-10-25, every archive): B9BCDC57
        // (30315 pictures, DXT5 tiled), DC78A2E3 (27947, DXT1 tiled), 19E29222 (132, 8 bits per texel, tiled:
        // "format_tfl8" lookup ramps). The linear and DXT3 ones are from public format lists, not seen on this disc.
        struct Known
        {
            uint32_t hash;
            TextureFormat format;
            bool tiled;
        };
        static constexpr Known Formats[] = {
            { 0xB9BCDC57, TextureFormat::Dxt5, true }, { 0xDC78A2E3, TextureFormat::Dxt1, true }, { 0x19E29222, TextureFormat::L8, true },
            { 0x50A19F21, TextureFormat::Dxt3, true }, { 0x049D7FAA, TextureFormat::Dxt5, false }, { 0x93566F0B, TextureFormat::Dxt5, false },
            { 0xD936ADEE, TextureFormat::Dxt1, false }, { 0xCC82E927, TextureFormat::Dxt1, false }, { 0xCBBED02C, TextureFormat::Dxt3, false },
            { 0xEABABF45, TextureFormat::Dxt3, false },
        };
        for (const Known& known : Formats)
        {
            if (known.hash != hash)
                continue;
            texture.format = known.format;
            texture.tiled = known.tiled;
            texture.endian = known.format == TextureFormat::L8 ? TextureEndian::None : TextureEndian::In16;
            texture.pitch = 0;
            return true;
        }
        return false;
    }

    bool DecodePicture(const IgzFile& file, const IgzFile::Picture& picture, Image& out)
    {
        Texture texture;
        if (!TextureFormatOf(picture.format, texture))
            return false;
        texture.width = picture.width;
        texture.height = picture.height;
        std::vector<uint8_t> pixels = file.Pixels(picture);
        if (pixels.empty() || !DecodeTexture(pixels.data(), pixels.size(), texture, out))
            return false;
        FlipVertically(out);
        return true;
    }
}
