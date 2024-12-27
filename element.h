/* element arena
 *
 * creates a fixed size arena, which
 * stores refcounted elements of type ATOM, CONS, ERROR, FUNCx
 */

#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>

enum class ElType : uint8_t {
    ATOM = 0,
    CONS = 1,
    ERROR = 2,
    // EXTATOM -- ATOM, but the data is unowned and shouldn't be freed
    // SMALLATOM -- ATOM, but no need for a pointer, just store the data directly
    // SIMPCONS -- CONS, but all children are either SIMPCONS or ATOM
};
template<uint8_t fnum>
constexpr ElType FUNC{3+fnum};

using enum ElType;

class Element;

struct GenElData
{
    void* a;
    void* b;
};

template<ElType T>
struct ElData { static constexpr bool invalid_eldata = true; };

template<ElType T>
concept ValidElData = requires(ElData<T>& ed, Element* a, Element* b)
{
    { ed.dealloc(&a, &b) };
};

class Element
{
private:
    uint32_t refcount;
    ElType type;
    alignas(GenElData) uint8_t eldata[sizeof(GenElData)];

public:
    static Element* bumpref(Element* el) { if (el) ++el->refcount; return el; }
    static void deref(Element* el);

    template<ElType T>
    ElData<T>* get()
    {
        static_assert(sizeof(ElData<T>) <= sizeof(GenElData) && alignof(ElData<T>) <= alignof(GenElData));

        if (type == T) {
            return reinterpret_cast<ElData<T>*>(&eldata);
        } else {
            return nullptr;
        }
    }

    template<ElType T>
    ElData<T>& force_as()
    {
        static_assert(sizeof(ElData<T>) <= sizeof(GenElData) && alignof(ElData<T>) <= alignof(GenElData));

        type = T;
        return *reinterpret_cast<ElData<T>*>(&eldata);
    }

    template<uint8_t N=0, typename R=void, typename Fn>
    R visit(Fn&& fn)
    {
        constexpr auto ET{static_cast<ElType>(N)};
        if constexpr (ValidElData<ET>) {
            if (type == ET) {
                return fn(reinterpret_cast<ElData<ET>*>(&eldata));
            }
        }
        if constexpr (N < std::numeric_limits<uint8_t>::max()) {
            return visit<N+1,R>(std::move(fn));
        } else {
            return R();
        }
    }
};

template<>
struct ElData<ATOM>
{
    size_t size;  // in bytes
    uint8_t* atom;

    void dealloc(Element**, Element**) { free(atom); atom = nullptr; size = 0; }
};

template<>
struct ElData<CONS>
{
    Element* left;
    Element* right;

    void dealloc(Element** a, Element** b) { *a = left; *b = right; left = right = nullptr; }

    std::pair<Element*,Element*> copy_children()
    {
        std::pair<Element*,Element*> ret{Element::bumpref(left), Element::bumpref(right)};
        return ret;
    }
};

template<>
struct ElData<ERROR>
{
    size_t size;
    uint8_t* msg;

    void dealloc(Element**, Element**) { free(msg); msg = nullptr; size = 0; }
};

// smart, read-only Element pointers
class ElRef
{
private:
    Element* m_el{nullptr};

public:
    ElRef() = default;

    ElRef(ElRef&& other)
    {
        m_el = other.m_el;
        other.m_el = nullptr;
    }

    ElRef(ElRef& other)
    {
        m_el = Element::bumpref(other.m_el);
    }

    ElRef& operator=(ElRef& other)
    {
        if (m_el != other.m_el) {
            Element::deref(m_el);
            m_el = Element::bumpref(other.m_el);
        }
        return *this;
    }

    ~ElRef() {
        Element::deref(m_el);
    }

    Element* copy()
    {
        return Element::bumpref(m_el);
    }

    Element* steal()
    {
        Element* el = m_el;
        m_el = nullptr;
        return el;
    }

    void takeover(Element* el)
    {
        if (m_el != el) Element::deref(m_el);
        m_el = Element::bumpref(el);
    }

    template<ElType T>
    const ElData<T>* get()
    {
        return (m_el ? m_el->get<T>() : nullptr);
    }

    template<typename R=void, typename Fn>
    R visit(Fn&& fn)
    {
        if (!m_el) return R();
        m_el->visit([&](const auto* d) { return fn(d); });
    }
};

/* todo:
 *   Allocator param for elements and data, to track memory usage etc
 *   need/want to be able to mass free data, i guess.
 *   construction of new elrefs via allocator/arena? (arena.Atom(420))
 *   Atom(0) and Atom(1) deduping
 *   assert on deref(0) ?
 *
 *   (de)serialization to bytes (and text?)
 *      -- incomplete cons when deserializing (while refcount==1)
 *         (really, this is a partial construction where the partial is
 *         acting as a stack, and is then removed and reallocated when complete)
 *      -- componentwise-serialization, concatenated/trimmed when complete?
 *
 *   math maybe?
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
 */
