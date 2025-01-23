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

void ElRef::decref(Elem* el)
{
    Elem* rest{nullptr};
    LogTrace(BCLog::BLL, "decref(%p)\n", el);
    while (el) {
        LogTrace(BCLog::BLL, "decref %p type=%d refcount=%d rest=%p\n", el, el->get_type(), el->get_refcount(), rest);
        if (!el->decref()) {
            el = nullptr;
        } else {
            Elem* a{nullptr};
            Elem* b{nullptr};
            ElView{el}.visit([&](auto d) { d.dealloc(a, b); });

            if (!a || !a->decref()) a = nullptr;
            if (!b || !a->decref()) b = nullptr;
            if (a && b) {
                LogTrace(BCLog::BLL, "Preserved container of %d at %p refcount=%d\n", el->get_type(), el, el->get_refcount());
                ElConcept<CONS>::init_as(*el, ElView(b), ElView(rest));
                rest = el;
                el = a;
            } else {
                if (b) std::swap(a, b);
                LogTrace(BCLog::BLL, "Deleted %d at %p\n", el->get_type(), el);
                //delete el; // XXX Allocator
                el = nullptr;
                el = a;
            }
        }
        while (!el && rest) {
            Elem* next{nullptr};
            ElView(rest).visit([&](auto d) { d.dealloc(el, next); });
            LogTrace(BCLog::BLL, "Deleted (2) %d at %p\n", rest->get_type(), rest);
            //delete rest; // XXX Allocator
            rest = next;
        }
    }
}

void ElConcept<CONS>::dealloc(Elem*& child1, Elem*& child2)
{
    ElVariantHelper<CONS>::mutate(*this, [&](ElData<CONS,0>& eldata) {
        child1 = eldata.left.steal();
        child2 = eldata.right.steal();
    });
}

void ElConcept<FUNC>::dealloc(Elem*& child1, Elem*& child2)
{
    (void)child2;
    ElVariantHelper<FUNC>::mutate(*this, util::Overloaded(
        [&](FuncExt& funcextdata) {
            child1 = funcextdata.extdata.steal();
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
    ElVariantHelper<ATOM>::visit(*this, [&](const ElData<ATOM,0>& eldata) {
        if (eldata.n == 0) res = Span<const uint8_t>(nildata, 0);
        else res = Span<const uint8_t>(reinterpret_cast<const uint8_t*>(&eldata.n), sizeof(eldata.n));
    });
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
            res = strprintf("FUNC<%s>", ElConceptDef<FUNC>::func_name[fn.variant()]);
        }
    ));
    if (in_list) {
        return " . " + res + ")";
    } else {
        return res;
    }
}


using func_name_array = std::array<std::string, ElConceptDef<FUNC>::variants>;

#define CASE_FUNC_NAME(F) case F: res[F] = #F; break
static func_name_array gen_func_names()
{
    func_name_array res;
    for (size_t i = 0; i < res.size(); ++i) {
        switch(static_cast<Func::Func>(i)) {
            CASE_FUNC_NAME(Func::QUOTE);
            CASE_FUNC_NAME(Func::BLLEVAL);
        }
    }
    return res;
}
#undef CASE_FUNC_NAME

const func_name_array ElConceptDef<FUNC>::func_name = gen_func_names();

template<ElType ET, ElConcept<ET>::V Variant, typename... Args>
static ElConcept<ET> init_as_helper(Elem& el, Args&&... args)
{
    el.set_type(ConceptOffset<ET>() + Variant);
    new(&el.data_rw<uint8_t>()) ElData<ET,Variant>{std::forward<decltype(args)>(args)...};
    return ElConcept<ET>(el);
}

ElConcept<ATOM> ElConcept<ATOM>::init_as(Elem& el, int64_t n) { return init_as_helper<ATOM,0>(el, n); }
ElConcept<CONS> ElConcept<CONS>::init_as(Elem& el, ElView left, ElView right) { return init_as_helper<CONS,0>(el, left, right); }
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
