/* element arena
 *
 * creates a fixed size arena, which
 * stores refcounted elements of type ATOM, CONS, ERROR, FUNCx
 */

#include <element.h>
#include <elconcept.h>
#include <elimpl.h>

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


template<ElType ET>
ElConcept<ET> ElRefViewHelper::convert(const ElView& ev)
{
    constexpr int offset = concept_offset<ET>();
    using EC = ElConcept<ET>;
    const typename EC::V variant = EC::V::make_checked(ev.m_el->get_type() - offset);
    return EC(variant, ev);
}

template ElConcept<ATOM> ElRefViewHelper::convert(const ElView& ev);
template ElConcept<CONS> ElRefViewHelper::convert(const ElView& ev);
template ElConcept<ERROR> ElRefViewHelper::convert(const ElView& ev);
template ElConcept<FUNC> ElRefViewHelper::convert(const ElView& ev);

template<ElType ET>
ElConcept<ET> ElRefViewHelper::set_type(ElRef& er, typename ElConcept<ET>::V variant)
{
    constexpr int offset = concept_offset<ET>();
    er.m_el->set_type(offset + variant);
    return ElConcept<ET>(variant, er);
}

template ElConcept<ATOM> ElRefViewHelper::set_type(ElRef& ev, typename ElConcept<ATOM>::V variant);
template ElConcept<CONS> ElRefViewHelper::set_type(ElRef& ev, typename ElConcept<CONS>::V variant);
template ElConcept<ERROR> ElRefViewHelper::set_type(ElRef& ev, typename ElConcept<ERROR>::V variant);
template ElConcept<FUNC> ElRefViewHelper::set_type(ElRef& ev, typename ElConcept<FUNC>::V variant);

void ElRefViewHelper::decref(ElRef&& el)
{
    ElRef rest{nullptr};

    while (el) {
LogTrace(BCLog::BLL, "decref %p refcount=%d\n", el.m_el, el.m_el->get_refcount());
        if (!el.m_el->decref()) {
            el.m_el = nullptr;
        } else {
            ElRef a{nullptr}, b{nullptr};
            el.visit([&](auto d) { d.dealloc(a, b); });

            if (a && a.m_el->get_refcount() > 1) {
                (void)a.m_el->decref();
                a.m_el = nullptr;
            }
            if (b && a.m_el->get_refcount() > 1) {
                (void)b.m_el->decref();
                b.m_el = nullptr;
            }
            if (a && b) {
                el.m_el->incref(); // need to keep this around for a bit
                ElRefViewHelper::init_as<CONS,0>(el, std::move(b), std::move(rest));
                rest = el.move();
                el = a.move();
            } else {
                if (b) a = b.move();
                LogTrace(BCLog::BLL, "Deleted %d at %p\n", el.m_el->get_type(), el.m_el);
                delete el.m_el; // XXX Allocator
                el.m_el = nullptr;
                el = a.move();
            }
        }
        while (!el && rest) {
            ElRef next{nullptr};
            rest.visit([&](auto d) { d.dealloc(el, next); });
            LogTrace(BCLog::BLL, "Deleted (2) %d at %p\n", rest.m_el->get_type(), rest.m_el);
            delete rest.m_el; // XXX Allocator
            rest.m_el = nullptr;
            rest = next.move();
        }
    }
}

class ElConceptHelper
{
public:
    template<typename EC, EC::V V=0, typename Fn>
    static void mutate(EC& ec, Fn&& fn)
    {
        if (ec.variant == V) return fn(ec.elview.m_el->template data<typename ElVariant<typename EC::ET,V>::ElData>());
        if constexpr (V < V.LAST) {
            return mutate<EC,V+1>(ec, fn);
        }
    }

    template<typename EC, EC::V V=0, typename Fn>
    static void visit(const EC& ec, Fn&& fn)
    {
        if (ec.variant == V) return fn(ec.elview.m_el->template data<typename ElVariant<typename EC::ET,V>::ElData>());
        if constexpr (V < V.LAST) {
            return visit<EC,V+1>(ec, fn);
        }
    }

};

void ElConcept<CONS>::dealloc(ElRef& child1, ElRef& child2)
{
    ElConceptHelper::mutate(*this, [&](ElVariant<CONS,0>::ElData& eld) {
        child1 = eld.left.move();
        child2 = eld.right.move();
    });
}

void ElConcept<FUNC>::dealloc(ElRef& child1, ElRef& child2)
{
    (void)child1; (void)child2;
    return;
}

ElView ElConcept<CONS>::left()
{
    ElView res;
    ElConceptHelper::visit(*this, [&](const ElVariant<CONS,0>::ElData& eld) {
        res = eld.left.view();
    });
    return res;
}

ElView ElConcept<CONS>::right()
{
    ElView res;
    ElConceptHelper::visit(*this, [&](const ElVariant<CONS,0>::ElData& eld) {
        res = eld.right.view();
    });
    return res;
}

Span<const uint8_t> ElConcept<ATOM>::data() const
{
    static const uint8_t nildata[0]{};
    Span<const uint8_t> res;
    ElConceptHelper::visit(*this, [&](const ElVariant<ATOM,0>::ElData& eld) {
        if (eld.n == 0) res = Span<const uint8_t>(nildata, 0);
        else res = Span<const uint8_t>(reinterpret_cast<const uint8_t*>(&eld.n), sizeof(eld.n));
    });
    return res;
}

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
        [&](ElConcept<FUNC>) { }
    ));
    if (in_list) {
        return " . " + res + ")";
    } else {
        return res;
    }
}
