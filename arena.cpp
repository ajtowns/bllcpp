
#include <arena.h>
#include <element.h>
#include <elconcept.h>
#include <elimpl.h>

Arena::Arena()
{
    m_nil = New<ATOM,0>(int64_t{0});
}

ElRef Arena::mkel(int64_t v)
{
    return (v == 0 ? nil() : New<ATOM>(v));
}

ElRef Arena::mkcons(ElRef&& a, ElRef&& b)
{
    return New<CONS>(a.move(), b.move());
}

template<uint8_t V>
static ElRef mkfn_check(Arena& arena, uint8_t variant)
{
    static_assert(V < ElConceptDef<FUNC>::variants);
    if constexpr (V+1 < ElConceptDef<FUNC>::variants) {
        if (variant != V) {
            return mkfn_check<V+1>(arena, variant);
        }
    }
    return New<FUNC,V>(arena);
}

ElRef Arena::mkfn(typename ElConceptDef<FUNC>::FnId fn)
{
    constexpr auto sft = ElConceptDef<FUNC>::simple_func_types;
    if constexpr (ElConceptDef<FUNC>::variants > 1) {
        if (fn >= sft) {
            return mkfn_check<1>(*this, fn - sft + 1);
        }
    }
    return New<FUNC,0>(*this, Bounded<sft>::make_checked(fn));
}

ElRef Arena::error()
{
    return New<ERROR>();
}
