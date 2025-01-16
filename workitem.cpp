
#include <workitem.h>
#include <element.h>
#include <elconcept.h>
#include <elimpl.h>

Arena::Arena()
{
    m_nil = New<ATOM,0>(int64_t{0});
}

ElRef Arena::mkel(int64_t v)
{
    return (v == 0 ? nil() : New<ATOM>(v));
}

ElRef Arena::mkcons(ElRef&& a, ElRef&& b)
{
    return New<CONS>(a.move(), b.move());
}

ElRef Arena::error()
{
    return New<ERROR>();
}

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
    wi.fin_value(func.copy()); // XXX fancy logic here
    (void)args; (void)env; (void)feedback;
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
