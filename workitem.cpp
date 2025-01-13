
#include <workitem.h>

using enum ElType;
using namespace ElementConcept;

Arena::Arena()
{
    m_nil = New<ATOM,0>(int64_t{0});
}

template<ElType ET>
struct WorkItem::Logic
{
    static void step(WorkItem& wi, ElementConcept::ElConcept<ET>& fn, ElRef&& args, ElRef&& env, ElRef&& feedback);
};

// default step is more or less a no-op
template<ElType ET>
void WorkItem::Logic<ET>::step(WorkItem& wi, ElementConcept::ElConcept<ET>& elc, ElRef&& args, ElRef&& env, ElRef&& feedback)
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
void WorkItem::Logic<FUNC>::step(WorkItem& wi, ElementConcept::ElConcept<FUNC>& func, ElRef&& args, ElRef&& env, ElRef&& feedback)
{
    wi.fin_value(func.copy()); // XXX fancy logic here
    (void)args; (void)env; (void)feedback;
}

template<ElType ET>
void step(ElementConcept::ElConcept<ET>& fn, ElRef&& args, ElRef&& env, ElRef&& feedback);

void WorkItem::cont()
{
    if (continuations.empty()) return;

    Continuation cont{pop_continuation()};
    ElRef feedback{pop_feedback()};

    if (feedback.is_error()) {
         while (!continuations.empty()) pop_continuation();
         fin_value(std::move(feedback));
         return;
    }

    cont.func | util::Overloaded(
        [&](auto elconcept) {
            step(elconcept, std::move(cont.args), std::move(cont.env), std::move(feedback));
        }
    );
}
