#ifndef ELIMPL_H
#define ELIMPL_H

#include <elem.h>
#include <element.h>
#include <elconcept.h>

#include <span.h>
#include <attributes.h>

#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>

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
    struct ElData { };
};

#endif // ELIMPL_H
