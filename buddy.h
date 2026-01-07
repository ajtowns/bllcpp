#ifndef BUDDY_H
#define BUDDY_H

#include <func.h>
#include <logging.h>
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

    constexpr uint32_t read() const { return val[0] + (val[1] << 8) + (val[2] << 16); }

    void write(uint32_t v)
    {
        val[0] = by(v,0);
        val[1] = by(v,1);
        val[2] = by(v,2);
    }
};
static_assert(sizeof(Uint24) == 3 && alignof(Uint24) == 1);
static_assert(Uint24{uint32_t{1000}}.read() == 1000);

class Ref
{
private:
    friend class Allocator;
    friend class ShortRef;

    uint16_t block;
    uint16_t chunk;

public:
    struct NullRef_tag { };

    Ref() = delete; // explicitly initialize as NULLREF instead

    struct Bare { uint16_t block, chunk; };

    constexpr Ref(const NullRef_tag&) : block{0xFFFF}, chunk{0xFFFF} { }
    explicit constexpr Ref(Bare b) : block{b.block}, chunk{b.chunk} { }

    constexpr Ref(const Ref&) = default;
    Ref& operator=(Ref&& o) = default;
    constexpr Ref(Ref&& other) = default;
    Ref& operator=(const Ref&) = default;

    void set_null() { block = chunk = 0xFFFF; }
    constexpr bool is_null() const { return block == 0xFFFF && chunk == 0xFFFF; }

    Ref take() { Ref r = *this; set_null(); return r; }

    friend constexpr bool operator==(const Ref& l, const Ref& r)
    {
        return l.block == r.block && l.chunk == r.chunk;
    }
};
inline constexpr Ref::NullRef_tag NULLREF{};

class ShortRef
{
private:
    Uint24 m_value;

    static_assert( (1ul<<24)/CHUNK_COUNT <= std::numeric_limits<uint16_t>::max() );
    static_assert( CHUNK_COUNT <= std::numeric_limits<uint16_t>::max() );

public:
    constexpr ShortRef(const Ref& ref) : m_value{static_cast<uint32_t>(ref.is_null() ? 0xFFFFFF : ref.block * CHUNK_COUNT + ref.chunk)} { }
    constexpr ShortRef(const Ref::NullRef_tag&) : ShortRef(Ref(NULLREF)) { }

    constexpr operator Ref() const
    {
        Ref::Bare res{.block=0xFFFF, .chunk=0xFFFF};
        uint32_t v = m_value.read();
        if (v != 0xFFFFFF) {
            res.block = static_cast<uint16_t>(v / CHUNK_COUNT);
            res.chunk = static_cast<uint16_t>(v % CHUNK_COUNT);
        }
        return Ref{res};
    }

    constexpr uint32_t get_value() const { return m_value.read(); }
};
static_assert(Ref{ShortRef{NULLREF}} == Ref{NULLREF});
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

template<size_t SIZE>
struct alignas(16) TagView<Tag::NOREFCOUNT, SIZE>
{
    const uint8_t tag;
    std::array<uint8_t, SIZE-1> data;
};
static_assert(sizeof(TagView<Tag::NOREFCOUNT, 16>) == 16);
static_assert(sizeof(TagView<Tag::NOREFCOUNT, 32>) == 32);
static_assert(sizeof(TagView<Tag::NOREFCOUNT, 64>) == 64);
static_assert(sizeof(TagView<Tag::NOREFCOUNT, 128>) == 128);

template<size_t SIZE>
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
    TagView<Tag::OWNED_ATOM, 16>(const uint8_t* _data, uint32_t _size) : size{_size}, data{_data} { }
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
    using FuncEnumType = Func;

    FuncEnumType funcid;
    ShortRef env;
    ShortRef state;
    std::array<uint8_t, 4> extra_state{0,0,0,0};
};
static_assert(sizeof(TagView<Tag::FUNC, 16>) == 16);

template<>
struct TagView<Tag::FUNC_COUNT, 16> : public TagRefCount
{
    using FuncEnumType = FuncCount;

    FuncEnumType funcid;
    ShortRef env;
    ShortRef state;
    uint32_t counter{0};
};
static_assert(sizeof(TagView<Tag::FUNC_COUNT, 16>) == 16);

template<>
struct TagView<Tag::FUNC_EXT, 16> : public TagRefCount
{
    using FuncEnumType = FuncExt;

    FuncEnumType funcid;
    ShortRef env;
    const void* state{nullptr};
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

template<typename TV>
inline constexpr bool IsTagView = false;

template<Tag TAG, size_t SIZE>
inline constexpr bool IsTagView<TagView<TAG,SIZE>> = true;

template<typename T>
concept AtomicTagView = IsTagView<T> && requires(const T& t) {
    { t.span() } -> std::same_as<std::span<const uint8_t>>;
};
static_assert(AtomicTagView<TagView<Tag::EXT_ATOM,16>>);

template<typename T>
concept FuncyTagView = IsTagView<T> && FuncEnum<typename T::FuncEnumType> &&
    std::same_as<typename T::FuncEnumType, decltype(T::funcid)>;
static_assert(FuncyTagView<TagView<Tag::FUNC,16>>);

template<typename T>
struct quoted_type {
    T value;
    explicit quoted_type(T&& v) : value(std::forward<T>(v)) { }
    explicit quoted_type(const T& v) : value(v) { }
};
template<typename T>
quoted_type(T&&) -> quoted_type<std::decay_t<T>>;

inline constexpr auto quote = [](auto&& v) -> quoted_type<std::decay_t<decltype(v)>> { return quoted_type{std::forward<decltype(v)>(v)}; };

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

    template<Tag TAG, size_t SIZE>
    TagView<TAG, SIZE>* TagViewAt(Chunk* chunk)
    {
        static_assert((SIZE & (SIZE-1)) == 0);
        static_assert(SIZE >= 16 && SIZE <= 128);
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

    std::array<Ref,2> _nilone = {NULLREF, NULLREF};

public:
    Allocator();

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

    Ref create(std::span<const uint8_t> sp)
    {
        if (sp.size() == 0) return nil();
        if (sp.size() == 1 && sp[0] == 1) return one();
        if (sp.size() < 12) return create<Tag::INPLACE_ATOM,16>(sp);
        if (sp.size() < 28) return create<Tag::INPLACE_ATOM,32>(sp);
        if (sp.size() < 60) return create<Tag::INPLACE_ATOM,64>(sp);
        if (sp.size() < 124) return create<Tag::INPLACE_ATOM,128>(sp);
        uint8_t* ext{static_cast<uint8_t*>(std::malloc(sp.size()))};
        std::copy(sp.begin(), sp.end(), ext);
        return create<Tag::OWNED_ATOM,16>({ext, static_cast<uint32_t>(sp.size())});
    }
    Ref create(std::span<const char> sp) { return create(MakeUCharSpan(sp)); }
    Ref create(std::string_view sv) { return create(MakeUCharSpan(sv)); }
    Ref create(const char* s) { return create(std::span(s, strlen(s))); }
    Ref create(int64_t n);

    Ref create(FuncEnum auto func) { return create(get_opcode(func)); }

    template<typename T>
    Ref create(quoted_type<T> r)
    {
        return create_cons(nil(), create(std::move(r.value)));
    }

    Ref create(Ref&& r) { return r.take(); }

    Ref create_bool(bool b) { return bumpref(_nilone[b ? 1 : 0]); }
    Ref nil() { return create_bool(false); }
    Ref one() { return create_bool(true); }

    Ref create_cons(Ref&& left, Ref&& right)
    {
        return create<Buddy::Tag::CONS, 16>({.left=left.take(), .right=right.take()});
    }

    Ref create_list() { return nil(); }
    template<typename T1, typename... T>
    Ref create_list(T1&& el1, T&&... args) {
        Ref t = create_list(std::forward<T>(args)...);
        return create_cons(this->create(std::forward<T1>(el1)), std::move(t));
    }

    Ref create_func(Func funcid, Ref&& env, Ref&& state)
    {
        return create<Buddy::Tag::FUNC,16>({
            .funcid = funcid,
            .env = env.take(),
            .state = state.take(),
        });
    }

    Ref create_func(FuncCount funcid, Ref&& env, Ref&& state, uint32_t counter=0)
    {
        return create<Buddy::Tag::FUNC_COUNT,16>({
            .funcid = funcid,
            .env = env.take(),
            .state = state.take(),
            .counter = counter,
        });
    }

    Ref create_func(FuncExt funcid, Ref&& env, const void* state)
    {
        return create<Buddy::Tag::FUNC_EXT,16>({
            .funcid = funcid,
            .env = env.take(),
            .state = state,
        });
    }

    Ref create_func(Func funcid, Ref&& env)
    {
        return create_func(funcid, std::move(env), NULLREF);
    }

    Ref create_func(FuncCount funcid, Ref&& env)
    {
        return create_func(funcid, std::move(env), NULLREF);
    }

    Ref create_func(FuncExt funcid, Ref&& env)
    {
        return create_func(funcid, std::move(env), nullptr);
    }

    Ref create_error(std::source_location sloc=std::source_location::current())
    {
        return create<Tag::ERROR,16>({.line=sloc.line(), .filename=sloc.file_name()});
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

    bool is_error(Ref ref)
    {
        bool res{false};
        dispatch(ref, util::Overloaded(
            [&](const TagView<Tag::ERROR,16>&) { res = true; },
            [](const auto&) { }
        ));
        return res;
    }

    template<TagViewCallable Fn>
    void dispatch(Ref ref, Fn&& fn)
    {
        using enum Tag;

        if (ref.is_null()) return;

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

template<bool RequireMin=true>
inline std::optional<int64_t> SmallInt(std::span<const uint8_t> sp)
{
    if (sp.empty()) return 0;
    if constexpr (RequireMin) {
        if (sp.back() == 0x00 || sp.back() == 0x80) {
            size_t s = sp.size();
            if (s == 1 || (sp[s-2] & 0x80) == 0) return std::nullopt;
        }
    }
    int64_t res = 0;
    if (sp.back() & 0x80) {
        // negative
        for (size_t i = 0; i < sp.size(); ++i) {
            int64_t v = sp[i];
            if (i == sp.size() - 1) v &= 0x7F;
            if (sp[i] != 0) {
                 if (i >= 8) return std::nullopt;
                 if (i == 7 && ((res > 0 && v == 0x80) || v > 0x80)) return std::nullopt;
                 res += (-v << (8*i));
            }
        }
    } else {
        // positive
        for (size_t i = 0; i < sp.size(); ++i) {
            int64_t v = sp[i];
            if (sp[i] != 0) {
                 if (i >= 8) return std::nullopt;
                 if (i == 7 && v >= 0x80) return std::nullopt;
                 res += (v << (8*i));
            }
        }
    }
    return res;
}

std::string to_string(Allocator& alloc, Ref ref, bool in_list=false);

} // Buddy namespace

#endif // BUDDY_H
