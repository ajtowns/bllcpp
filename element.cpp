/* element arena
 *
 * creates a fixed size arena, which
 * stores refcounted elements of type ATOM, CONS, ERROR, FUNCx
 */

#include <element.h>
#include <elconcept.h>
#include <arena.h>
#include <elimpl.h>
#include <funcimpl.h>

#include <logging.h>

#include <cstring>
#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>

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

template<typename Fn>
static void mutate_elem(Elem& el, Fn&& fn)
{
    PerConcept([&](auto et) {
        using ET = decltype(et);
        if (TypeIsConcept<ET>(el.get_type())) {
            fn(ElConcept<ET>(el));
        }
    });
}

void ElRef::decref(Elem* el)
{
    ElRef rest{nullptr};
    LogTrace(BCLog::BLL, "decref(%p)\n", el);
    while (el) {
        LogTrace(BCLog::BLL, "decref %p type=%d refcount=%d rest=%p\n", el, el->get_type(), el->get_refcount(), rest.m_el);
        if (!el->decref()) {
            el = nullptr;
        } else {
            ElRef a{nullptr};
            ElRef b{nullptr};
            mutate_elem(*el, [&](auto d) { d.dealloc(a, b); });

            // if this isn't the last refcount for a or b, deal with them
            // immediately by decref'ing and ignoring
            if (a && a.m_el->get_refcount() > 1) {
                LogTrace(BCLog::BLL, "decref a %p type=%d refcount=%d\n", a.m_el, a.m_el->get_type(), a.m_el->get_refcount());
                (void)a.m_el->decref();
                a.m_el = nullptr;
            }
            if (b && b.m_el->get_refcount() > 1) {
                LogTrace(BCLog::BLL, "decref a %p type=%d refcount=%d\n", b.m_el, b.m_el->get_type(), b.m_el->get_refcount());
                (void)b.m_el->decref();
                b.m_el = nullptr;
            }

            // simplify
            if (b && !a) a = b.move();
            if (!rest) rest = b.move();

            if (a && b) {
                LogTrace(BCLog::BLL, "recover %p as CONS\n", el);
                ElConcept<CONS>::init_as(*el, b.move(), rest.move());
                rest = ElRef(std::move(el));
                el = a.steal();
            } else {
                LogTrace(BCLog::BLL, "free %p\n", el);
                //delete el; // XXX Allocator
                el = nullptr;
                el = a.steal();
            }
        }
        if (!el) el = rest.steal();
    }
}

void ElConcept<ATOM>::dealloc(ElRef&, ElRef&)
{
    ElVariantHelper<ATOM>::mutate(*this, util::Overloaded(
        [&](ElData<ATOM,0>&) { },
        [&](ElData<ATOM,1>&) { },
        [&](ElData<ATOM,2>& eldata) {
            delete eldata.owned.data();
        }
    ));
}

void ElConcept<CONS>::dealloc(ElRef& child1, ElRef& child2)
{
    ElVariantHelper<CONS>::mutate(*this, [&](ElData<CONS,0>& eldata) {
        child1 = eldata.left.move();
        child2 = eldata.right.move();
    });
}

void ElConcept<FUNC>::dealloc(ElRef& child1, ElRef&)
{
    ElVariantHelper<FUNC>::mutate(*this, util::Overloaded(
        [&](FuncExt& funcextdata) {
            child1 = funcextdata.extdata.move();
        },
        [&](FuncExtCount& funcextdata) {
            child1 = funcextdata.extdata.move();
        },
        [&](FuncNone&) { }
    ));
    return;
}

ElView ElConcept<CONS>::left() const LIFETIMEBOUND
{
    ElView res;
    ElVariantHelper<CONS>::visit(*this, [&](const ElData<CONS,0>& eldata) {
        res = eldata.left.view();
    });
    return res;
}

ElView ElConcept<CONS>::right() const LIFETIMEBOUND
{
    ElView res;
    ElVariantHelper<CONS>::visit(*this, [&](const ElData<CONS,0>& eldata) {
        res = eldata.right.view();
    });
    return res;
}

Span<const uint8_t> ElConcept<ATOM>::data() const
{
    static const uint8_t nildata[0]{};
    Span<const uint8_t> res;
    ElVariantHelper<ATOM>::visit(*this, util::Overloaded(
        [&](const ElData<ATOM,0>& eldata) {
            if (eldata.n == 0) res = Span<const uint8_t>(nildata, 0);
            else res = Span<const uint8_t>(reinterpret_cast<const uint8_t*>(&eldata.n), sizeof(eldata.n));
        },
        [&](const ElData<ATOM,1>& eldata) { res = eldata.external; },
        [&](const ElData<ATOM,2>& eldata) { res = eldata.owned; }
    ));
    return res;
}

std::optional<int64_t> ElConcept<ATOM>::small_int() const
{
    // XXX this should be taking the span, then seeing if it is as small int,
    // rather than be special cased anyway
    std::optional<int64_t> res = 0;
    ElVariantHelper<ATOM>::visit(*this, util::Overloaded(
        [&](const ElData<ATOM,0>& eldata) { res = eldata.n; },
        [&](const ElData<ATOM,1>&) { res = std::nullopt; },
        [&](const ElData<ATOM,2>&) { res = std::nullopt; }
    ));
    return res;
}

#if 0

ElData<FUNC,0>::ElData(Arena& arena, const Bounded<functypes>& type)
    : type{type}
{
    extdata = arena.nil(); // everything gets a nil for now!
}

#endif

std::string ElRefViewHelper::to_string(ElView ev, bool in_list)
{
    std::string res;
    if (!ev) res = "nullptr";

    ev.visit(util::Overloaded(
        [&](ElConcept<ATOM> d) {
            if (d.data().size() == 0) {
                if (in_list) {
                    res = ")";
                    in_list = false;
                } else {
                    res = "nil";
                }
            } else {
                auto data = d.data();
                if (data.size() == 8 && data[0] != 0 && data[1] == 0 && data[2] == 0 && data[3] == 0 && data[4] == 0 && data[5] == 0 && data[6] == 0 && data[7] == 0) {
                    res = strprintf("%d", data[0]);
                } else {
                    res = strprintf("0x%s", HexStr(data));
                }
            }
        },
        [&](ElConcept<CONS> c) {
            res = strprintf("%s%s%s", (in_list ? " " : "("), to_string(c.left()), to_string(c.right(), /*in_list=*/true));
            in_list = false;
        },
        [&](ElConcept<ERROR>) { res = "ERROR"; },
        [&](ElConcept<FUNC> fn) {
            std::string extra = "";
            ElVariantHelper<FUNC>::visit(fn, util::Overloaded(
                [&](const FuncNone&) { },
                [&](const FuncExt& fe) {
                    extra = strprintf("; %s", fe.extdata.to_string());
                },
                [&](const FuncExtCount& fec) {
                    extra = strprintf("; %s", fec.extdata.to_string());
                }
            ));
            res = strprintf("FUNC<%s%s>", ElConceptDef<FUNC>::func_name[fn.variant()], extra);
        }
    ));
    if (in_list) {
        return " . " + res + ")";
    } else {
        return res;
    }
}

template<ElType ET, ElConcept<ET>::V Variant, typename... Args>
static ElConcept<ET> init_as_helper(Elem& el, Args&&... args)
{
    el.set_type(ConceptOffset<ET>() + Variant);
    new(&el.data_rw<uint8_t>()) ElData<ET,Variant>{std::forward<decltype(args)>(args)...};
    return ElConcept<ET>(el);
}

ElConcept<ATOM> ElConcept<ATOM>::init_as(Elem& el, int64_t n)
{
    return init_as_helper<ATOM,0>(el, n);
}

ElConcept<ATOM> ElConcept<ATOM>::init_as(Elem& el, Arena& arena, Span<const uint8_t> data)
{
    (void)arena; // XXX use arena to allocate memory
    uint8_t* own = new uint8_t[data.size()];
    std::memcpy(own, data.data(), data.size());
    return init_as_helper<ATOM,1>(el, Span<uint8_t>(own, data.size()));
}

ElConcept<ATOM> ElConcept<ATOM>::init_as(Elem& el, Arena& arena, size_t size, Span<uint8_t>& data)
{
    (void)arena; // XXX use arena to allocate memory
    uint8_t* own = new uint8_t[size];
    std::memset(own, 0, size);
    data = Span<uint8_t>(own, size);
    return init_as_helper<ATOM,1>(el, data);
}

ElConcept<ATOM> ElConcept<ATOM>::init_as(Elem& el, Span<const uint8_t> data, ATOM::external_type)
{
    return init_as_helper<ATOM,2>(el, data);
}

ElConcept<CONS> ElConcept<CONS>::init_as(Elem& el, ElRef&& left, ElRef&& right) { return init_as_helper<CONS,0>(el, std::move(left), std::move(right)); }
ElConcept<ERROR> ElConcept<ERROR>::init_as(Elem& el) { return init_as_helper<ERROR,0>(el); }

template<ElConcept<FUNC>::V Variant=0>
static void init_func_as_helper(Elem& el, Arena& arena, Func::Func fnid)
{
    if constexpr (Variant < Variant.LAST) {
        if (fnid != Variant) {
            return init_func_as_helper<Variant+1>(el, arena, fnid);
        }
    }
    el.set_type(ConceptOffset<FUNC>() + Variant);
    new(&el.data_rw<uint8_t>()) ElData<FUNC,Variant>{arena};
}

ElConcept<FUNC> ElConcept<FUNC>::init_as(Elem& el, Arena& arena, Func::Func fnid)
{
    init_func_as_helper(el, arena, fnid);
    return ElConcept<ET>(el);
}
