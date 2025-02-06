
#include <workitem.h>
#include <elconcept.h>
#include <element.h>
#include <arena.h>
#include <elimpl.h>
#include <funcimpl.h>

#include <map>
#include <type_traits>

template<> void WorkItem::Logic<class ElConcept<ATOM>>::step(StepParams&& sp, const ElConcept<ATOM>& elc) { sp.wi.fin_value(ElRef::copy_of(elc)); }
template<> void WorkItem::Logic<class ElConcept<CONS>>::step(StepParams&& sp, const ElConcept<CONS>& elc) { sp.wi.fin_value(ElRef::copy_of(elc)); }
template<> void WorkItem::Logic<class ElConcept<ERROR>>::step(StepParams&& sp, const ElConcept<ERROR>& elc) { sp.wi.fin_value(ElRef::copy_of(elc)); }

void WorkItem::step()
{
    if (continuations.empty()) return; // nothing to do

    Continuation cont{pop_continuation()};
    ElRef fb{pop_feedback()};

    if (fb.view().is<ERROR>()) {
         // shortcut
         LogTrace(BCLog::BLL, "error feedback, cleaning up\n");
         while (!continuations.empty()) pop_continuation();
         fin_value(std::move(fb));
         return;
    }

    StepParams sp(*this, std::move(cont.args), std::move(cont.env), std::move(fb));

    cont.func.view().visit([&](auto elconcept) {
        using L = WorkItem::Logic<ElConcept<typename decltype(elconcept)::ET>>;
        L::step(std::move(sp), elconcept);
    });
}

void WorkItem::eval_sexpr(ElRef&& sexpr, ElRef&& env)
{
    new_continuation(Func::BLLEVAL, sexpr.move(), env.move());
}
