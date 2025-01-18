#ifndef ELIMPL_H
#define ELIMPL_H

#include <elem.h>
#include <element.h>
#include <elconcept.h>
#include <arena.h>

#include <span.h>
#include <attributes.h>

#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>

class Arena;

template<ElType ET, ElConcept<ET>::V V>
struct ElVariant;

class ElConceptHelper
{
protected:
    template<ElType ET, ElConcept<ET>::V V>
    friend struct ElVariant;

    template<ElType ET>
    static Elem* get_elem(const ElConcept<ET>& ec) { return ec.elview.m_el; }

public:
    template<typename EC, EC::V V=0, typename Fn>
    static void mutate(EC& ec, Fn&& fn)
    {
        if (ec.variant == V) {
            ElVariant<typename EC::ET,V> ev(ec);
            return fn(ev);
        } else if constexpr (V < V.LAST) {
            return mutate<EC,V+1>(ec, fn);
        }
    }

    template<typename EC, EC::V V=0, typename Fn>
    static void visit(const EC& ec, Fn&& fn)
    {
        if (ec.variant == V) {
            const ElVariant<typename EC::ET,V> ev(ec);
            return fn(ev);
        } else if constexpr (V < V.LAST) {
            return visit<EC,V+1>(ec, fn);
        }
    }
};

template<ElType ET, ElConcept<ET>::V V>
struct ElVariant
{
    using EC = ElConcept<ET>;
    static constexpr auto variant = V;

    const EC& ec;
    ElData<ET,V>& eldata;

    explicit ElVariant(const EC& ec LIFETIMEBOUND) : ec{ec}, eldata{ElConceptHelper::get_elem(ec)->template data<ElData<ET,V>>()} { }
    ElVariant(const ElVariant&) = delete;
};

template<typename EC, EC::V V>
using ElConceptVariant = ElVariant<typename EC::ET, V>;

template<> struct ElData<ATOM,0>
{
    int64_t n{0};
    ElData(int64_t n) : n{n} { };
};

template<> struct ElData<CONS,0>
{
    ElRef left, right;
    ElData(ElRef&& left, ElRef&& right) : left{left.move()}, right{right.move()} { }
};

template<> struct ElData<ERROR,0>
{
    // filename and line number?
};

template<> struct ElData<FUNC,0>
{
    static constexpr uint8_t functypes = ElConceptDef<FUNC>::simple_func_types;
    ElRef extdata;
    Bounded<functypes> type;

    static_assert(decltype(type)::in_range(Func::BLLEVAL));

    ElData(Arena& arena, const Bounded<functypes>& type);
};

#if 0
template<> struct ElData<FUNC,1>
{
    ElRef d;
    CSHA256* int_state;
};
#endif

#endif // ELIMPL_H
