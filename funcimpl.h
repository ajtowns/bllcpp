#ifndef FUNCIMPL_H
#define FUNCIMPL_H

#include <elem.h>
#include <element.h>
#include <elconcept.h>
#include <arena.h>
#include <elimpl.h>

#include <span.h>
#include <attributes.h>

#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>

template<ElConcept<FUNC>::V Variant>
struct FuncStep
{
     static void step(const ElData<FUNC,Variant>&, StepParams&);
};

// For functions with no internal state, just args and environment
struct FuncNone
{
    FuncNone() = default;
    explicit FuncNone(Arena&) { }
};

// For functions with a single element as internal state
struct FuncExt
{
    ElRef extdata{nullptr};

    FuncExt() = delete;
    FuncExt& operator=(FuncExt&&) = delete;

    FuncExt(std::nullptr_t) : extdata{nullptr} { }
    FuncExt(ElRef&& extdata) : extdata{std::move(extdata)} { }
    ~FuncExt() = default;
};

struct FuncExtNil : FuncExt
{
    explicit FuncExtNil(Arena& arena) : FuncExt{arena.nil()} { }
};

template<>
struct ElVariant<FUNC,Func::BLLEVAL> { using ElData = FuncNone; };

template<>
struct ElVariant<FUNC,Func::QUOTE> { using ElData = FuncNone; };

#if 0
template<> struct ElData<FUNC,1>
{
    CSHA256* int_state;
};
#endif

#endif // FUNCIMPL_H
