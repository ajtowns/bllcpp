#include <buddy.h>

#include <cassert>
#include <cstdlib>

namespace Buddy {

Ref Allocator::TakeFree(Ref ref)
{
    Ref result = NULLREF;
    Chunk* chunk = GetChunk(ref);
    if (chunk->info.next != ref) {
        Chunk* next = GetChunk(chunk->info.next);
        Chunk* prev = GetChunk(chunk->info.prev);
        next->info.prev = chunk->info.prev;
        prev->info.next = chunk->info.next;
        result = prev->info.next;
    }
    return result;
}

void Allocator::MakeFree(Ref ref, Shift16 sz)
{
    Chunk* chunk = GetChunk(ref);
    chunk->info.tag = sz.sh;
    Ref next = m_free[sz.sh];
    if (next == NULLREF) {
        chunk->info.prev = chunk->info.next = ref;
    } else {
        Chunk* chunknext = GetChunk(next);
        Chunk* chunkprev = GetChunk(chunknext->info.prev);
        chunk->info.prev = chunknext->info.prev;
        chunknext->info.prev = ref;
        chunkprev->info.next = ref;
        chunk->info.next = next;
        chunk->info.tag = TagInfo::Free(sz).tagbyte();
    }
    m_free[sz.sh] = ref;
}

void Allocator::NewBlock()
{
    uint16_t block = m_blocks.size();
    m_blocks.emplace_back(std::make_unique<Block>());
    FreeHalfChunk({{.block=block, .chunk=0}}, BLOCK_EXP);
}

void Allocator::FreeHalfChunk(Ref r, Shift16 sz)
{
    assert(sz.sh > 0 && sz.sh <= m_free.size());
    Ref other{{.block=r.block, .chunk=static_cast<uint16_t>(r.chunk + (1<<(sz.sh-1)))}};
    MakeFree(other, sz - 1);
}

Ref Allocator::allocate(Tag tag, AllocShift16 sz)
{
    size_t best_free = sz.sh;
    while (best_free < m_free.size() && m_free[best_free] == NULLREF) {
        ++best_free;
    }
    Ref blk;
    if (best_free == m_free.size()) {
        blk.block = m_blocks.size();
        blk.chunk = 0;
        m_blocks.emplace_back(std::make_unique<Block>());
        MakeFree(blk, BLOCK_EXP);
        --best_free;
    } else {
        blk = m_free[best_free];
        m_free[best_free] = TakeFree(blk);
    }
    while (best_free > sz.sh) {
        MakeFree(blk, Shift16::FromInt(best_free));
        --best_free;
    }

    GetChunk(blk)->data[0] = TagInfo::Allocated(tag, sz).tagbyte();

    return blk;
}

void Allocator::deallocate(Ref&& ref)
{
    Ref r = std::move(ref);
    ref = NULLREF;
    Shift16 sz{TagInfo{GetChunk(r)->data[0]}.size};
    while (sz.sh < 3) {
        Ref buddy = GetBuddy(r, sz);
        Chunk* chunk = GetChunk(buddy);
        TagInfo buddytag{chunk->info.tag};
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
    Ref todo{};

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
        return !other_refs;
    };

    while (!work.is_null()) {
        if (has_other_refs(work)) {
            work = NULLREF;
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
                    todo_a = from_shortref(cons.left);
                    todo_b = from_shortref(cons.right);
                },
                [&](const TagView<Tag::ERROR,16>&) { },
                [&](const TagView<Tag::FUNC,16>& func) {
                    todo_a = from_shortref(func.env);
                    todo_b = from_shortref(func.state);
                },
                [&](const TagView<Tag::FUNC_COUNT,16>& func_count) {
                    todo_a = from_shortref(func_count.env);
                    todo_b = from_shortref(func_count.state);
                },
                [&](const TagView<Tag::FUNC_EXT,16>& func_ext) {
                    std::free(const_cast<void*>(func_ext.state));
                    todo_a = from_shortref(func_ext.env);
                }
            ));
            if (has_other_refs(todo_a)) todo_a = NULLREF;
            if (has_other_refs(todo_b)) todo_b = NULLREF;
            if (todo_a.is_null() && !todo_b.is_null()) std::swap(todo_a, todo_b);
            if (todo_b.is_null()) {
                deallocate(std::move(work));
                work = std::move(todo_a);
            } else {
                // todo_a and todo_b are not null, therefore size is 16, therefore convert to cons
                set_at(work, TagView<Tag::CONS, 16>{.left=to_shortref(todo_b), .right=to_shortref(todo)});
                todo = work;
                work = todo_a;
            }
        }
        if (work.is_null()) {
            work = std::move(todo);
        }
    }
}

} // Buddy namespace

