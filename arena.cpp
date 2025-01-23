
#include <arena.h>
#include <element.h>
#include <elconcept.h>

Arena::Arena()
   : m_nil{New<ATOM>(int64_t{0})}
{
}

ElRef Arena::mkel(int64_t v)
{
    return (v == 0 ? nil() : New<ATOM>(v));
}

ElRef Arena::error()
{
    return New<ERROR>();
}
