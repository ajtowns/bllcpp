
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

ElRef Arena::error(std::source_location sloc)
{
    int64_t line = std::min<int64_t>(std::max<int64_t>(sloc.line(), 0), std::numeric_limits<uint32_t>::max());
    return New<ERROR>(ElConcept<ERROR>::sourceloc{sloc.file_name(), static_cast<uint32_t>(line)});
}
