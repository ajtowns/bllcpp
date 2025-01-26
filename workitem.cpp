
#include <workitem.h>
#include <elconcept.h>
#include <element.h>
#include <arena.h>
#include <elimpl.h>
#include <funcimpl.h>

#include <map>
#include <type_traits>

const static std::map<uint8_t, Func::Func> bll_opcodes = {
  { 0, Func::QUOTE },
  { 5, Func::OP_TAIL },
  { 6, Func::OP_IF },
};

template<typename T>
struct WorkItem::Logic
{
    Logic() = delete;
    static void step(StepParams&& sp, const T& fn);
};


template<> void WorkItem::Logic<class ElConcept<ATOM>>::step(StepParams&& sp, const ElConcept<ATOM>& elc) { sp.wi.fin_value(ElRef::copy_of(elc)); }
template<> void WorkItem::Logic<class ElConcept<CONS>>::step(StepParams&& sp, const ElConcept<CONS>& elc) { sp.wi.fin_value(ElRef::copy_of(elc)); }
template<> void WorkItem::Logic<class ElConcept<ERROR>>::step(StepParams&& sp, const ElConcept<ERROR>& elc) { sp.wi.fin_value(ElRef::copy_of(elc)); }

template<> void WorkItem::Logic<ElConcept<FUNC>>::step(StepParams&& sp, const ElConcept<FUNC>& func) { func.step(sp); }

template<ElConcept<FUNC>::V Variant=0>
static void func_step_helper(const ElConcept<FUNC>& ec, StepParams& sp)
{
    static_assert(Variant < Variant.MAX);
    if constexpr (Variant < Variant.LAST) {
        if (ec.variant() != Variant) {
            return func_step_helper<Variant+1>(ec, sp);
        }
    }
    auto& eld = ec.get_el().data_ro<ElData<FUNC,Variant>>();
    return FuncStep<Variant>::step(ec, eld, sp);
}
void ElConcept<FUNC>::step(StepParams& sp) const
{
    func_step_helper(*this, sp);
}

template<>
void FuncStep<Func::QUOTE>::step(const ElConcept<FUNC>&, const FuncNone&, StepParams& sp)
{
    LogTrace(BCLog::BLL, "QUOTE step\n");
    sp.wi.fin_value( sp.args.move() );
}

template<>
void FuncStep<Func::BLLEVAL>::step(const ElConcept<FUNC>&, const FuncNone&, StepParams& sp)
{
    LogTrace(BCLog::BLL, "BLLEVAL step\n");
    if (auto at = sp.args.get<ATOM>(); at) {
        // XXX: env lookup
        sp.wi.fin_value( sp.wi.arena.mkel(3) );
    } else if (auto lr = sp.args.get<CONS>(); lr) {
        auto l = lr->left();
        auto r = lr->right();
        if (auto op = l.get<ATOM>(); op) {
            auto n = op->small_int();
            if (!n) return sp.wi.error();
            auto fni = bll_opcodes.find(*n);
            if (fni == bll_opcodes.end()) return sp.wi.error();
            sp.wi.new_continuation(fni->second, ElRef::copy_of(r), sp.env.move());
        } else {
            sp.wi.error();
        }
    } else {
        sp.wi.error();
    }
}

static ElRef tail_step(Arena& arena, ElView lst)
{
    if (auto lr = lst.get<CONS>(); lr) {
        return ElRef::copy_of(lr->right());
    } else {
        return arena.error();
    }
}

static ElRef if_step(Arena& arena, ElView c, ElView t, ElView f)
{
    if (c.is_nil()) {
        return (f ? ElRef::copy_of(f) : arena.nil());
    } else {
        return (t ? ElRef::copy_of(t) : arena.one());
    }
}

static bool extcount_helper(const ElConcept<FUNC>& ec, const FuncExtCount& extcount, StepParams& sp, int min_args, int max_args)
{
    if (sp.feedback) {
        auto newed = (extcount.count == 0 ? sp.feedback.move() : sp.wi.arena.New<CONS>(sp.feedback.move(), extcount.extdata.copy()));
        auto newfn = sp.wi.arena.NewFunc<FuncExtCount>(ec.get_fnid(), newed.move(), extcount.count + 1);
        sp.wi.new_continuation(newfn.move(), sp.args.move(), sp.env.move());
        return true;
    } else if (auto lr = sp.args.get<CONS>(); lr) {
        if (extcount.count >= max_args) {
            sp.wi.error();
            return true;
        } else {
            auto l = lr->left();
            auto r = lr->right();
            sp.wi.new_continuation(ElRef::copy_of(ec), ElRef::copy_of(r), sp.env.copy());
            sp.wi.new_continuation(Func::BLLEVAL, ElRef::copy_of(l), sp.env.move());
            return true;
        }
    } else if (!sp.args.is_nil()) {
        sp.wi.error();
        return true;
    } else if (extcount.count < min_args) {
        sp.wi.error();
        return true;
    } else {
        return false; // more work to do
    }
}

template<size_t N, size_t I=0>
static void populate(std::array<ElRef, N>& arr, ElView el, size_t remaining)
{
    if constexpr (I < N) {
        constexpr int Want = N - I;
        if (remaining < Want) {
            arr[N-I-1] = ElRef(nullptr);
            return populate<N,I+1>(arr, el, remaining);
        } else if (remaining == 1) {
            arr[N-I-1] = ElRef::copy_of(el);
        } else if (auto c = el.get<CONS>(); c) {
            arr[N-I-1] = ElRef::copy_of(c->left());
            return populate<N,I+1>(arr, c->right(), remaining - 1);
        }
    }
}

template<size_t N>
static std::array<ElRef, N> mkElRefArray()
{
    return []<size_t... Is>(std::index_sequence<Is...>) {
        return std::array<ElRef,N>{((void)Is,ElRef(nullptr))...};
    }(std::make_index_sequence<N>{});
}

template<>
void FuncStep<Func::OP_TAIL>::step(const ElConcept<FUNC>& ec, const FuncExtCount& extcount, StepParams& sp)
{
    if (extcount_helper(ec, extcount, sp, 1, 1)) return;
    std::array<ElRef, 1> arr = mkElRefArray<1>();
    populate(arr, extcount.extdata, extcount.count);
    ElRef res = tail_step(sp.wi.arena, std::get<0>(arr));
    sp.wi.fin_value(res.move());
}

template<>
void FuncStep<Func::OP_IF>::step(const ElConcept<FUNC>& ec, const FuncExtCount& extcount, StepParams& sp)
{
    if (extcount_helper(ec, extcount, sp, 1, 3)) return;
    std::array<ElRef, 3> arr = mkElRefArray<3>();
    populate(arr, extcount.extdata, extcount.count);
    ElRef res = if_step(sp.wi.arena, std::get<0>(arr), std::get<1>(arr), std::get<2>(arr));
    sp.wi.fin_value(res.move());
}

/* XXX
 * want some way to have most FUNCs do similar behaviour, namely:
 *   - run BLLEVAL over each of their args (can be common code)
 *   - call an "opcode" thing
 *        binop: state + arg -> state
 *        n_args: count and track number of args seen, evaluate at once
 *        intstate: special state + arg -> special state
 */

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
