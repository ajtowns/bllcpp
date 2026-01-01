#ifndef BUDDY_H
#define BUDDY_H

#include <overloaded.h>
#include <tinyformat.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <source_location>
#include <span>
#include <vector>

namespace Buddy {

static constexpr size_t BLOCK_SIZE{256*1024};
static constexpr uint16_t CHUNK_COUNT{BLOCK_SIZE / 16};

static_assert(((BLOCK_SIZE-1) & BLOCK_SIZE) == 0, "must be power of 2");

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
    Shift16& operator--() { --sh; return *this; }
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
    constexpr size_t chunk_size() const { return 1 << sh; }
};

struct AllocShift16 : public Shift16
{
    consteval AllocShift16(size_t n) : Shift16{n}
    {
        if (n > 128) throw;
    }
};

enum class Tag : uint8_t
{
    NOREFCOUNT   = 0,
    INPLACE_ATOM = 1,
    OWNED_ATOM   = 2,
    EXT_ATOM     = 3,
    CONS         = 4,
    ERROR        = 5,
    FUNC         = 6,
    FUNC_COUNT   = 7,
    FUNC_EXT     = 8,
};

inline std::optional<Tag> GetTag(uint8_t t, Shift16 sz)
{
    if (t > static_cast<uint8_t>(Tag::FUNC_EXT)) return std::nullopt;
    if (sz.sh > 0 && t > static_cast<uint8_t>(Tag::INPLACE_ATOM)) return std::nullopt;
    return Tag{t};
}

struct TagInfo {
    bool free{true};
    Shift16 size{16};
    std::optional<Tag> tag{std::nullopt};

    TagInfo() = default;
    constexpr TagInfo(uint8_t b)
    {
        if ((b & 0x80) != 0) {
            free = true;
            size.set(b & 0x7F);
            tag = std::nullopt;
        } else {
            free = false;
            size.set(b & 0x03);
            tag = GetTag(b >> 2, size);
        }
    }

    constexpr uint8_t tagbyte() const
    {
        return (free ? 0x80 : 0x00) | (static_cast<uint8_t>(tag.value_or(Tag::NOREFCOUNT)) << 2) | size.sh;
    }

    static constexpr TagInfo Free(Shift16 sz)
    {
        TagInfo r;
        r.size = sz;
        return r;
    }

    static constexpr TagInfo Allocated(Tag tag, Shift16 sz)
    {
        TagInfo res;
        res.free = false;
        res.tag = tag;
        res.size = sz;
        return res;
    }
};

class Uint24
{
private:
    std::array<uint8_t, 3> val{0,0,0};

    static constexpr uint8_t by(uint32_t v, size_t n) { return static_cast<uint8_t>((v >> (8*n)) & 0xFF); }
public:
    Uint24() = default;
    explicit constexpr Uint24(std::span<const uint8_t> s)
    {
        if (s.size() == 3) {
            val[0] = s[0]; val[1] = s[1]; val[2] = s[2];
        }
    }
    constexpr Uint24(uint32_t v) : val{by(v,0), by(v,1), by(v,2)} { }

    constexpr uint32_t read() const { return val[0] + (val[1] << 8) + (val[1] << 16); }

    void write(uint32_t v)
    {
        val[0] = by(v,0);
        val[1] = by(v,1);
        val[2] = by(v,2);
    }
};
static_assert(sizeof(Uint24) == 3 && alignof(Uint24) == 1);

class Ref {
private:
    friend class Allocator;
    friend class ShortRef;

    uint16_t block;
    uint16_t chunk;

public:
    Ref() = delete; // explicitly initialize as NULLREF instead

    struct Bare { uint16_t block, chunk; };

    constexpr Ref(Bare b) : block{b.block}, chunk{b.chunk} { }
    constexpr Ref(const Ref&) = default;
    constexpr Ref(Ref&& other) : block{other.block}, chunk{other.chunk}
    {
        other.block = other.chunk = 0xFFFF;
    }

    constexpr bool is_null() const { return block == 0xFFFF && chunk == 0xFFFF; }

    Ref& operator=(const Ref&) = default;
    Ref& operator=(Ref&& o)
    {
        *this = o;
        o.block = o.chunk = 0xFFFF;
        return *this;
    }

    friend constexpr bool operator==(const Ref& l, const Ref& r)
    {
        return l.block == r.block && l.chunk == r.chunk;
    }
};
inline constexpr Ref NULLREF{{.block=0xFFFF, .chunk=0xFFFF}};

class ShortRef
{
private:
    Uint24 m_value;

    static_assert( (1ul<<24)/CHUNK_COUNT <= std::numeric_limits<uint16_t>::max() );
    static_assert( CHUNK_COUNT <= std::numeric_limits<uint16_t>::max() );

public:
    constexpr ShortRef(const Ref& ref) : m_value{static_cast<uint32_t>(ref.is_null() ? 0xFFFFFF : ref.block * CHUNK_COUNT + ref.chunk)} { }

    constexpr operator Ref() const
    {
        Ref res{NULLREF};
        uint32_t v = m_value.read();
        if (v != 0xFFFFFF) {
            res.block = static_cast<uint16_t>(v / CHUNK_COUNT);
            res.chunk = static_cast<uint16_t>(v % CHUNK_COUNT);
        }
        return res;
    }

    constexpr uint32_t get_value() const { return m_value.read(); }
};
static_assert(ShortRef{NULLREF} == NULLREF);
static_assert(ShortRef{NULLREF}.get_value() == 0xFFFFFF);

enum class Func : uint16_t;
enum class FuncCount : uint16_t;
enum class FuncExt : uint8_t;

template<Tag TAG, size_t SIZE>
struct TagView;

struct TagRefCount
{
    uint8_t tag;
    mutable Uint24 refcount{uint32_t{1}};
};
static_assert(sizeof(TagRefCount) == 4);

template<uint8_t SIZE>
struct alignas(16) TagView<Tag::NOREFCOUNT, SIZE>
{
    const uint8_t tag;
    std::array<uint8_t, SIZE-1> data;
};
static_assert(sizeof(TagView<Tag::NOREFCOUNT, 16>) == 16);
static_assert(sizeof(TagView<Tag::NOREFCOUNT, 32>) == 32);
static_assert(sizeof(TagView<Tag::NOREFCOUNT, 64>) == 64);
static_assert(sizeof(TagView<Tag::NOREFCOUNT, 128>) == 128);

template<uint8_t SIZE>
struct alignas(16) TagView<Tag::INPLACE_ATOM, SIZE> : public TagRefCount
{
    uint8_t size;
    std::array<uint8_t, SIZE-5> data;
    std::span<uint8_t> span() { return std::span(data).subspan(0, size); }
    std::span<const uint8_t> span() const { return std::span(data).subspan(0, size); }

    TagView<Tag::INPLACE_ATOM, SIZE>(std::span<const uint8_t> sp)
    {
        if (sp.size() > SIZE - 5) sp = sp.subspan(0, SIZE-5);
        size = sp.size();
        std::copy(sp.begin(), sp.end(), data.begin());
    }
    TagView<Tag::INPLACE_ATOM, SIZE>(std::span<const char> sp) : TagView<Tag::INPLACE_ATOM, SIZE>(MakeUCharSpan(sp)) { }
};
static_assert(sizeof(TagView<Tag::INPLACE_ATOM, 16>) == 16);
static_assert(sizeof(TagView<Tag::INPLACE_ATOM, 32>) == 32);
static_assert(sizeof(TagView<Tag::INPLACE_ATOM, 64>) == 64);
static_assert(sizeof(TagView<Tag::INPLACE_ATOM, 128>) == 128);

template<>
struct TagView<Tag::OWNED_ATOM, 16> : public TagRefCount
{
    uint32_t size;
    const uint8_t* data;
    std::span<const uint8_t> span() const { return std::span<const uint8_t>{data, size}; }
};
static_assert(sizeof(TagView<Tag::OWNED_ATOM, 16>) == 16);

template<>
struct TagView<Tag::EXT_ATOM, 16> : public TagRefCount
{
    uint32_t size;
    const uint8_t* data;
    std::span<const uint8_t> span() const { return std::span<const uint8_t>{data, size}; }
};
static_assert(sizeof(TagView<Tag::EXT_ATOM, 16>) == 16);

template<>
struct TagView<Tag::CONS, 16> : public TagRefCount
{
    ShortRef left;
    ShortRef right;
    std::array<uint8_t,6> padding{0};
};
static_assert(sizeof(TagView<Tag::CONS, 16>) == 16);

template<>
struct TagView<Tag::ERROR, 16> : public TagRefCount
{
    uint32_t line;
    const char* filename;
};
static_assert(sizeof(TagView<Tag::ERROR, 16>) == 16);

template<>
struct TagView<Tag::FUNC, 16> : public TagRefCount
{
    Func funcid;
    ShortRef env;
    ShortRef state;
    std::array<uint8_t, 4> extra_state;
};
static_assert(sizeof(TagView<Tag::FUNC, 16>) == 16);

template<>
struct TagView<Tag::FUNC_COUNT, 16> : public TagRefCount
{
    FuncCount funcid;
    ShortRef env;
    ShortRef state;
    uint32_t counter;
};
static_assert(sizeof(TagView<Tag::FUNC_COUNT, 16>) == 16);

template<>
struct TagView<Tag::FUNC_EXT, 16> : public TagRefCount
{
    FuncExt funcid;
    ShortRef env;
    const void* state;
};
static_assert(sizeof(TagView<Tag::FUNC_EXT, 16>) == 16);

template<typename Fn>
concept TagViewCallable =
    std::invocable<Fn, TagView<Tag::NOREFCOUNT, 16>&> &&
    std::invocable<Fn, TagView<Tag::NOREFCOUNT, 32>&> &&
    std::invocable<Fn, TagView<Tag::NOREFCOUNT, 64>&> &&
    std::invocable<Fn, TagView<Tag::NOREFCOUNT, 128>&> &&
    std::invocable<Fn, TagView<Tag::INPLACE_ATOM, 16>&> &&
    std::invocable<Fn, TagView<Tag::INPLACE_ATOM, 32>&> &&
    std::invocable<Fn, TagView<Tag::INPLACE_ATOM, 64>&> &&
    std::invocable<Fn, TagView<Tag::INPLACE_ATOM, 128>&> &&
    std::invocable<Fn, TagView<Tag::OWNED_ATOM, 16>&> &&
    std::invocable<Fn, TagView<Tag::EXT_ATOM, 16>&> &&
    std::invocable<Fn, TagView<Tag::CONS, 16>&> &&
    std::invocable<Fn, TagView<Tag::ERROR, 16>&> &&
    std::invocable<Fn, TagView<Tag::FUNC, 16>&> &&
    std::invocable<Fn, TagView<Tag::FUNC_COUNT, 16>&> &&
    std::invocable<Fn, TagView<Tag::FUNC_EXT, 16>&>;

class Allocator
{
private:
    struct Info {
        uint8_t tag;
        Ref prev;
        Ref next;
    };
    static_assert(sizeof(Info) <= 16);
    static_assert(alignof(Info) <= 16);

    struct alignas(16) Chunk {
        std::array<uint8_t, 16> data;

        Info& info() { return *reinterpret_cast<Info*>(&data); }

        constexpr TagInfo taginfo() const { return TagInfo{data[0]}; }
    };
    static_assert(sizeof(Chunk) == 16);
    static_assert(offsetof(Chunk, data) == offsetof(Info, tag));

    template<Tag TAG, uint8_t SIZE>
    TagView<TAG, SIZE>* TagViewAt(Chunk* chunk)
    {
        return reinterpret_cast<TagView<TAG,SIZE>*>(chunk);
    };

    static constexpr Shift16 BLOCK_EXP{BLOCK_SIZE};

    struct Block { alignas(128) std::array<Chunk, CHUNK_COUNT> chunk; };

    static_assert(BLOCK_EXP.byte_size() == BLOCK_SIZE);
    static_assert(CHUNK_COUNT * sizeof(Chunk) == BLOCK_SIZE);
    static_assert(sizeof(Block) == BLOCK_SIZE);

    std::vector<std::unique_ptr<Block>> m_blocks;

    template<std::size_t N>
    constexpr static std::array<Ref, N> make_ref_array()
    {
        // The lambda (or function) returns a T constructed from args, repeated N times
        auto make_element = [&](auto) -> Ref { return NULLREF; };

        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return std::array<Ref, N>{ make_element(I)... };
        }(std::make_index_sequence<N>{});
    }

    std::array<Ref, BLOCK_EXP.sh + 1> m_free{make_ref_array<BLOCK_EXP.sh + 1>()};

    Chunk* GetChunk(Ref ref) { return &(m_blocks[ref.block]->chunk[ref.chunk]); }

    /* Removes ref from free list, returns "next" if available or NULLREF */
    Ref TakeFree(Ref ref);
    Ref GetBuddy(Ref ref, Shift16 sz)
    {
        return Ref{{.block = ref.block, .chunk=static_cast<uint16_t>(ref.chunk ^ sz.chunk_size())}};
    }

    void MakeFree(Ref ref, Shift16 sz);

    // allocates, without tagging
    Ref allocate(AllocShift16 sz);
    // combines buddies; but does not recursively deref
    void deallocate(Ref&& ref);

    template<Tag TAG, size_t SIZE>
    void set_at(Ref ref, TagView<TAG, SIZE> tv)
    {
        Chunk* chunk = GetChunk(ref);
        *TagViewAt<TAG,SIZE>(chunk) = tv;
        chunk->data[0] = TagInfo::Allocated(TAG, SIZE).tagbyte();
    }

public:
    Allocator() = default;

    void DumpChunks(std::source_location sloc=std::source_location::current())
    {
        std::cout << strprintf("%s:%d - Blocks: %d", sloc.file_name(), sloc.line(), m_blocks.size()) << std::endl;
        Ref ref{NULLREF};
        for (ref.block = 0; ref.block < m_blocks.size(); ++ref.block) {
            std::cout << ref.block << ":";
            ref.chunk = 0;
            while (ref.chunk < CHUNK_COUNT) {
                Chunk* chunk = GetChunk(ref);
                auto tag = chunk->taginfo();
                std::cout << strprintf(" %d%s%d", refs(ref), (tag.free ? "_" : "*"), tag.size.byte_size());
                ref.chunk += tag.size.chunk_size();
            }
            std::cout << std::endl;
        }
    }

    template<Tag TAG, size_t SIZE, typename... T>
    Ref create(TagView<TAG,SIZE> tv)
    {
        Ref r{allocate(SIZE)};
        set_at(r, std::move(tv));
        return r;
    }

    template<Tag TAG, size_t SIZE, typename... T>
    Ref create(T&&... args)
    {
        Ref r{allocate(SIZE)};
        set_at(r, TagView<TAG,SIZE>(args...));
        return r;
    }

    Ref bumpref(Ref& ref)
    {
        Ref res{NULLREF};
        dispatch(ref, util::Overloaded(
            [&]<size_t SIZE>(const TagView<Tag::NOREFCOUNT,SIZE>&) { },
            [&](const TagRefCount& trc) {
                auto rc = trc.refcount.read();
                trc.refcount.write(rc + 1);
                res = ref;
            }
        ));
        return res;
    }

    void deref(Ref&& ref);

    std::tuple<std::optional<Tag>, std::span<uint8_t>, Shift16> lookup(Ref ref)
    {
        Chunk* chunk = GetChunk(ref);
        auto tag = chunk->taginfo();
        if (tag.free) {
            return {std::nullopt, {}, {}};
        } else {
            return {tag.tag, std::span<uint8_t>{&chunk->data[0], tag.size.byte_size()}.subspan(1), tag.size};
        }
    }

    size_t refs(Ref ref)
    {
        size_t res{0};
        dispatch(ref, util::Overloaded(
            [&]<size_t SIZE>(const TagView<Tag::NOREFCOUNT,SIZE>&) { res = 1; },
            [&](const TagRefCount& trc) { res = trc.refcount.read(); }
        ));
        return res;
    }

    template<TagViewCallable Fn>
    void dispatch(Ref ref, Fn&& fn)
    {
        using enum Tag;

        Chunk* chunk = GetChunk(ref);
        auto tag = chunk->taginfo();
        if (tag.free || !tag.tag) return;
        switch(*tag.tag) {
        case NOREFCOUNT:
            if (tag.size.sh == 0) return fn(*TagViewAt<NOREFCOUNT,16>(chunk));
            if (tag.size.sh == 1) return fn(*TagViewAt<NOREFCOUNT,32>(chunk));
            if (tag.size.sh == 2) return fn(*TagViewAt<NOREFCOUNT,64>(chunk));
            if (tag.size.sh == 3) return fn(*TagViewAt<NOREFCOUNT,128>(chunk));
            break;
        case INPLACE_ATOM:
            if (tag.size.sh == 0) return fn(*TagViewAt<INPLACE_ATOM,16>(chunk));
            if (tag.size.sh == 1) return fn(*TagViewAt<INPLACE_ATOM,32>(chunk));
            if (tag.size.sh == 2) return fn(*TagViewAt<INPLACE_ATOM,64>(chunk));
            if (tag.size.sh == 3) return fn(*TagViewAt<INPLACE_ATOM,128>(chunk));
            break;
        case OWNED_ATOM:
            return fn(*TagViewAt<OWNED_ATOM,16>(chunk));
        case EXT_ATOM:
            return fn(*TagViewAt<EXT_ATOM,16>(chunk));
        case CONS:
            return fn(*TagViewAt<CONS,16>(chunk));
        case ERROR:
            return fn(*TagViewAt<ERROR,16>(chunk));
        case FUNC:
            return fn(*TagViewAt<FUNC,16>(chunk));
        case FUNC_COUNT:
            return fn(*TagViewAt<FUNC_COUNT,16>(chunk));
        case FUNC_EXT:
            return fn(*TagViewAt<FUNC_EXT,16>(chunk));
        }
    }
};

class AtomRef
{
private:
    Ref ref{NULLREF};
    std::span<const uint8_t> atom;

    AtomRef() = default;

    AtomRef(std::span<const uint8_t> _atom) : ref{NULLREF}, atom{_atom} { }
    AtomRef(Ref&& _ref, std::span<const uint8_t> _atom) : ref{std::move(_ref)}, atom{_atom} { }

public:
    std::span<const uint8_t> span() const { return atom; }

    static std::optional<AtomRef> FromRef(Allocator& alloc, Ref&& ref)
    {
        std::optional<AtomRef> res{std::nullopt};
        alloc.dispatch(ref, util::Overloaded(
            [&]<uint8_t SIZE>(const TagView<Tag::INPLACE_ATOM,SIZE>& atom) {
                res = AtomRef(std::move(ref), atom.span());
            },
            [&](const TagView<Tag::OWNED_ATOM,16>& atomown) {
                res = AtomRef(std::move(ref), atomown.span());
            },
            [&](const TagView<Tag::EXT_ATOM,16>& atomext) {
                res = AtomRef(atomext.span());
                alloc.deref(std::move(ref));
            },
            [](const auto&) { } // not an atom
        ));
        return res;
    }

    void deallocate(Allocator& alloc)
    {
        alloc.deref(std::move(ref));
    }
};

} // Buddy namespace

#endif // BUDDY_H
