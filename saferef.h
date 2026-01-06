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
    Buddy::Allocator& Allocator() LIFETIMEBOUND { return m_alloc; }
    const Buddy::Allocator& Allocator() const LIFETIMEBOUND { return m_alloc; }

    class SafeRef {
    private:
        friend SafeAllocator;

        SafeAllocator& m_safealloc;
        Ref m_ref;

        explicit SafeRef(SafeAllocator& sa LIFETIMEBOUND, Ref&& r) : m_safealloc{sa}, m_ref{r.take()} { }

        void deref()
        {
            m_safealloc.m_alloc.deref(m_ref.take());
        }

    public:
        SafeRef() = delete;
        ~SafeRef() { deref(); }

        SafeRef(const SafeRef&) = delete; // use copy()
        SafeRef& operator=(const SafeRef&) = delete; // use copy()

        SafeRef(SafeRef&& other) : m_safealloc{other.m_safealloc}, m_ref{other.m_ref.take()} { }
        SafeRef& operator=(SafeRef&& other)
        {
            assert(&m_safealloc.m_alloc == &other.m_safealloc.m_alloc);
            deref();
            m_ref = other.m_ref;
            other.m_ref.set_null();
            return *this;
        }

        SafeAllocator& Allocator() const { return m_safealloc; }
        SafeRef copy() { return SafeRef{m_safealloc, m_safealloc.m_alloc.bumpref(m_ref)}; }
        Ref take() { return m_ref.take(); }

        SafeRef nullref() const { return m_safealloc.nullref(); }
        bool is_null() const { return m_ref.is_null(); }

        bool is_error() { return m_safealloc.Allocator().is_error(m_ref); }
        std::string to_string() { return Buddy::to_string(m_safealloc.m_alloc, m_ref); }

        template<typename Fn>
        void dispatch(Fn&& fn)
        {
            m_safealloc.Allocator().dispatch(m_ref, std::forward<Fn>(fn));
        }
    };

    class SafeView {
    private:
        friend SafeAllocator;

        SafeAllocator& m_safealloc;
        Ref m_ref;

        explicit SafeView(SafeAllocator& sa LIFETIMEBOUND, const Ref& r) : m_safealloc{sa}, m_ref{r} { }

        void deref() { m_ref.set_null(); }

    public:
        SafeView() = delete;
        ~SafeView() = default;

        SafeView(const SafeView&) = default;
        SafeView& operator=(const SafeView&) = delete;
        SafeView(const SafeRef& ref LIFETIMEBOUND) : m_safealloc{ref.m_safealloc}, m_ref{ref.m_ref} { }

        SafeView(SafeView&& other) : m_safealloc{other.m_safealloc}, m_ref{other.m_ref.take()} { }
        SafeView& operator=(SafeView&& other)
        {
            assert(&m_safealloc.m_alloc == &other.m_safealloc.m_alloc);
            deref();
            m_ref = other.m_ref;
            other.m_ref.set_null();
            return *this;
        }

        SafeAllocator& Allocator() const { return m_safealloc; }
        SafeRef copy() { return SafeRef{m_safealloc, m_safealloc.m_alloc.bumpref(m_ref)}; }

        SafeRef nullref() const { return m_safealloc.nullref(); }
        bool is_null() const { return m_ref.is_null(); }

        bool is_error() { return m_safealloc.Allocator().is_error(m_ref); }
        std::string to_string() { return Buddy::to_string(m_safealloc.m_alloc, m_ref); }

        template<typename Fn>
        void dispatch(Fn&& fn) const
        {
            // const functions only
            m_safealloc.Allocator().dispatch(m_ref, [&](const auto& tv) { fn(tv); });
        }

    };

    SafeRef takeref(Ref&& ref) { return SafeRef(*this, ref.take()); }
    SafeView view(const Ref& ref LIFETIMEBOUND) { return SafeView(*this, Ref{ref}); }

    SafeRef nullref() { return SafeRef(*this, Buddy::NULLREF); }

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

    SafeRef error(std::source_location sloc = std::source_location::current())
    {
        return SafeRef{*this, m_alloc.create_error(sloc)};
    }

    SafeRef nil() LIFETIMEBOUND { return make_safe(m_alloc.nil()); }
    SafeRef one() LIFETIMEBOUND { return make_safe(m_alloc.one()); }

    void DumpChunks(std::source_location sloc = std::source_location::current()) { m_alloc.DumpChunks(sloc); }

private:
    SafeRef make_safe(Ref&& ref) LIFETIMEBOUND { return SafeRef{*this, std::move(ref)}; };
};
using SafeRef = SafeAllocator::SafeRef;
using SafeView = SafeAllocator::SafeView;

namespace SafeConv {

using namespace Buddy;

template<typename T>
class ConvertRef;

template<>
class ConvertRef<std::span<const uint8_t>>
{
private:
    SafeRef ref;
    std::span<const uint8_t> atom;

public:
    ConvertRef(SafeRef&& _ref, std::span<const uint8_t> _atom) : ref{std::move(_ref)}, atom{_atom} { }
    ConvertRef(ConvertRef&&) = default;
    ~ConvertRef() = default;

    static std::optional<ConvertRef> FromRef(SafeRef&& ref)
    {
        std::optional<ConvertRef> res{std::nullopt};
        ref.dispatch(util::Overloaded(
            [&]<uint8_t SIZE>(const TagView<Tag::INPLACE_ATOM,SIZE>& atom) {
                res.emplace(std::move(ref), atom.span());
            },
            [&](const TagView<Tag::OWNED_ATOM,16>& atomown) {
                res.emplace(std::move(ref), atomown.span());
            },
            [&](const TagView<Tag::EXT_ATOM,16>& atomext) {
                res.emplace(ref.nullref(), atomext.span());
                ref = ref.nullref();
            },
            [](const auto&) { } // not an atom
        ));
        return res;
    }

    static std::optional<ConvertRef> FromView(const SafeView& view)
    {
        std::optional<ConvertRef> res{std::nullopt};
        view.dispatch(util::Overloaded(
            [&]<AtomicTagView ATV>(const ATV& atom) {
                res.emplace(view.nullref(), atom.span());
            },
            [](const auto&) { } // not an atom
        ));
        return res;
    }

    std::span<const uint8_t> value() const { return atom; }
};

template<>
class ConvertRef<int64_t>
{
private:
    int64_t v;

public:
    ConvertRef(int64_t v) : v{v} { }
    ConvertRef(ConvertRef&&) = default;
    ~ConvertRef() = default;

    static std::optional<ConvertRef> FromRef(SafeRef&& ref)
    {
        auto res = FromView(ref);
        ref = ref.nullref(); // free ref
        return res;
    }

    static std::optional<ConvertRef> FromView(const SafeView& view)
    {
        std::optional<int64_t> res{std::nullopt};
        view.dispatch(util::Overloaded(
            [&]<AtomicTagView ATV>(const ATV& atom) {
                res = Buddy::SmallInt(atom.span());
            },
            [](const auto&) { } // not an atom
        ));
        return res;
    }

    int64_t value() const { return v; }
};

} // SafeConv namespace

#endif // SAFEREF_H
