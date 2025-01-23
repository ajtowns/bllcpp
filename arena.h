#ifndef ARENA_H
#define ARENA_H

#include <elem.h>
#include <elconcept.h>
#include <element.h>

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

    template<ElType ET, typename... T>
    ElRef New(T&&... args)
    {
        Elem* el = new Elem;
        LogTrace(BCLog::BLL, "Created new %s at %p\n", ET::name, el);

        ElConcept<ET>::init_as(*el, std::forward<decltype(args)>(args)...);

        return ElRef{std::move(el)};
    }

    ElRef nil() { return m_nil.copy(); }
    ElRef error();

    ElRef mkfn(Func::Func fn) { return New<FUNC>(*this, fn); }

    inline ElRef mkel(ElRef&& e) { return e.move(); }
    ElRef mkel(int64_t v);

    inline ElRef mklist() { return nil(); }
    inline ElRef mklist(auto&& head, auto&&... args)
    {
        ElRef t = mklist(std::forward<decltype(args)>(args)...);
        return New<CONS>(mkel(std::forward<decltype(head)>(head)), std::move(t));
    }
};

#endif // ARENA_H
