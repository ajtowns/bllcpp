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

template<> struct ElVariant<ATOM,0>
{
    struct ElData {
        int64_t n{0};
        ElData(int64_t n) : n{n} { };
    };
};

template<> struct ElVariant<CONS,0>
{
    struct ElData {
        ElRef left, right;
        ElData(ElRef&& left, ElRef&& right) : left{left.move()}, right{right.move()} { }
    };
};

template<> struct ElVariant<ERROR,0>
{
    struct ElData { }; // filename and line number?
};

template<> struct ElVariant<FUNC,0>
{
    struct ElData {
        static constexpr uint8_t functypes = ElConceptDef<FUNC>::simple_func_types;
        ElRef extdata;
        Bounded<functypes> type;

        static_assert(decltype(type)::in_range(Func::BLLEVAL));

        ElData(Arena& arena, const Bounded<functypes>& type);
    };
};

#if 0
template<> struct ElVariant<FUNC,1>
{
    struct ElData {
        ElRef d;
        CSHA256* int_state;
    };
};
#endif

#endif // ELIMPL_H
