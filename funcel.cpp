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
