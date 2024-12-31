/* element arena
 *
 * creates a fixed size arena, which
 * stores refcounted elements of type ATOM, CONS, ERROR, FUNCx
 */

#include <sha256.h>

#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>

using ElType = uint8_t;

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
    ElType get_type() const { return type; }
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

    template<ElType N=0, typename R=void, typename Fn>
    R visit(Fn&& fn)
    {
        if constexpr (ValidElData<N>) {
            if (type == N) {
                return fn(reinterpret_cast<ElData<N>*>(&eldata));
            }
        }
        if constexpr (N < std::numeric_limits<uint8_t>::max()) {
            return visit<N+1,R>(std::move(fn));
        } else {
            return R();
        }
    }
};

#define STRUCTELDATA(NAME, TYPENUM) \
    static constexpr ElType NAME{TYPENUM}; \
    template<> struct ElData<NAME>

STRUCTELDATA(ATOM, 0)
{
    size_t size;  // in bytes
    uint8_t* atom;

    void init(size_t s, uint8_t* a) { size = s; atom = a; }
    void dealloc(Element**, Element**) { free(atom); atom = nullptr; size = 0; }
};

STRUCTELDATA(CONS, 1)
{
    Element* left;
    Element* right;

    void init(Element* l, Element* r) { left = l; right = r; }
    void dealloc(Element** a, Element** b) { *a = left; *b = right; left = right = nullptr; }

    std::pair<Element*,Element*> copy_children()
    {
        std::pair<Element*,Element*> ret{Element::bumpref(left), Element::bumpref(right)};
        return ret;
    }
};

STRUCTELDATA(ERROR, 2)
{
    // no data. could add: size_t size; uint8_t* msg;

    void init() { }
    void dealloc(Element**, Element**) { }
};

STRUCTELDATA(SMLATOM, 3)
{
    size_t size;
    uint32_t data;

    void init(int32_t a) 
    {
        bool neg = false;
        if (a < 0) {
            neg = true;
            a = -a;
        }
        if (a == 0) {
            size = 0;
            data = 0;
        } else if (a < (1L << 7)) {
            size = 1;
            data = a | (neg ? 0x80 : 0);
        } else if (a < (1L << 15)) {
            size = 2;
            data = a | (neg ? 0x8000 : 0);
        } else if (a < (1L << 23)) {
            size = 3;
            data = a | (neg ? 0x800000 : 0);
        } else {
            size = 4;
            data = a | (neg ? 0x80000000 : 0);
        }
    }

    void dealloc(Element**, Element**) { data = 0; size = 0; }
};

template<ElType N>
struct FuncData; // { static constexpr bool is_empty_funcdata = true; };

#define FUNCELDATA(NAME, TYPENUM) \
STRUCTELDATA(NAME, TYPENUM) \
{ \
    std::nullptr_t* int_data{nullptr}; \
    Element* ext_data{nullptr}; \
    void init(Element* ed) { ext_data = ed; } \
    void dealloc(Element** a, Element**) { *a = ext_data; free(int_data); ext_data = nullptr; int_data = nullptr; } \
}

#define FUNCELDATA_FUNCDATA(NAME, TYPENUM, FUNCDATA) \
template<> struct FuncData<TYPENUM> { FUNCDATA; }; \
STRUCTELDATA(NAME, TYPENUM) \
{ \
    FuncData<NAME>* int_data{nullptr}; \
    Element* ext_data{nullptr}; \
    void init(Element* ed, FuncData<NAME>* id) { ext_data = ed; int_data = id; } \
    void dealloc(Element** a, Element**) { *a = ext_data; free(int_data); ext_data = nullptr; int_data = nullptr; } \
}

FUNCELDATA(BLLEVAL, 5);
FUNCELDATA(QUOTE, 6);
FUNCELDATA_FUNCDATA(SHA256, 98, CSHA256 hasher);

static_assert(ValidElData<BLLEVAL>);
static_assert(ValidElData<SHA256>);

// EXTATOM -- ATOM, but the data is unowned and shouldn't be freed
// SMALLATOM -- ATOM, but no need for a pointer, just store the data directly
// SIMPCONS -- CONS, but all children are either SIMPCONS or ATOM

// smart, read-only Element pointers
class ElRef
{
private:
    Element* m_el{nullptr};

    // are these useful?
    Element* copy_underlying()
    {
        return Element::bumpref(m_el);
    }

public:
    bool is_nullptr() const { return m_el == nullptr; }

    bool is_error() const { return m_el && m_el->get_type() == ERROR; }

    ElRef() = default;

    ElRef(ElRef&& other)
    {
        m_el = other.m_el;
        other.m_el = nullptr;
    }

    explicit ElRef(ElRef& other)
    {
        m_el = Element::bumpref(other.m_el);
    }

    explicit ElRef(Element*&& other)
    {
        m_el = other;
        other = nullptr;
    }

    ElRef& operator=(ElRef& other)
    {
        if (m_el != other.m_el) {
            Element::deref(m_el);
            m_el = Element::bumpref(other.m_el);
        }
        return *this;
    }

    ElRef& operator=(ElRef&& other)
    {
        Element::deref(m_el);
        m_el = Element::bumpref(other.m_el);
        return *this;
    }


    ~ElRef() {
        Element::deref(m_el);
    }

    ElRef copy()
    {
        return ElRef{Element::bumpref(m_el)};
    }

    operator Element*() &&
    {
        Element* el = m_el;
        m_el = nullptr;
        return el;
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
 */
