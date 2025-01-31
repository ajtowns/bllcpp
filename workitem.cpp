
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
  // { 1, Func::APPLY },
  // { 2, Func::SOFTFORK },
  // { 3, Func::PARTIAL },
  { 4, Func::OP_X },
  { 5, Func::OP_IF },
  // { 6, Func::OP_RC },
  { 7, Func::OP_HEAD },
  { 8, Func::OP_TAIL },
  // { 9, Func::OP_LIST },
  // { 10, Func::OP_BINTREE },
  // { 11, Func::OP_NOTALL },
  // { 12, Func::OP_ALL },
  // { 13, Func::OP_ANY },
  // { 14, Func::OP_EQ },
  // { 15, Func::OP_LT_STR },
  // { 16, Func::OP_STRLEN },
  // { 17, Func::OP_SUBSTR },
  // { 18, Func::OP_CAT },
  // { 19, Func::OP_NAND_BYTES },
  // { 20, Func::OP_AND_BYTES },
  // { 21, Func::OP_OR_BYTES },
  // { 22, Func::OP_XOR_BYTES },
  // { 23, Func::OP_ADD },
  // { 24, Func::OP_SUB },
  // { 25, Func::OP_MUL },
  // { 26, Func::OP_MOD },
  // { 27, Func::OP_SHIFT },
  // { 28, ? },
  // { 29, ? },
  // { 30, Func::OP_SHIFT },
  // { 31, ? },
  // { 32, Func::OP_RD },
  // { 33, Func::OP_WR },
  // { 34, Func::OP_SHA256 },
  // { 35, Func::OP_RIPEMD160 },
  // { 36, Func::OP_HASH160 },
  // { 37, Func::OP_HASH256 },
  // { 38, Func::OP_BIP340_VERIFY },
  // { 39, Func::OP_ECDSA_VERIFY },
  // { 40, Func::OP_SECP256K1_MULADD },
  // { 41, Func::OP_TX },
  // { 42, Func::OP_BIP342_TXMSG },

  // {(0xff, "===", op_bigeq), ## XXX shouldn't be an opcode?
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

template<ElConcept<FUNC>::V Variant>
struct FuncStep
{
     static void step(const ElConcept<FUNC>&, const ElData<FUNC,Variant>&, StepParams&);
};

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

template<Func::Func FnId>
struct FixOpcode;

template<size_t MIN, size_t MAX>
struct FixOpcodeBase
{
    static constexpr size_t min = MIN;
    static constexpr size_t max = MAX;
};

template<>
struct FixOpcode<Func::OP_X> : FixOpcodeBase<1,1>
{
    static ElRef fixop(Arena& arena, ElView)
    {
        return arena.error();
    }
};

template<>
struct FixOpcode<Func::OP_HEAD> : FixOpcodeBase<1,1>
{
    static ElRef fixop(Arena& arena, ElView lst)
    {
        if (auto lr = lst.get<CONS>(); lr) {
            return ElRef::copy_of(lr->left());
        } else {
            return arena.error();
        }
    }
};

template<>
struct FixOpcode<Func::OP_TAIL> : FixOpcodeBase<1,1>
{
    static ElRef fixop(Arena& arena, ElView lst)
    {
        if (auto lr = lst.get<CONS>(); lr) {
            return ElRef::copy_of(lr->right());
        } else {
            return arena.error();
        }
    }
};

template<>
struct FixOpcode<Func::OP_IF> : FixOpcodeBase<1,3>
{
    static ElRef fixop(Arena& arena, ElView c, ElView t, ElView f)
    {
        if (c.is_nil()) {
            return (f ? ElRef::copy_of(f) : arena.nil());
        } else {
            return (t ? ElRef::copy_of(t) : arena.one());
        }
    }
};

template<size_t N>
static std::array<ElRef, N> mkElRefArray()
{
    return []<size_t... Is>(std::index_sequence<Is...>) {
        return std::array<ElRef,N>{((void)Is,ElRef(nullptr))...};
    }(std::make_index_sequence<N>{});
}

template<typename Fn, typename A, typename... T>
static auto apply_suffix(Fn&& fn, A&& suffix, T&&... prefix)
{
    auto call_suffix = [&](auto&&... args) {
        return fn(prefix..., args...);
    };
    return std::apply(call_suffix, suffix);
}

template<ElConcept<FUNC>::V Variant>
void FuncStep<Variant>::step(const ElConcept<FUNC>& ec, const ElData<FUNC,Variant>& extcount, StepParams& sp)
{
    using FO = FixOpcode<ElConcept<FUNC>::V2FnId(Variant)>;
    if (extcount_helper(ec, extcount, sp, FO::min, FO::max)) return;
    auto arr = mkElRefArray<FO::max>();
    populate(arr, extcount.extdata, extcount.count);
    ElRef res = apply_suffix(FO::fixop, arr, sp.wi.arena);
    sp.wi.fin_value(res.move());
}

/* XXX
 * want some way to have most FUNCs do similar behaviour, namely:
 *   - run BLLEVAL over each of their args (can be common code)
 *   - call an "opcode" thing
 *        binop: state + arg -> state
 *        n_args: count and track number of args seen, evaluate at once  -- done!
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
