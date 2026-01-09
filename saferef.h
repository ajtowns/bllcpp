#ifndef SAFEREF_H
#define SAFEREF_H

#include <attributes.h>
#include <buddy.h>
#include <logging.h>
#include <overloaded.h>
#include <tinyformat.h>

namespace SafeConv {
template<typename T>
class ConvertRef;
}

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
        SafeRef copy() const { return SafeRef{m_safealloc, m_safealloc.m_alloc.bumpref(m_ref)}; }
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

        template<typename T>
        auto convert() {
            return SafeConv::ConvertRef<T>(std::move(*this));
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
        SafeView& operator=(const SafeView& other)
        {
            assert(&m_safealloc.m_alloc == &other.m_safealloc.m_alloc);
            m_ref = other.m_ref;
            return *this;
        }
        SafeView(const SafeRef& ref LIFETIMEBOUND) : m_safealloc{ref.m_safealloc}, m_ref{ref.m_ref} { }

        SafeView(SafeView&& other) : m_safealloc{other.m_safealloc}, m_ref{other.m_ref.take()} { }
        SafeView& operator=(SafeView&& other)
        {
            assert(&m_safealloc.m_alloc == &other.m_safealloc.m_alloc);
            m_ref = other.m_ref;
            other.m_ref.set_null();
            return *this;
        }

        SafeAllocator& Allocator() const { return m_safealloc; }
        SafeRef copy() const { return SafeRef{m_safealloc, m_safealloc.m_alloc.bumpref(m_ref)}; }
        Ref take_view() const { return m_ref; }

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

        template<typename T>
        auto convert() const {
            return SafeConv::ConvertRef<T>(*this);
        }
    };

    SafeRef bumpref(Ref ref) { return SafeRef(*this, m_alloc.bumpref(ref)); }
    SafeRef takeref(Ref&& ref) { return SafeRef(*this, ref.take()); }
    SafeView view(const Ref& ref LIFETIMEBOUND) { return SafeView(*this, Ref{ref}); }

    SafeRef nullref() { return SafeRef(*this, Buddy::NULLREF); }
    SafeView nullview() { return SafeView(*this, Buddy::NULLREF); }

    template<typename T>
    SafeRef create(Buddy::quoted_type<T> r)
    {
        return cons(nil(), create(std::move(r.value)));
    }

    SafeRef create(SafeRef&& r) LIFETIMEBOUND { return std::move(r); }
    SafeRef create(SafeView v) LIFETIMEBOUND { return v.copy(); }

    template<typename... T>
    SafeRef create(T&&... args) LIFETIMEBOUND
    {
        return SafeRef(*this, m_alloc.create(std::forward<T>(args)...));
    }

    SafeRef create_owned(std::span<uint8_t> atom)
    {
        if (atom.size() > std::numeric_limits<uint32_t>::max()) return error();
        return SafeRef(*this, m_alloc.create<Buddy::Tag::OWNED_ATOM,16>({atom.data(), static_cast<uint32_t>(atom.size())}));
    }

    SafeRef create(Buddy::FuncEnum auto funcid, SafeRef&& env)
    {
        return SafeRef(*this, m_alloc.create_func(funcid, env.m_ref.take()));
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

template<>
class ConvertRef<SafeRef>
{
private:
    SafeRef m_ref;

public:
    explicit ConvertRef(SafeRef&& ref) : m_ref{std::move(ref)} { }
    explicit ConvertRef(const SafeView& view) : m_ref{view.copy()} { }
    ConvertRef(ConvertRef&&) = default;
    ~ConvertRef() = default;

    bool has_value() const { return !m_ref.is_null(); }
    operator bool() const { return has_value(); }

    void set_value(SafeRef&& ref) { m_ref = std::move(ref); }

    auto& operator*() { return m_ref; }
    auto* operator->() { return &m_ref; }
};

template<>
class ConvertRef<SafeView>
{
private:
    SafeView m_view;
    SafeRef m_ref;

public:
    explicit ConvertRef(SafeRef&& ref) : m_view{ref}, m_ref{std::move(ref)} { }
    explicit ConvertRef(const SafeView& view) : m_view{view}, m_ref{view.nullref()} { }
    ConvertRef(ConvertRef&&) = default;
    ~ConvertRef() = default;

    bool has_value() const { return true; }
    operator bool() const { return has_value(); }

    auto& operator*() { return m_view; }
    auto* operator->() { return &m_view; }
};

template<>
class ConvertRef<bool>
{
private:
    std::optional<bool> v;

public:
    explicit ConvertRef(bool b) : v{b} { }
    explicit ConvertRef(SafeRef&& ref) : ConvertRef(SafeView(ref))
    {
        if (v.has_value()) ref = ref.nullref(); // free ref
    }
    explicit ConvertRef(const SafeView& view)
    {
        if (view.is_null()) {
            v = std::nullopt;
        } else {
            v = true;
            view.dispatch(util::Overloaded(
                [&]<AtomicTagView ATV>(const ATV& atom) {
                    if (atom.span().size() == 0) v = false;
                },
                [](const auto&) { } // not an atom
            ));
        }
    }
    ConvertRef(ConvertRef&&) = default;
    ~ConvertRef() = default;

    bool has_value() const { return v.has_value(); }
    operator bool() const { return has_value(); }

    void set_value(bool b) { v = b; }

    auto& operator*() { return v.value(); }
};

template<>
class ConvertRef<std::span<const uint8_t>>
{
private:
    SafeRef ref;
    std::optional<std::span<const uint8_t>> atom;

public:
    ConvertRef(ConvertRef&&) = default;
    ~ConvertRef() = default;

    explicit ConvertRef(SafeRef&& _ref) : ref{std::move(_ref)}, atom{std::nullopt}
    {
        ref.dispatch(util::Overloaded(
            [&]<size_t SIZE>(const TagView<Tag::INPLACE_ATOM,SIZE>& atomin) {
                atom = atomin.span();
            },
            [&](const TagView<Tag::OWNED_ATOM,16>& atomown) {
                atom = atomown.span();
            },
            [&](const TagView<Tag::EXT_ATOM,16>& atomext) {
                atom = atomext.span();
                ref = ref.nullref(); // free
            },
            [&](const auto& tv) { // not an atom
                static_assert(!AtomicTagView<std::remove_cvref_t<decltype(tv)>>);
            }
        ));
    }

    explicit ConvertRef(const SafeView& view) : ref{view.Allocator().nullref()}, atom{std::nullopt}
    {
        view.dispatch(util::Overloaded(
            [&]<AtomicTagView ATV>(const ATV& atomatv) {
                atom = atomatv.span();
            },
            [](const auto&) { } // not an atom
        ));
    }

    bool has_value() const { return atom.has_value(); }
    operator bool() const { return has_value(); }

    void set_value(std::span<const uint8_t> a) { atom = a; }

    auto& operator*() { return atom.value(); }
    auto* operator->() { return &atom.value(); }
};

template<>
class ConvertRef<int64_t>
{
private:
    std::optional<int64_t> v;

public:
    explicit ConvertRef(int64_t v) : v{v} { }
    ConvertRef(ConvertRef&&) = default;
    ~ConvertRef() = default;

    explicit ConvertRef(SafeRef&& ref) : ConvertRef(SafeView(ref))
    {
        if (v.has_value()) ref = ref.nullref(); // free ref
    }

    explicit ConvertRef(const SafeView& view) : v{std::nullopt}
    {
        view.dispatch(util::Overloaded(
            [&]<AtomicTagView ATV>(const ATV& atom) {
                v = Buddy::SmallInt(atom.span());
            },
            [](const auto&) { } // not an atom
        ));
    }

    bool has_value() const { return v.has_value(); }
    operator bool() const { return has_value(); }

    void set_value(int64_t n) { v = n; }

    auto& operator*() { return v.value(); }
    auto* operator->() { return &v.value(); }
};

template<>
class ConvertRef<std::pair<SafeRef,SafeRef>>
{
private:
    std::optional<std::pair<SafeRef, SafeRef>> cons;

public:
    ConvertRef(ConvertRef&&) = default;
    ~ConvertRef() = default;

    explicit ConvertRef(SafeRef&& ref) : ConvertRef(SafeView(ref))
    {
        if (cons.has_value()) ref = ref.nullref(); // free ref
    }

    explicit ConvertRef(const SafeView& view) : cons{std::nullopt}
    {
        auto& alloc = view.Allocator();
        view.dispatch(util::Overloaded(
            [&](const Buddy::TagView<Buddy::Tag::CONS,16>& consatv) {
                cons.emplace(alloc.bumpref(consatv.left), alloc.bumpref(consatv.right));
            },
            [](const auto&) { } // not an atom
        ));
    }

    bool has_value() const { return cons.has_value(); }
    operator bool() const { return has_value(); }

    auto& operator*() { return cons.value(); }
    auto* operator->() { return &cons.value(); }
};

template<>
class ConvertRef<std::pair<SafeView,SafeView>>
{
private:
    SafeRef parent;
    std::optional<std::pair<SafeView, SafeView>> cons;

public:
    ConvertRef(ConvertRef&&) = default;
    ~ConvertRef() = default;

    explicit ConvertRef(SafeRef&& ref) : ConvertRef(SafeView(ref))
    {
        if (cons.has_value()) parent = std::move(ref); // keep it around
    }

    explicit ConvertRef(const SafeView& view) : parent{view.nullref()}, cons{std::nullopt}
    {
        auto& alloc = view.Allocator();
        view.dispatch(util::Overloaded(
            [&](const Buddy::TagView<Buddy::Tag::CONS,16>& consatv) {
                cons.emplace(alloc.view(consatv.left), alloc.view(consatv.right));
            },
            [](const auto&) { } // not an atom
        ));
    }

    bool has_value() const { return cons.has_value(); }
    operator bool() const { return has_value(); }

    auto& operator*() { return cons.value(); }
    auto* operator->() { return &cons.value(); }
};

} // SafeConv namespace

#endif // SAFEREF_H
