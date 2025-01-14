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

#include <logging.h>

#include <limits>
#include <memory>
#include <optional>
#include <stddef.h>
#include <stdint.h>
#include <utility>

template<bool OWNED> class ElRefView;
using ElRef = ElRefView<true>;
using ElView = ElRefView<false>;

struct ElTypeTag { };
struct ATOM : ElTypeTag { };
struct CONS : ElTypeTag { };
struct ERROR : ElTypeTag { };
struct FUNC : ElTypeTag { };

using ElTypeOrder = std::tuple<ATOM,CONS,ERROR,FUNC>;

template<typename T>
concept ElType = std::is_base_of_v<ElTypeTag,T>;

template<uint8_t>
class Bounded;

template<ElType>
struct ElConceptDef;

template<ElType>
class ElConcept;

template<ElType ET,Bounded<ElConceptDef<ET>::variants> V>
struct ElVariant;

class ElRefViewHelper
{
private:
    template<ElType ET>
    static ElConcept<ET> set_type(ElRef& er, typename ElConcept<ET>::V v);

protected:
    friend ElRef;
    friend ElView;

    static void decref(ElRef&& el); // recursively free elements

    /* return it as an ET, having already check that it's in range */
    template<ElType ET>
    static ElConcept<ET> convert(const ElView& ev LIFETIMEBOUND);

    template<ElType ET, int V, bool O, typename... T>
    static ElConcept<ET> init_as(ElRefView<O>& er, T&&... args)
    {
        ElConcept<ET> res = set_type<ET>(er, V);

        using ECV = ElVariant<ET,V>;
        er.template inplace_new<typename ECV::ElData>(std::forward<decltype(args)>(args)...);
        return res;
    }

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
};

template<bool OWNED>
class ElRefView
{
private:
    static constexpr bool owned = OWNED;

    friend class ElRefView<!OWNED>;
    friend class ElRefViewHelper;
    friend class ElConceptHelper;

    Elem* m_el{nullptr};

public:
    ElRefView() = default;

    static ElRef takeover(Elem* el)
    {
        ElRef er;
        er.m_el = el;
        return er.move();
    }

    ~ElRefView()
    {
        if (m_el != nullptr) reset();
    }

    void reset()
    {
        if constexpr (OWNED) {
            ElRefViewHelper::decref(std::move(*this));
        }
        m_el = nullptr;
    }

    // creating an owned copy requires moving from an owned element
    explicit ElRefView(ElRef&& other) requires(OWNED)
    {
        m_el = other.m_el;
        other.m_el = nullptr;
    }

    explicit ElRefView(Elem*&& other) requires(OWNED)
    {
        m_el = other;
        other = nullptr;
    }

    ElRef& operator=(ElRef&& other) requires(OWNED)
    {
        m_el = other.m_el;
        other.m_el = nullptr;
        return *this;
    }

    // creating a view just requires something to view, and a lifetimebound
    template<bool O>
    explicit ElRefView(const ElRefView<O>& other LIFETIMEBOUND) requires(!OWNED)
    {
        m_el = other.m_el;
    }

    explicit ElRefView(Elem* other LIFETIMEBOUND) requires(!OWNED)
    {
        m_el = other;
    }

    ElView& operator=(ElView& other) requires(!OWNED)
    {
        m_el = other.m_el;
        return *this;
    }

    template<typename ElData, typename... T>
    ElData* inplace_new(T&&... args)
    {
        return new (m_el) ElData{std::forward<decltype(args)>(args)...};
    }

    ElRef copy()
    {
        Elem* cp = m_el;
        if (cp) {
            cp->incref();
            LogTrace(BCLog::BLL, "copied elref %p refcount=%d\n", m_el, m_el->get_refcount());
        }
        return ElRef{std::move(cp)};
    }

    // allows writing `return x.move();` instead of having
    // to write `return ElRef{std::move(x)};` due to the move
    // constructor being explicit
    ElRef move() requires(OWNED)
    {
        return ElRef{std::move(*this)};
    }

    ElView view() const LIFETIMEBOUND
    {
        return ElView{m_el};
    }

    operator bool() const { return m_el != nullptr; }

    template<ElType ET, int V, typename... T>
    ElConcept<ET> init_as(T&&... args)
    {
        return ElRefViewHelper::init_as<ET,V>(*this, std::forward<decltype(args)>(args)...);
    }

    template<ElType ET>
    bool is()
    {
        if (!m_el) return false;

        constexpr auto offset = ElRefViewHelper::concept_offset<ET>();
        return (offset <= m_el->get_type() && m_el->get_type() < offset + ElConcept<ET>::variants);
    }

    template<ElType ET>
    std::optional<ElConcept<ET>> get() LIFETIMEBOUND
    {
        if (is<ET>()) {
            return ElRefViewHelper::convert<ET>(view());
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
                fn(ElRefViewHelper::convert<ET>(view()));
            }
        });
    }

    template <typename... Ts, typename... Fs>
    constexpr decltype(auto) operator| (util::Overloaded<Fs...> const& match) {
        return visit(match);
    }
};

static_assert(sizeof(ElRef) == sizeof(Elem*));
static_assert(sizeof(ElView) == sizeof(Elem*));

/* todo:
 *   Allocator param for elements and data, to track memory usage etc
 *   need/want to be able to mass free data, i guess.
 *   construction of new elrefs via allocator/arena? (arena.Atom(420))
 *   Atom(0) and Atom(1) deduping
 *   assert on deref(0) ?
 *
 *   how to distinguish internal allocations (Func) and external allocations
 *   (Cons) and early(?) allocations (data for Atom so there's something to populate
 *   as we're calculating?)
 *
 *   (de)serialization to bytes (and text?)
 *      -- incomplete cons when deserializing (while refcount==1)
 *         (really, this is a partial construction where the partial is
 *         acting as a stack, and is then removed and reallocated when complete)
 *      -- componentwise-serialization, concatenated/trimmed when complete?
 *
 *   math maybe?
 *     maybe all math should be done modulo N? but is the range [0,N) or [-N/2,N) ?
 *     would we change "softfork" to "ctx" or something in that case? that would give
 *     a stronger argument for (verify) opcode.
 *
 *   bllcons / smallatom / extatom
 *
 *   func type (partially evaluated operation)
 *   opcodes
 *   env access
 *   program evaluation
 */

/* what does usage look like?

   auto arena = Arena::New(5<<20); // new 5MiB arena

   auto x = arena.Atom(543); // create a new atom, with value 543, refcount 1

   {
       auto b = x.copy(); // bump x's refcount and copy it
       if (auto bat = b.get<ATOM>()) {
           // b was an atom, *bat has atom-specific funcs, bat->foo()
           ...
       }
   } // decrement x's refcount as b is RAII'ed


   class op_add {
       static ElRef func(Arena& arena, ElRef&& state, ElRef&& args)
       {
           auto st = state.get<ATOM>();
           if (!st) return arena.Error(INTERNAL);

           if (auto argc = args.get<CONS>()) {
               if (auto arg = argc.left.get<ATOM>()) {
                    bignum res{bignum{st.data()} + bignum{arg.data()}};
                    return arena.Func<op_add>(arena.Atom(res));
               } else {
                    return arena.Error(EXPECTED_ATOM);
               }
           } else if (args.is_nil()) {
               return std::move(state);
           } else {
               return arena.Error(EXPECTED_PROPER_LIST);
           }
       }
   };

----
   I want:
      * concepts: atom, cons, error, func
      * memory repr: {atom, small atom, ext atom} {cons, simp cons} {func func-stateful}
      * detailed implementations: (op_add, etc)
         -- structure?

   detailed implementation{uint8_t} -> memory repr -> type
 */

#endif // ELEMENT_H
