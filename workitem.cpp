
#include <workitem.h>
#include <element.h>
#include <elconcept.h>
#include <elimpl.h>
#include <arena.h>

template<ElType ET>
struct WorkItem::Logic
{
    Logic() = delete;
    static void step(WorkItem& wi, ElConcept<ET>& fn, ElRef&& args, ElRef&& env, ElRef&& feedback);
};

// default step is more or less a no-op
template<ElType ET>
void WorkItem::Logic<ET>::step(WorkItem& wi, ElConcept<ET>& elc, ElRef&& args, ElRef&& env, ElRef&& feedback)
{
    // discard
    args.reset();
    env.reset();
    feedback.reset();

    // save value
    wi.fin_value(elc.copy());
}

// default step is more or less a no-op
template<>
void WorkItem::Logic<FUNC>::step(WorkItem& wi, ElConcept<FUNC>& func, ElRef&& args, ElRef&& env, ElRef&& feedback)
{
    (void)func;
    wi.fin_value(
        wi.arena.mklist(args.move(), env.move(), feedback ? feedback.move() : wi.arena.nil())
    );
}

template<ElType ET>
void WorkItem::step(ElConcept<ET>& fn, ElRef&& args, ElRef&& env, ElRef&& feedback)
{
    WorkItem::Logic<ET>::step(*this, fn, std::move(args), std::move(env), std::move(feedback));
}

void WorkItem::cont()
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

    cont.func | util::Overloaded(
        [&](auto elconcept) {
            step(elconcept, std::move(cont.args), std::move(cont.env), std::move(fb));
        }
    );
}

void WorkItem::eval_sexpr(ElRef&& sexpr, ElRef&& env)
{
    new_continuation(arena.mkfn(Func::BLLEVAL), sexpr.move(), env.move());
}
