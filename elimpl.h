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

template<ElType ET,ElConcept<ET>::V>
struct ElVariant;

template<ElType ET>
class ElVariantHelper
{
protected:
    static uint8_t variant(const Elem& el)
    {
        return el.get_type() - ConceptOffset<ET>();
    }

public:
    using EC = ElConcept<ET>;

    template<EC::V V=0, typename Fn>
    static void mutate(EC& ec, Fn&& fn)
    {
        Elem& el = *const_cast<Elem*>(&ec.get_el());
        if (variant(el) == V) {
            auto& eld = el.data_rw<ElData<ET,V>>();
            return fn(eld);
        } else if constexpr (V < V.LAST) {
            return mutate<V+1>(ec, std::move(fn));
        }
    }

    template<ElConcept<ET>::V V=0, typename Fn>
    static void visit(const EC& ec, Fn&& fn)
    {
        const Elem& el = ec.get_el();
        if (variant(el) == V) {
            auto& eld = el.data_ro<ElData<ET,V>>();
            return fn(eld);
        } else if constexpr (V < V.LAST) {
            return visit<V+1>(ec, std::move(fn));
        }
    }
};

template<> struct ElVariant<ATOM,0>
{
    struct ElData {
        int64_t n{0};
    };
};

template<> struct ElVariant<ATOM,1>
{
    struct ElData {
        Span<uint8_t> external;
    };
};

template<> struct ElVariant<ATOM,2>
{
    struct ElData {
        Span<uint8_t> owned;
    };
};

template<> struct ElVariant<CONS,0>
{
    struct ElData {
         ElRef left, right;
         ElData(ElRef&& left, ElRef&& right) : left{std::move(left)}, right{std::move(right)} { }
    };
};

template<> struct ElVariant<ERROR,0>
{
    struct ElData { }; // filename and line number?
};

#endif // ELIMPL_H
