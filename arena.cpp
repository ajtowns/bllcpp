
#include <arena.h>
#include <element.h>
#include <elconcept.h>

Arena::Arena()
   : m_nil{New<ATOM>(int64_t{0})}
   , m_one{New<ATOM>(int64_t{1})}
{
}

ElRef Arena::mkel(int64_t v)
{
    if (v == 0) return nil();
    if (v == 1) return one();
    return New<ATOM>(v);
}

ElRef Arena::error()
{
    return New<ERROR>();
}
