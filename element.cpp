#include <element.h>
#include <elconcept.h>
#include <arena.h>
#include <elimpl.h>

#include <logging.h>

#include <cstring>
#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>

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
