#ifndef BUDDY_H
#define BUDDY_H

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <vector>

namespace Buddy {

struct Shift16
{
    uint8_t sh{0};

    Shift16() = default;
    consteval Shift16(size_t n)
    {
        sh = 0;
        while (n > 16) {
            ++sh;
            n >>= 1;
        }
        if (n != 16) throw; // too small or not a power of two
    }

    static Shift16 FromInt(uint8_t sh)
    {
        return Shift16{}.set(sh);
    }

    Shift16 operator+(uint8_t x)
    {
        return FromInt(sh+x);
    }

    Shift16& operator++() { ++sh; return *this; }
    friend bool operator==(const Shift16& l, const Shift16& r)
    {
        return l.sh == r.sh;
    }

    friend Shift16 operator-(const Shift16& s16, uint8_t x)
    {
        return Shift16::FromInt(s16.sh - x);
    }

    Shift16& set(uint8_t _sh) { sh = _sh; return *this; }
    constexpr size_t byte_size() const { return 16 << sh; }
    constexpr uint8_t chunk_size() const { return 1 << sh; }
};

struct AllocShift16 : public Shift16
{
    consteval AllocShift16(size_t n) : Shift16{n}
    {
        if (n > 128) throw;
    }
};

struct TagInfo {
    bool free{false};
    uint8_t tag{0};
    Shift16 size{16};

    TagInfo() = default;
    TagInfo(uint8_t b)
    {
        if ((b & 0x80) != 0) {
            free = true;
            tag = 0;
            size.set(b & 0x7F);
        } else {
            free = false;
            tag = (b >> 2) & 0x1F;
            size.set(b & 0x03);
        }
    }

    uint8_t tagbyte() const
    {
        return (free ? 0x80 : 0x00) | (tag << 2) | size.sh;
    }

    static TagInfo Free(Shift16 sz)
    {
        return TagInfo(static_cast<uint8_t>(0x80 | sz.sh));
    }

    static TagInfo Allocated(uint8_t tag, Shift16 sz)
    {
        TagInfo res;
        res.free = false;
        res.tag = (tag & 0x1F);
        res.size = sz;
        return res;
    }
};

class Allocator
{
private:

    struct Ref {
        uint16_t block;
        uint16_t chunk;
        friend bool operator==(const Ref& l, const Ref& r)
        {
            return l.block == r.block && l.chunk == r.chunk;
        }
    };
    static constexpr Ref NULLREF = {0xFFFF, 0xFFFF};
    struct Info {
        uint8_t tag;
        Ref prev;
        Ref next;
    };
    union alignas(16) Chunk {
        std::array<uint8_t, 16> data;
        Info info;
    };
    static_assert(sizeof(Chunk) == 16);
    static_assert(offsetof(Chunk, data) == offsetof(Chunk, info.tag));

    static constexpr size_t BLOCK_SIZE{256*1024};
    static constexpr Shift16 BLOCK_EXP{BLOCK_SIZE};
    static constexpr uint16_t CHUNK_COUNT{BLOCK_SIZE / sizeof(Chunk)};

    struct Block { alignas(128) std::array<Chunk, CHUNK_COUNT> data; };

    static_assert(BLOCK_EXP.byte_size() == BLOCK_SIZE);
    static_assert(CHUNK_COUNT * sizeof(Chunk) == BLOCK_SIZE);
    static_assert(sizeof(Block) == BLOCK_SIZE);

    std::vector<std::unique_ptr<Block>> m_blocks;
    std::array<Ref, BLOCK_EXP.sh> m_free;

    Chunk* GetChunk(Ref ref) { return &(m_blocks[ref.block]->data[ref.chunk]); }

    /* Removes ref from free list, returns "next" if available or NULLREF */
    Ref TakeFree(Ref ref);
    Ref GetBuddy(Ref ref, Shift16 sz)
    {
        return Ref{.block = ref.block, .chunk=static_cast<uint16_t>(ref.chunk ^ sz.chunk_size())};
    }

    void MakeFree(Ref ref, Shift16 sz);
    void NewBlock();
    void FreeHalfChunk(Ref ref, Shift16 sz);

public:
    Allocator()
    {
        for (Ref& ref : m_free) ref = NULLREF;
    }

    Ref allocate(uint8_t id, AllocShift16 sz);
    void deallocate(Ref&& ref, Shift16 sz);
};

class Type
{
private:
    uint8_t m_type{0xFF};

public:
    explicit Type(uint8_t t) : m_type{t} { }
    explicit consteval Type(Shift16 size, uint8_t code)
    {
        if (code >= 32) throw;
        if (size.sh >= 4) throw;
        m_type = (size.sh << 5) | code;
    }

    bool allocated() const { return (m_type & 0x80) == 0; }
    Shift16 size() const { return Shift16::FromInt((m_type >> 5) & 0x04); }
    uint8_t code() const { return m_type & 0x1F; }

    static Type Free(uint8_t size)
    {
        return Type{static_cast<uint8_t>(0x80 | (size & 0x7F))};
    }
};

} // Buddy namespace

#endif // BUDDY_H
