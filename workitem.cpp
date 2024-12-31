
#include <workitem.h>

template<ElType ET>
struct ElCont
{
    static void cont(const ElData<ET>* eldata, ElRef&& args, ElRef&& env, ElRef&& feedback, WorkItem& wi)
    {
        // XXX I don't think this is right -- should be fin_value(eldata), except eldata would need to be an ElRef...
        //     and in any event, I think I want to have <FUNC> be a single type
        (void)eldata; (void)env; (void) feedback;
        wi.fin_value(std::move(args));
    }
};



void WorkItem::cont(Continuation&& cont, ElRef feedback)
{
    if (feedback.is_error()) {
         while (!continuations.empty()) pop_continuation();
         fin_value(std::move(feedback));
         return;
    }

    cont.func.visit([&]<ElType ET>(const ElData<ET>* fndata) {
        ElCont<ET>::cont(fndata, std::move(cont.args), std::move(cont.env), std::move(feedback), *this);
    });
}

void step(Continuation&& cont)
{
    (void)cont;
}

