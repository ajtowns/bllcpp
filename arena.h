#ifndef ARENA_H
#define ARENA_H

#include <elem.h>
#include <element.h>
#include <elconcept.h>

#include <logging.h>

#include <array>
#include <optional>
#include <vector>

class Arena
{
private:
    ElRef m_nil;

public:
    Arena();
    ~Arena() = default;

    template<ElType ET, int Variant, typename... T>
    ElRef New(T&&... args)
    {
        Elem* el = new Elem;
        LogTrace(BCLog::BLL, "Created new %s at %p\n", ET::name, el);
        ElRef res{std::move(el)};
        res.template init_as<ET, Variant>(std::forward<decltype(args)>(args)...);
        return res.move();
    }

    template<ElType ET, typename... T>
    ElRef New(T&&... args)
    {
        static_assert(ElConcept<ET>::variants == 1);
        return New<ET,0>(std::forward<decltype(args)>(args)...);
    }

    inline ElRef mkel(ElRef&& e) { return e.move(); }
    ElRef mkel(int64_t v);
    ElRef nil() { return m_nil.copy(); }

    ElRef error();

    ElRef mkcons(ElRef&& a, ElRef&& b);

    ElRef mkfn(typename ElConceptDef<FUNC>::FnId fn);

    inline ElRef mklist() { return nil(); }
    inline ElRef mklist(auto&& head, auto&&... args)
    {
        ElRef t = mklist(std::forward<decltype(args)>(args)...);
        return New<CONS>(mkel(std::forward<decltype(head)>(head)), std::move(t));
    }
};

#endif // ARENA_H
