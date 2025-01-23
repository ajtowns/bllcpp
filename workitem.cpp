
#include <workitem.h>
#include <elconcept.h>
#include <element.h>
#include <arena.h>
#include <elimpl.h>
#include <funcimpl.h>

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


template<> void WorkItem::Logic<class ElConcept<ATOM>>::step(StepParams&& sp, const ElConcept<ATOM>& elc) { sp.wi.fin_value(ElRef::copy_of(&elc.get_el())); }
template<> void WorkItem::Logic<class ElConcept<CONS>>::step(StepParams&& sp, const ElConcept<CONS>& elc) { sp.wi.fin_value(ElRef::copy_of(&elc.get_el())); }
template<> void WorkItem::Logic<class ElConcept<ERROR>>::step(StepParams&& sp, const ElConcept<ERROR>& elc) { sp.wi.fin_value(ElRef::copy_of(&elc.get_el())); }

template<> void WorkItem::Logic<ElConcept<FUNC>>::step(StepParams&& sp, const ElConcept<FUNC>& func) { func.step(sp); }

template<ElConcept<FUNC>::V Variant=0>
static void func_step_helper(const ElConcept<FUNC>& ec, StepParams& sp)
{
    static_assert(Variant < 2);
    if constexpr (Variant < Variant.LAST) {
        if (ec.variant() != Variant) {
            return func_step_helper<Variant+1>(ec, sp);
        }
    }
    auto& eld = ec.get_el().data_ro<ElData<FUNC,Variant>>();
    return FuncStep<Variant>::step(eld, sp);
}
void ElConcept<FUNC>::step(StepParams& sp) const
{
    func_step_helper(*this, sp);
}

template<>
void FuncStep<Func::QUOTE>::step(const ElData<FUNC,Func::BLLEVAL>&, StepParams& sp)
{
    // just do something weird, whatever
    sp.wi.fin_value( sp.wi.arena.mklist( sp.args.move(), sp.env.move(), sp.feedback ? sp.feedback.move() : sp.wi.arena.nil()) );
}

template<>
void FuncStep<Func::BLLEVAL>::step(const ElData<FUNC,Func::BLLEVAL>&, StepParams& sp)
{
    // just do something weird, whatever
    sp.wi.fin_value( sp.wi.arena.mklist( sp.args.move(), sp.env.move(), sp.feedback ? sp.feedback.move() : sp.wi.arena.nil()) );
}

void WorkItem::step()
{
    if (continuations.empty()) return; // nothing to do

    Continuation cont{pop_continuation()};
    ElRef fb{pop_feedback()};

    if (fb.view().is<ERROR>()) {
         // shortcut
         while (!continuations.empty()) pop_continuation();
         fin_value(std::move(fb));
         return;
    }

    StepParams sp(*this, std::move(cont.args), std::move(cont.env), std::move(feedback));

    cont.func.view().visit([&](auto elconcept) { WorkItem::Logic<ElConcept<typename decltype(elconcept)::ET>>::step(std::move(sp), elconcept); });
}

void WorkItem::eval_sexpr(ElRef&& sexpr, ElRef&& env)
{
    new_continuation(arena.mkfn(Func::BLLEVAL), sexpr.move(), env.move());
}
