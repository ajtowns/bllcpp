/* element arena
 *
 * creates a fixed size arena, which
 * stores refcounted elements of type ATOM, CONS, ERROR, FUNCx
 */

#ifndef ELEMENT_H
#define ELEMENT_H

#include <elem.h>
#include <span.h>
#include <attributes.h>
#include <overloaded.h>
#include <elconcept.h>
#include <bounded.h>

#include <logging.h>

#include <limits>
#include <memory>
#include <optional>
#include <stddef.h>
#include <stdint.h>
#include <utility>

class ElRef;
class ElView;

class ElRefViewHelper
{
protected:
    friend ElRef;
    friend ElView;

    template<typename ED, typename... T>
    ED* inplace_new(Elem* el, T&&... args)
    {
        return new (el) ED{std::forward<decltype(args)>(args)...};
    }

#if 0
    template<ElType ET, int V, typename... T>
    static ElConcept<ET> init_as(Elem* el, T&&... args)
    {
        static_assert(false);
    }
#endif

    template<typename Fn>
    static constexpr void per_type(Fn&& fn)
    {
        std::apply([&](auto&&... args) { (fn(args), ...); }, ElTypeOrder{});
    }

    template<ElType Target>
    static consteval int concept_offset()
    {
        int offset = 0;
        bool done = false;
        per_type(util::Overloaded(
            [&](Target) { done = true; },
            [&](auto et) {
                if (!done) offset += ElConcept<decltype(et)>::variants;
            }
        ));
        if (!done) throw std::out_of_range("not a valid ElType");
        return offset;
    }

    static std::string to_string(ElView ev, bool in_list=false);
};

class ElView
{
protected:
    friend class ElRef;
    friend class ElRefViewHelper;

    const Elem* m_el{nullptr}; // cannot modify Elem

    explicit ElView(Elem* other LIFETIMEBOUND)
    {
        m_el = other;
    }

public:
    ElView() = default;
    ~ElView() = default;

    ElView(const ElView& other LIFETIMEBOUND) = default;
    ElView(ElView&& other) {
        m_el = other.m_el;
        other.m_el = nullptr;
    }


    ElView& operator=(const ElView& other)
    {
        m_el = other.m_el;
        return *this;
    }

    ElView& operator=(ElView&& other)
    {
        m_el = other.m_el;
        other.m_el = nullptr;
        return *this;
    }

    void reset()
    {
        m_el = nullptr;
    }

    operator bool() const { return m_el != nullptr; }

    template<ElType ET>
    bool is()
    {
        if (!m_el) return false;
        constexpr auto offset = ElRefViewHelper::concept_offset<ET>();
        return (offset <= m_el->get_type() && m_el->get_type() < offset + ElConcept<ET>::variants);
    }

    bool is_nil()
    {
        auto ec = get<ATOM>();
        return (ec && ec->data().size() == 0);
    }

    template<ElType ET>
    std::optional<ElConcept<ET>> get() LIFETIMEBOUND
    {
        if (is<ET>()) {
            return ElConcept<ET>(*m_el);
        } else {
            return std::nullopt;
        }
    }

    template<typename Fn>
    void visit(Fn&& fn)
    {
        if (!m_el) return;
        ElRefViewHelper::per_type([&](auto et) {
            using ET = decltype(et);
            if (is<ET>()) {
                fn(ElConcept<ET>(*m_el));
            }
        });
    }

    template <typename... Ts, typename... Fs>
    constexpr decltype(auto) operator| (util::Overloaded<Fs...> const& match) {
        return visit(match);
    }

    std::string to_string() const { return ElRefViewHelper::to_string(*this); };
};

class ElRef
{
protected:
    friend class ElRefViewHelper;

    Elem* m_el{nullptr};

    explicit ElRef(Elem& el) : m_el{&el} { }

    static void decref(Elem* el); // recursively free elements

    template<ElType ET>
    friend class ElConcept;

    Elem* steal()
    {
        Elem* el = m_el;
        m_el = nullptr;
        return el;
    }

public:
    ElRef() = delete;
    ElRef(std::nullptr_t) : m_el{nullptr} { }
    ElRef(const ElRef&) = delete;
    ElRef& operator=(const ElRef& other) = delete;

    explicit ElRef(ElRef&& other) : m_el{other.m_el} { other.m_el = nullptr; }

    explicit ElRef(Elem*&& other) : m_el{other} { other = nullptr; }

    explicit ElRef(const ElView& elv) : m_el{const_cast<Elem*>(elv.m_el)} { if (m_el) m_el->incref(); }

    ~ElRef()
    {
        if (m_el != nullptr) reset();
    }

    void reset()
    {
        ElRef::decref(m_el);
        m_el = nullptr;
    }

    ElRef& operator=(ElRef&& other)
    {
        m_el = other.m_el;
        other.m_el = nullptr;
        return *this;
    }

    static ElRef copy_of(Elem* other)
    {
        if (other) {
            other->incref();
            LogTrace(BCLog::BLL, "copied elref %p refcount=%d\n", other, other->get_refcount());
        }
        return ElRef{std::move(other)};
    }

    static ElRef copy_of(const Elem* other) { return copy_of(const_cast<Elem*>(other)); }
    static ElRef copy_of(const ElView& other) { return copy_of(const_cast<Elem*>(other.m_el)); }

    ElRef copy()
    {
        return ElRef::copy_of(m_el);
    }

    // allows writing `return x.move();` instead of having
    // to write `return ElRef{std::move(x)};` due to the move
    // constructor being explicit
    ElRef move()
    {
        return ElRef{std::move(*this)};
    }

    ElView view() const LIFETIMEBOUND
    {
        return ElView{m_el};
    }

    operator ElView() const LIFETIMEBOUND
    {
        return view();
    }

    operator bool() const { return m_el != nullptr; }

#if 0
    template<ElType ET, int V, typename... T>
    ElConcept<ET> init_as(T&&... args)
    {
        return ElRefViewHelper::init_as<ET,V>(*this, std::forward<decltype(args)>(args)...);
    }
#endif

    std::string to_string() const { return ElRefViewHelper::to_string(view()); };
};

static_assert(sizeof(ElRef) == sizeof(Elem*));
static_assert(sizeof(ElView) == sizeof(Elem*));

#endif // ELEMENT_H
