#include <buddy.h>

#include <cassert>

namespace Buddy {

Allocator::Ref Allocator::TakeFree(Allocator::Ref ref)
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
    FreeHalfChunk({block, 0}, BLOCK_EXP);
}

void Allocator::FreeHalfChunk(Ref r, Shift16 sz)
{
    assert(sz.sh > 0 && sz.sh <= m_free.size());
    Ref other{.block=r.block, .chunk=static_cast<uint16_t>(r.chunk + (1<<(sz.sh-1)))};
    MakeFree(other, sz - 1);
}

Allocator::Ref Allocator::allocate(uint8_t id, AllocShift16 sz)
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

    GetChunk(blk)->data[0] = TagInfo::Allocated(id, sz).tagbyte();

    return blk;
}

void Allocator::deallocate(Ref&& ref, Shift16 sz)
{
    Ref r = std::move(ref);
    ref = NULLREF;

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

} // Buddy namespace

