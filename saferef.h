#ifndef SAFEREF_H
#define SAFEREF_H

#include <attributes.h>
#include <buddy.h>
#include <logging.h>
#include <overloaded.h>
#include <tinyformat.h>

class SafeAllocator
{
private:
    using Ref = Buddy::Ref;

    Buddy::Allocator& m_alloc;

public:
    explicit SafeAllocator(Buddy::Allocator& alloc LIFETIMEBOUND) : m_alloc{alloc} { }

    // const access to m_alloc
    const Buddy::Allocator& Allocator() const LIFETIMEBOUND { return m_alloc; }

    class SafeRef {
    private:
        friend SafeAllocator;

        SafeAllocator& m_safealloc;
        Ref m_ref;

        explicit SafeRef(SafeAllocator& sa LIFETIMEBOUND, Ref&& r) : m_safealloc{sa}, m_ref{std::move(r)} { }

        void deref()
        {
            m_safealloc.m_alloc.deref(std::move(m_ref));
            m_ref.set_null();
        }

    public:
        SafeRef() = delete;
        SafeRef(const SafeRef&) = delete; // use copy()
        SafeRef& operator=(const SafeRef&) = delete;
        SafeRef(SafeRef&& other) : m_safealloc{other.m_safealloc}, m_ref{std::move(other.m_ref)} { }
        SafeRef& operator=(SafeRef&& other)
        {
            assert(&m_safealloc.m_alloc == &other.m_safealloc.m_alloc);
            deref();
            m_ref = other.m_ref;
            other.m_ref.set_null();
            return *this;
        }
        ~SafeRef() { deref(); }

        SafeRef copy() { return SafeRef(m_safealloc, m_safealloc.m_alloc.bumpref(m_ref)); }

        std::string to_string() { return Buddy::to_string(m_safealloc.m_alloc, m_ref); }
    };

    SafeRef create(SafeRef&& r) LIFETIMEBOUND { return std::move(r); }

    template<typename... T>
    SafeRef create(T&&... args) LIFETIMEBOUND
    {
        return SafeRef(*this, m_alloc.create(std::forward<T>(args)...));
    }

    SafeRef cons(SafeRef&& left, SafeRef&& right) LIFETIMEBOUND
    {
        Ref l = left.m_ref.take();
        Ref r = right.m_ref.take();
        return make_safe(m_alloc.create<Buddy::Tag::CONS,16>({.left=l, .right=r}));
    }

    SafeRef create_list() LIFETIMEBOUND { return nil(); }
    template<typename H, typename... T>
    SafeRef create_list(H&& h, T&&... args) LIFETIMEBOUND
    {
        SafeRef t = create_list(std::forward<T>(args)...);
        return cons(this->create(std::forward<H>(h)), std::move(t));
    }

    SafeRef nil() LIFETIMEBOUND { return make_safe(m_alloc.nil()); }
    SafeRef one() LIFETIMEBOUND { return make_safe(m_alloc.one()); }

    void DumpChunks(std::source_location sloc = std::source_location::current()) { m_alloc.DumpChunks(sloc); }

private:
    SafeRef make_safe(Ref&& ref) LIFETIMEBOUND { return SafeRef{*this, std::move(ref)}; };
};
using SafeRef = SafeAllocator::SafeRef;

namespace Buddy {
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

#endif // SAFEREF_H
