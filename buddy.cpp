#include <buddy.h>

#include <cassert>
#include <cstdlib>


// from crypto/hex_base.cpp
namespace {
using ByteAsHex = std::array<char, 2>;
constexpr std::array<ByteAsHex, 256> CreateByteToHexMap()
{
    constexpr char hexmap[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    std::array<ByteAsHex, 256> byte_to_hex{};
    for (size_t i = 0; i < byte_to_hex.size(); ++i) {
        byte_to_hex[i][0] = hexmap[i >> 4];
        byte_to_hex[i][1] = hexmap[i & 15];
    }
    return byte_to_hex;
}
} // namespace
static std::string HexStr(const Span<const uint8_t> s)
{
    std::string rv(s.size() * 2, '\0');
    static constexpr auto byte_to_hex = CreateByteToHexMap();
    static_assert(sizeof(byte_to_hex) == 512);

    char* it = rv.data();
    for (uint8_t v : s) {
        std::memcpy(it, byte_to_hex[v].data(), 2);
        it += 2;
    }

    assert(it == rv.data() + rv.size());
    return rv;
}

namespace Buddy {

Allocator::Allocator()
{
    constexpr std::array<const uint8_t,1> data{{1}};
    _nilone[0] = create<Tag::INPLACE_ATOM,16>(std::span(data).subspan(0, 0));
    _nilone[1] = create<Tag::INPLACE_ATOM,16>(std::span(data).subspan(0, 1));
}

Ref Allocator::TakeFree(Ref ref)
{
    Ref result = NULLREF;
    Chunk* chunk = GetChunk(ref);
    if (chunk->info().next != ref) {
        Chunk* next = GetChunk(chunk->info().next);
        Chunk* prev = GetChunk(chunk->info().prev);
        next->info().prev = chunk->info().prev;
        prev->info().next = chunk->info().next;
        result = prev->info().next;
    }
    return result;
}

void Allocator::MakeFree(Ref ref, Shift16 sz)
{
    Chunk* chunk = GetChunk(ref);
    chunk->info().tag = TagInfo::Free(sz).tagbyte();
    Ref next = m_free[sz.sh];
    if (next.is_null()) {
        chunk->info().prev = chunk->info().next = ref;
    } else {
        Chunk* chunknext = GetChunk(next);
        Chunk* chunkprev = GetChunk(chunknext->info().prev);
        chunk->info().prev = chunknext->info().prev;
        chunknext->info().prev = ref;
        chunkprev->info().next = ref;
        chunk->info().next = next;
        chunk->info().tag = TagInfo::Free(sz).tagbyte();
    }
    m_free[sz.sh] = ref;
}

Ref Allocator::allocate(AllocShift16 sz)
{
    size_t best_free = sz.sh;
    while (best_free < m_free.size() && m_free[best_free].is_null()) {
        ++best_free;
    }
    Ref blk{NULLREF};
    if (best_free == m_free.size()) {
        static_assert(std::tuple_size_v<decltype(m_free)> == BLOCK_EXP.sh + 1);
        blk.block = m_blocks.size();
        blk.chunk = 0;
        m_blocks.emplace_back(std::make_unique<Block>());
        MakeFree(blk, BLOCK_EXP);
        --best_free;
    }
    blk = m_free[best_free];
    m_free[best_free] = TakeFree(blk);
    Shift16 blk_sz = Shift16::FromInt(best_free);

    while (blk_sz.sh > sz.sh) {
        --blk_sz;
        MakeFree(GetBuddy(blk, blk_sz), blk_sz);
    }
    return blk;
}

void Allocator::deallocate(Ref&& ref)
{
    Ref r = ref.take();
    assert(r != _nilone[0]);
    assert(r != _nilone[1]);
    Shift16 sz{TagInfo{GetChunk(r)->data[0]}.size};
    while (sz.sh < 8) {
        Ref buddy = GetBuddy(r, sz);
        Chunk* chunk = GetChunk(buddy);
        TagInfo buddytag{chunk->taginfo()};
        if (buddytag.free && buddytag.size == sz) {
            Ref buddynext = TakeFree(buddy);
            if (m_free[sz.sh] == buddy) m_free[sz.sh] = buddynext;
            if (buddy.chunk < r.chunk) r = buddy;
            ++sz;
        } else {
            break;
        }
    }
    MakeFree(r, sz);
}

void Allocator::deref(Ref&& ref)
{
    Ref work{ref};
    Ref todo{NULLREF};

    auto has_other_refs = [&](Ref r) -> bool {
        bool other_refs{true};
        dispatch(r, util::Overloaded(
            [&]<size_t SIZE>(const TagView<Tag::NOREFCOUNT,SIZE>&) { other_refs = false; },
            [&](const TagRefCount& trc) {
                auto rc = trc.refcount.read();
                if (rc > 1) {
                    trc.refcount.write(rc - 1);
                } else {
                    other_refs = false;
                }
            }
        ));
        return other_refs;
    };

    while (!work.is_null()) {
        if (has_other_refs(work)) {
            work.set_null();
        } else {
            Ref todo_a{NULLREF}, todo_b{NULLREF};
            dispatch(work, util::Overloaded(
                [&]<size_t SIZE>(const TagView<Tag::NOREFCOUNT,SIZE>&) { },
                [&]<size_t SIZE>(const TagView<Tag::INPLACE_ATOM,SIZE>&) { },
                [&](const TagView<Tag::OWNED_ATOM,16>& atomown) {
                    std::free(const_cast<uint8_t*>(atomown.data));
                },
                [&](const TagView<Tag::EXT_ATOM,16>&) { },
                [&](const TagView<Tag::CONS,16>& cons) {
                    todo_a = cons.left;
                    todo_b = cons.right;
                },
                [&](const TagView<Tag::ERROR,16>&) { },
                [&](const TagView<Tag::FUNC,16>& func) {
                    todo_a = func.env;
                    todo_b = func.state;
                },
                [&](const TagView<Tag::FUNC_COUNT,16>& func_count) {
                    todo_a = func_count.env;
                    todo_b = func_count.state;
                },
                [&](const TagView<Tag::FUNC_EXT,16>& func_ext) {
                    std::free(const_cast<void*>(func_ext.state));
                    todo_a = func_ext.env;
                }
            ));
            if (!todo_a.is_null() && has_other_refs(todo_a)) todo_a.set_null();
            if (!todo_b.is_null() && has_other_refs(todo_b)) todo_b.set_null();
            if (todo_a.is_null() && !todo_b.is_null()) std::swap(todo_a, todo_b);
            if (todo_b.is_null()) {
                deallocate(std::move(work));
                work = todo_a;
            } else {
                // todo_a and todo_b are not null, therefore size is 16, therefore convert to cons
                set_at(work, TagView<Tag::CONS, 16>{.left=todo_b, .right=todo});
                todo = work;
                work = todo_a;
            }
        }
        if (work.is_null()) {
            work = todo.take();
        }
    }
}

Ref Allocator::create(int64_t n)
{
    if (n == 0) return nil();
    if (n == 1) return one();
    std::array<uint8_t,9> v{0,0,0,0,0,0,0,0,0};;
    if (n == std::numeric_limits<int64_t>::min()) {
        v.back() = 0x80;
        return create(v);
    }
    bool neg = n < 0;
    if (neg) n = -n;
    size_t i = 0;
    while (n > 0) {
        v[i] = (n & 0xFF);
        n >>= 8;
        if (n > 0) ++i;
    }
    if (v[i] & 0x80) ++i;
    if (neg) v[i] |= 0x80;
    return create(std::span(v).subspan(0,i+1));
}

std::string to_string(Allocator& alloc, Ref ref, bool in_list)
{
    auto is_all_printable = [](auto sp) {
        for (auto x : sp) { if (x == '"' || x < 32 || x > 126) return false; }
        return true;
    };
    std::string res;
    if (ref.is_null()) {
        res = "NULLREF";
    } else {
        alloc.dispatch(ref, util::Overloaded(
            [&]<size_t SIZE>(const TagView<Tag::NOREFCOUNT,SIZE>&) {
                res = strprintf("NOREF(%d:-)", SIZE);
            },
            [&]<AtomicTagView ATV>(const ATV& atom) {
                auto sp = atom.span();
                if (sp.size() == 0) {
                    if (in_list) {
                        res = ")";
                        in_list = false;
                    } else {
                        res = "nil";
                    }
                } else if (sp.size() > 4 && is_all_printable(sp)) {
                    res = strprintf("\"%s\"", std::string(sp.begin(), sp.end()));
                } else if (auto small = SmallInt(sp); small) {
                    res = strprintf("%d", *small);
                } else {
                    res = strprintf("0x%s", HexStr(sp));
                }
            },
            [&](const TagView<Tag::CONS,16>& cons) {
                res = strprintf("%s%s%s", (in_list ? " " : "("), to_string(alloc, cons.left), to_string(alloc, cons.right, /*in_list=*/true));
                in_list = false;
            },
            [&](const TagView<Tag::ERROR,16>& err) {
                res = strprintf("ERROR(%s:%d)", err.filename, err.line);
            },
            [&](const TagView<Tag::FUNC,16>& func) { res = strprintf("FUNC(%d,-,-)", static_cast<int>(func.funcid)); },
            [&](const TagView<Tag::FUNC_COUNT,16>& func) { res = strprintf("FUNCC(%d,-,-,%d)", static_cast<int>(func.funcid), func.counter); },
            [&](const TagView<Tag::FUNC_EXT,16>& func) { res = strprintf("FUNCEXT(%d,-,-)", static_cast<int>(func.funcid)); }
        ));
    }
    if (in_list) {
        res = " . " + res + ")";
    }
    return res;
}

} // Buddy namespace

