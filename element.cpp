/* element arena
 *
 * creates a fixed size arena, which
 * stores refcounted elements of type ATOM, CONS, ERROR, FUNCx
 */

#include <element.h>
#include <elconcept.h>

#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>

using enum ElType;
using namespace ElementConcept;

static constexpr ElType MAX_ELTYPE{FUNC};

template<ElType Target, ElType ET=static_cast<ElType>(0)>
static consteval int concept_offset()
{
    if constexpr (ET == Target) {
        return 0;
    } else if constexpr (ET != MAX_ELTYPE) {
        constexpr ElType NEXT{static_cast<int>(ET) + 1};
        return ElConcept<ET>::variants + concept_offset<Target,NEXT>();
    } else {
        static_assert(false);
    }
}

template<ElType ET=static_cast<ElType>(0)>
static ElType get_eltype(ElBaseType basetype)
{
    if constexpr (ET == MAX_ELTYPE) {
        return ET;
    } else if (basetype < concept_offset<ET>() + ElConcept<ET>::variants) {
        return ET;
    } else {
        constexpr ElType NEXT{static_cast<int>(ET) + 1};
        return get_eltype<NEXT>(basetype);
    }
}

ElBaseType ElRefViewHelper::elbasetype(ElType et, int variant)
{
    switch (et) {
    case ATOM: return concept_offset<ATOM>() + variant;
    case CONS: return concept_offset<CONS>() + variant;
    case ERROR: return concept_offset<ERROR>() + variant;
    case FUNC: return concept_offset<FUNC>() + variant;
    }
    return std::numeric_limits<ElBaseType>::max();
}

ElType ElRefViewHelper::eltype(ElBaseType basetype)
{
    return get_eltype(basetype);
}

template<ElType ET>
std::optional<ElConcept<ET>> ElRefViewHelper::convert(ElView ev)
{
    constexpr int offset = concept_offset<ET>();
    if (ev.m_el) {
        const auto type = ev.m_el->get_type();
        if (offset <= type && type < offset + ElConcept<ET>::variants) {
            const int variant = type - offset;
            return ElConcept<ET>(variant, ev);
        }
    }
    return std::nullopt;
}

template<ElType ET>
ElementConcept::ElConcept<ET> ElRefViewHelper::set_type(ElRef& er, int variant)
{
    constexpr int offset = concept_offset<ET>();
    er.m_el->set_type(offset + variant);
    return ElConcept<ET>(variant, er);
}

template std::optional<ElConcept<ATOM>> ElRefViewHelper::convert(ElView ev);
template std::optional<ElConcept<CONS>> ElRefViewHelper::convert(ElView ev);
template std::optional<ElConcept<ERROR>> ElRefViewHelper::convert(ElView ev);
template std::optional<ElConcept<FUNC>> ElRefViewHelper::convert(ElView ev);

template ElementConcept::ElConcept<ATOM> ElRefViewHelper::set_type(ElRef& ev, int variant);
template ElementConcept::ElConcept<CONS> ElRefViewHelper::set_type(ElRef& ev, int variant);
template ElementConcept::ElConcept<ERROR> ElRefViewHelper::set_type(ElRef& ev, int variant);
template ElementConcept::ElConcept<FUNC> ElRefViewHelper::set_type(ElRef& ev, int variant);

void ElRefViewHelper::decref(ElRef&& el)
{
    ElRef rest{nullptr};

    while (el) {
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
                ElRefViewHelper::init_as<CONS,0>(el, std::move(b), std::move(rest));
                rest = el.move();
                el = a.move();
            } else {
                if (b) a = b.move();
                delete el.m_el; // XXX Allocator
                el.m_el = nullptr;
                el = a.move();
            }
        }
        while (!el && rest) {
            ElRef next{nullptr};
            el.visit([&](auto d) { d.dealloc(el, next); });
            delete rest.m_el; // XXX Allocator
            rest.m_el = nullptr;
            rest = next.move();
        }
    }
}

void test(ElRef&& er)
{
    ElRef left, right;
    er | util::Overloaded(
        [&](ElConcept<CONS> cons) {
            left = cons.left.copy();
            right = cons.right.copy();
        },
        [&](auto) {
            // no-op
        }
    );
    if (left && right) {
        er.reset();
        // ...
    }
}

