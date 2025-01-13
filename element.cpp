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
