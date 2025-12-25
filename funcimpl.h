#ifndef FUNCIMPL_H
#define FUNCIMPL_H

#include <elem.h>
#include <element.h>
#include <elconcept.h>
#include <arena.h>
#include <elimpl.h>

#include <span.h>
#include <attributes.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

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

    explicit FuncExt(Arena&) { }
};

struct FuncExtNil : FuncExt
{
    explicit FuncExtNil(Arena& arena) : FuncExt{arena.nil()} { }
};

// For functions that want to count how many arguments have been processed
struct FuncExtCount
{
    ElRef extdata{nullptr};
    int32_t count{0};

    FuncExtCount() = delete;
    FuncExtCount& operator=(FuncExtCount&&) = delete;

    FuncExtCount(std::nullptr_t) : extdata{nullptr}, count{0} { }
    FuncExtCount(ElRef&& extdata, int32_t count) : extdata{std::move(extdata)}, count{count} { }
    ~FuncExtCount() = default;

    explicit FuncExtCount(Arena&) : extdata{nullptr}, count{0} { }
};

template<> struct ElVariant<FUNC,Func::QUOTE> { using ElData = FuncNone; };
template<> struct ElVariant<FUNC,Func::APPLY> { using ElData = FuncExtCount; };

template<> struct ElVariant<FUNC,Func::OP_HEAD> { using ElData = FuncExtCount; };
template<> struct ElVariant<FUNC,Func::OP_TAIL> { using ElData = FuncExtCount; };
template<> struct ElVariant<FUNC,Func::OP_LIST> { using ElData = FuncExtCount; };
template<> struct ElVariant<FUNC,Func::OP_IF> { using ElData = FuncExtCount; };
template<> struct ElVariant<FUNC,Func::OP_X> { using ElData = FuncExtCount; };
template<> struct ElVariant<FUNC,Func::OP_SUBSTR> { using ElData = FuncExtCount; };

template<> struct ElVariant<FUNC,Func::OP_RC> { using ElData = FuncExt; };
template<> struct ElVariant<FUNC,Func::OP_NOTALL> { using ElData = FuncExt; };
template<> struct ElVariant<FUNC,Func::OP_ALL> { using ElData = FuncExt; };
template<> struct ElVariant<FUNC,Func::OP_ANY> { using ElData = FuncExt; };
template<> struct ElVariant<FUNC,Func::OP_LT_STR> { using ElData = FuncExt; };
template<> struct ElVariant<FUNC,Func::OP_STRLEN> { using ElData = FuncExtNil; };
template<> struct ElVariant<FUNC,Func::OP_CAT> { using ElData = FuncExtNil; };

template<> struct ElVariant<FUNC,Func::OP_ADD> { using ElData = FuncExt; };

template<> struct ElVariant<FUNC,Func::BLLEVAL> { using ElData = FuncNone; };

#if 0
template<> struct ElData<FUNC,1>
{
    CSHA256* int_state;
};
#endif

#endif // FUNCIMPL_H
