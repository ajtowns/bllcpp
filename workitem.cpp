
#include <workitem.h>
#include <element.h>
#include <elconcept.h>
#include <elimpl.h>
#include <arena.h>

#include <type_traits>

struct StepParams
{
    WorkItem& wi;
    ElRef args;
    ElRef env;
    ElRef feedback;

    StepParams(WorkItem& wi, ElRef&& args, ElRef&& env, ElRef&& feedback)
    : wi{wi}, args{std::move(args)}, env{std::move(env)}, feedback{std::move(feedback)}
    {
    }

    StepParams() = delete;
    StepParams(const StepParams&) = delete;
    StepParams(StepParams&&) = default;
    ~StepParams() = default;
};

template<typename T>
struct WorkItem::Logic
{
    Logic() = delete;
    static void step(StepParams&& sp, const T& fn);
};

// func step is a delegation
template<>
void WorkItem::Logic<ElConcept<FUNC>>::step(StepParams&& sp, const ElConcept<FUNC>& func)
{
    ElConceptHelper::visit(func, [&](const auto &elv) {
        WorkItem::Logic<std::remove_cvref_t<decltype(elv)>>::step(std::move(sp), elv);
    });
}

template<ElType ET>
using WAT = WorkItem::Logic<ElConcept<ET>>;

// default step is basically a no-op
static void step_noop(StepParams&& sp, ElView elv)
{
    // save value
    sp.wi.fin_value(elv.copy());
}

template<> void WorkItem::Logic<class ElConcept<ATOM>>::step(StepParams&& sp, const ElConcept<ATOM>& elc) { step_noop(std::move(sp), elc.view()); }
template<> void WorkItem::Logic<class ElConcept<CONS>>::step(StepParams&& sp, const ElConcept<CONS>& elc) { step_noop(std::move(sp), elc.view()); }
template<> void WorkItem::Logic<class ElConcept<ERROR>>::step(StepParams&& sp, const ElConcept<ERROR>& elc) { step_noop(std::move(sp), elc.view()); }

/* handle simple functions */
template<>
void WorkItem::Logic<ElVariant<FUNC,0>>::step(StepParams&& sp, const ElVariant<FUNC,0>& elv)
{
    (void)elv;
    // just do something weird, whatever
    sp.wi.fin_value( sp.wi.arena.mklist( sp.args.move(), sp.env.move(), sp.feedback ? sp.feedback.move() : sp.wi.arena.nil()) );
}


void WorkItem::step()
{
    if (continuations.empty()) return; // nothing to do

    Continuation cont{pop_continuation()};
    ElRef fb{pop_feedback()};

    if (fb.is<ERROR>()) {
         // shortcut
         while (!continuations.empty()) pop_continuation();
         fin_value(std::move(fb));
         return;
    }

    StepParams sp(*this, std::move(cont.args), std::move(cont.env), std::move(feedback));

    cont.func.visit([&](auto elconcept) { WorkItem::Logic<ElConcept<typename decltype(elconcept)::ET>>::step(std::move(sp), elconcept); });
}

void WorkItem::eval_sexpr(ElRef&& sexpr, ElRef&& env)
{
    new_continuation(arena.mkfn(Func::BLLEVAL), sexpr.move(), env.move());
}
