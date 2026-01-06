#include <execution.h>

#include <func.h>
#include <buddy.h>
#include <saferef.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

using Ref = Buddy::Ref;
using Func = Buddy::Func;
using FuncCount = Buddy::FuncCount;
using FuncExt = Buddy::FuncExt;

namespace Execution {

template<Buddy::FuncEnum FE> struct StepParams;

template<> struct StepParams<Func> {
    Program& program;
    SafeView func;
    Func funcid;
    SafeView state;
    SafeView env;
    SafeRef feedback;
    SafeRef args;
};

template<> struct StepParams<FuncCount> {
    Program& program;
    SafeView func;
    FuncCount funcid;
    SafeView state;
    uint32_t counter;
    SafeView env;
    SafeRef feedback;
    SafeRef args;
};

template<> struct StepParams<FuncExt> {
    Program& program;
    SafeView func;
    FuncExt funcid;
    const void* state;
    SafeView env;
    SafeRef feedback;
    SafeRef args;
};

template<auto FUNC> struct FuncDispatch;

template<>
struct FuncDispatch<QUOTE> {
    static void step(StepParams<Func>& params)
    {
        params.program.fin_value(params.args.take());
    }

    SafeRef default_state(Program& program) { return program.m_alloc.nullref(); }
};

static bool blleval_helper(auto& params)
{
    // shouldn't call this function if there's feedback
    assert(params.feedback.is_null());
    bool res = true;
    params.args.dispatch(util::Overloaded(
        [&]<Buddy::AtomicTagView ATV>(const ATV& atom) {
            if (atom.span().size() != 0) {
                res = false;
            } else {
                params.program.error();
            }
        },
        [&](const Buddy::TagView<Buddy::Tag::CONS,16>& cons) {
            params.program.new_continuation(
                 params.func.copy().take(),
                 cons.right);
            params.program.new_continuation(
                 params.program.m_alloc.Allocator().create_func(
                     BLLEVAL, params.env.copy().take(), Buddy::NULLREF),
                 cons.left);
            res = true;
        },
        [&](const auto&) {
            params.program.error();
            res = true;
        }
    ));
    return res;
}

template<>
struct FuncDispatch<OP_ADD> {
    static void step(StepParams<Func>& params)
    {
        if (params.feedback.is_null()) {
            if (!blleval_helper(params)) {
                params.program.fin_value(params.state.copy().take()); // finish()
            }
            return;
        }

        auto s = SafeConv::ConvertRef<int64_t>::FromView(params.state);
        if (!s) return params.program.error();
        auto a = SafeConv::ConvertRef<int64_t>::FromRef(std::move(params.feedback));
        if (!a) return params.program.error();
        SafeRef r = binop(params.program, s->value(), a->value());
        if (r.is_null()) return; // error
        params.program.new_continuation(
            params.program.m_alloc.Allocator().create_func(
                params.funcid, params.env.copy().take(), r.take()),
            params.args.take());
    }

    static SafeRef binop(Program& program, int64_t state, int64_t arg)
    {
        if ((arg >= 0 && std::numeric_limits<int64_t>::max() - arg >= state)
            || (arg < 0 && std::numeric_limits<int64_t>::min() - arg >= state)) {
            return program.m_alloc.create(state + arg);
        } else {
            program.m_alloc.error();
            return program.m_alloc.nullref();
        }
    }
};

template<auto FUNC> requires std::same_as<decltype(FUNC), Buddy::Func>
struct FuncDispatch<FUNC> {
    static void step(StepParams<Func>& ) { return; }
};

template<auto FUNCCNT> requires std::same_as<decltype(FUNCCNT), Buddy::FuncCount>
struct FuncDispatch<FUNCCNT> {
    static void step(StepParams<FuncCount>& ) { return; }
};

template<auto FUNCEXT> requires std::same_as<decltype(FUNCEXT), Buddy::FuncExt>
struct FuncDispatch<FUNCEXT> {
    static void step(StepParams<FuncExt>& ) { return; }
};

template<Buddy::FuncEnum FE>
struct FuncEnumDispatch;

template<> struct FuncEnumDispatch<Buddy::Func> {
    template<size_t I=0>
    static void step(StepParams<Func>&& params)
    {
        if constexpr (I < Buddy::NUM_Func) {
            constexpr auto F = static_cast<Func>(I);
            if (params.funcid == F) {
                return FuncDispatch<F>::step(params);
            }
            return step<I+1>(std::move(params));
        }
    }
};

template<> struct FuncEnumDispatch<Buddy::FuncCount> {
    template<size_t I=0>
    static void step(StepParams<FuncCount>&& params)
    {
        if constexpr (I < Buddy::NUM_FuncCount) {
            constexpr auto F = static_cast<FuncCount>(I);
            if (params.funcid == F) {
                return FuncDispatch<F>::step(params);
            }
            return step<I+1>(std::move(params));
        }
    }
};

template<> struct FuncEnumDispatch<Buddy::FuncExt> {
    template<size_t I=0>
    static void step(StepParams<FuncExt>&& params)
    {
        if constexpr (I < Buddy::NUM_FuncExt) {
            constexpr auto F = static_cast<FuncExt>(I);
            if (params.funcid == F) {
                return FuncDispatch<F>::step(params);
            }
            return step<I+1>(std::move(params));
        }
    }
};

Buddy::Ref Program::create_bll_func(Buddy::Ref&& env)
{
    return m_alloc.Allocator().create_func(BLLEVAL, env.take(), NULLREF);
}

void Program::step()
{
    if (m_continuations.empty()) return; // nothing to do

    Buddy::Allocator& rawalloc = m_alloc.Allocator();

    Ref feedback{pop_feedback()};
    if (rawalloc.is_error(feedback)) {
         // shortcut
         while (!m_continuations.empty()) {
              Continuation c{pop_continuation()};
              rawalloc.deref(c.func.take());
              rawalloc.deref(c.args.take());
         }
         fin_value(feedback.take());
         return;
    }

    Continuation cont{pop_continuation()};

    Ref args{cont.args.take()};
    Ref func{cont.func.take()}; // funcid, state, environment

    rawalloc.dispatch(func, util::Overloaded(
        [&](const Buddy::TagView<Buddy::Tag::FUNC,16>& f) {
            FuncEnumDispatch<Func>::step({
                .program=*this,
                .func=m_alloc.view(func),
                .funcid=f.funcid,
                .state=m_alloc.view(f.state),
                .env=m_alloc.view(f.env),
                .feedback=m_alloc.takeref(feedback.take()),
                .args=m_alloc.takeref(args.take()),
            });
        },
        [&](const Buddy::TagView<Buddy::Tag::FUNC_COUNT,16>& f) {
            FuncEnumDispatch<FuncCount>::step({
                .program=*this,
                .func=m_alloc.view(func),
                .funcid=f.funcid,
                .state=m_alloc.view(f.state),
                .counter=f.counter,
                .env=m_alloc.view(f.env),
                .feedback=m_alloc.takeref(feedback.take()),
                .args=m_alloc.takeref(args.take()),
            });
        },
        [&](const Buddy::TagView<Buddy::Tag::FUNC_EXT,16>& f) {
            FuncEnumDispatch<FuncExt>::step({
                .program=*this,
                .func=m_alloc.view(func),
                .funcid=f.funcid,
                .state=f.state,
                .env=m_alloc.view(f.env),
                .feedback=m_alloc.takeref(feedback.take()),
                .args=m_alloc.takeref(args.take()),
            });
        },
        [&](const auto&) {
            rawalloc.deref(feedback.take());
            rawalloc.deref(args.take());
            error();
        }
    ));
    rawalloc.deref(func.take());
}

#if 0

using func_name_array = std::array<std::string, ElConceptDef<FUNC>::variants>;

#define CASE_FUNC_NAME(F) case F: res[F] = #F; break
static func_name_array gen_func_names()
{
    func_name_array res;
    for (size_t i = 0; i < res.size(); ++i) {
        switch(static_cast<Func::Func>(i)) {
            CASE_FUNC_NAME(Func::QUOTE);
            CASE_FUNC_NAME(Func::APPLY);
            CASE_FUNC_NAME(Func::OP_X);
            CASE_FUNC_NAME(Func::OP_RC);
            CASE_FUNC_NAME(Func::OP_HEAD);
            CASE_FUNC_NAME(Func::OP_TAIL);
            CASE_FUNC_NAME(Func::OP_LIST);
            CASE_FUNC_NAME(Func::OP_IF);
            CASE_FUNC_NAME(Func::OP_NOTALL);
            CASE_FUNC_NAME(Func::OP_ALL);
            CASE_FUNC_NAME(Func::OP_ANY);
            CASE_FUNC_NAME(Func::OP_LT_STR);
            CASE_FUNC_NAME(Func::OP_STRLEN);
            CASE_FUNC_NAME(Func::OP_SUBSTR);
            CASE_FUNC_NAME(Func::OP_CAT);
            CASE_FUNC_NAME(Func::OP_ADD);
            CASE_FUNC_NAME(Func::BLLEVAL);
        }
    }
    return res;
}
#undef CASE_FUNC_NAME

const func_name_array ElConceptDef<FUNC>::func_name = gen_func_names();

template<> void WorkItem::Logic<ElConcept<FUNC>>::step(StepParams&& sp, const ElConcept<FUNC>& func) { func.step(sp); }

template<typename T, ElConcept<FUNC>::V Variant>
struct FuncStep
{
     static_assert(std::is_same_v<ElData<FUNC,Variant>, T>);
     static void step(const ElConcept<FUNC>&, const T&, StepParams& sp);
};

template<ElConcept<FUNC>::V Variant>
using FuncStepV = FuncStep<ElData<FUNC,Variant>, Variant>;

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
    return FuncStepV<Variant>::step(ec, eld, sp);
}

void ElConcept<FUNC>::step(StepParams& sp) const
{
    func_step_helper(*this, sp);
}

template<>
void FuncStepV<Func::QUOTE>::step(const ElConcept<FUNC>&, const FuncNone&, StepParams& sp)
{
    sp.wi.fin_value( sp.args.move() );
}

static ElView get_env(ElView env, int32_t n)
{
    if (n <= 0) return ElView(nullptr);

    while (n > 1) {
        if (auto lr = env.get<CONS>(); lr) {
            if (n & 1) {
                env = lr->right();
            } else {
                env = lr->left();
            }
            n >>= 1;
        } else {
            return ElView(nullptr);
        }
    }
    return env;
}

template<>
void FuncStepV<Func::BLLEVAL>::step(const ElConcept<FUNC>&, const FuncNone&, StepParams& sp)
{
    LogTrace(BCLog::BLL, "BLLEVAL step\n");
    if (auto at = sp.args.get<ATOM>(); at) {
        auto n = at->small_int();
        if (n) {
            if (*n == 0) {
                sp.wi.fin_value(sp.wi.arena.nil());
                return;
            }
            auto el = get_env(sp.env, *n);
            if (el) {
                sp.wi.fin_value(ElRef::copy_of(el));
                return;
            }
        }
        sp.wi.error();
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
struct BinOpcode;

struct BinOpcodeBase
{
    static ElRef finish(Arena&, ElView state) { return ElRef::copy_of(state); }
};

template<Func::Func FnId>
struct FixOpcode;

template<size_t MIN, size_t MAX>
struct FixOpcodeBase
{
    static constexpr size_t min = MIN;
    static constexpr size_t max = MAX;

    template<typename FO>
    static void fixmacro(StepParams& sp, std::array<ElRef,MAX>& arr)
    {
        sp.wi.fin_value(apply_suffix(FO::fixop, arr, sp.wi.arena));
    }
};

template<>
struct FixOpcode<Func::APPLY> : FixOpcodeBase<1,2>
{
    template<typename>
    static void fixmacro(StepParams& sp, std::array<ElRef,FixOpcodeBase::max>& arr)
    {
        ElRef expr = std::get<0>(arr).move();
        ElRef env = std::get<1>(arr).move();
        if (!env) env = sp.env.move();
        sp.wi.new_continuation(Func::BLLEVAL, expr.move(), env.move());
    }
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
struct FixOpcode<Func::OP_LIST> : FixOpcodeBase<1,1>
{
    static ElRef fixop(Arena& arena, ElView lst)
    {
        return arena.mkbool(lst.is<CONS>());
    }
};

template<>
struct FixOpcode<Func::OP_SUBSTR> : FixOpcodeBase<1,3>
{
    static ElRef fixop(Arena& arena, ElView str, ElView fst, ElView lst)
    {
        if (!str.is<ATOM>()) return arena.error();
        if (fst && !fst.is<ATOM>()) return arena.error();
        if (lst && !lst.is<ATOM>()) return arena.error();

        auto str_s = str.get<ATOM>()->data();

        int64_t sz = str_s.size();

        int64_t f = fst ? fst.get<ATOM>()->small_int_or(sz) : 0;
        int64_t l = lst ? lst.get<ATOM>()->small_int_or(0) : sz;
        if (f >= l || f >= sz || l <= 0) return arena.nil();
        if (f == 0 && l == sz) return ElRef::copy_of(str);
        return arena.New<ATOM>(arena, str_s.subspan(f, l));
    }
};

template<>
struct BinOpcode<Func::OP_RC>
{
    // state = nullptr -> finish=nil
    // first arg replaces nullptr
    // later args are cons(arg, state)
    static ElRef binop(Arena& arena, ElView state, ElView arg)
    {
        if (!state) {
            return ElRef::copy_of(arg);
        } else {
            return arena.New<CONS>(ElRef::copy_of(arg), ElRef::copy_of(state));
        }
    }

    static ElRef finish(Arena& arena, ElView state)
    {
        if (!state) {
            return arena.nil();
        } else {
            return ElRef::copy_of(state);
        }
    }
};

template<>
struct BinOpcode<Func::OP_NOTALL>
{
    // state = nullptr at start; false
    // any nil => state = one()
    static ElRef binop(Arena& arena, ElView state, ElView arg)
    {
        if (!state && arg.is_nil()) {
            return arena.one();
        } else {
            return ElRef::copy_of(state);
        }
    }

    static ElRef finish(Arena& arena, ElView state)
    {
        if (!state) {
            return arena.nil();
        } else {
            return ElRef::copy_of(state);
        }
    }
};

template<>
struct BinOpcode<Func::OP_ADD>
{
    // state = nullptr at start; false
    // any nil => state = one()
    static ElRef binop(Arena& arena, ElView state, ElView arg)
    {
        int64_t s{0};

        if (state) {
            if (auto st_a = state.get<ATOM>(); st_a) {
                auto os = st_a->small_int();
                if (!os) return arena.error(); // should be impossible
                s = *os;
            } else {
                return arena.error();
            }
        }

        if (auto arg_a = arg.get<ATOM>(); arg_a) {
            auto a = arg_a->small_int();
            if (!a) return arena.error();
            if ((*a >= 0 && std::numeric_limits<int64_t>::max() - *a >= s)
                || (*a < 0 && std::numeric_limits<int64_t>::min() - *a <= s))
            {
                return arena.New<ATOM>(s + *a);
            }
        }
        return arena.error();
    }

    static ElRef finish(Arena& arena, ElView state)
    {
        if (!state) {
            return arena.nil();
        } else {
            return ElRef::copy_of(state);
        }
    }

    int64_t xdefault_state() { return 0; }
    static ElRef xbinop(Arena& arena, int64_t state, int64_t arg)
    {
        if ((arg >= 0 && std::numeric_limits<int64_t>::max() - arg >= state)
            || (arg < 0 && std::numeric_limits<int64_t>::min() - arg <= state))
        {
            return arena.New<ATOM>(state + arg);
        } else {
            return arena.error();
        }
    }
};

template<>
struct BinOpcode<Func::OP_ALL>
{
    // state = nullptr at start; true
    // any nil => state = nil()
    static ElRef binop(Arena& arena, ElView state, ElView arg)
    {
        if (!state && arg.is_nil()) {
            return arena.nil();
        } else {
            return ElRef::copy_of(state);
        }
    }

    static ElRef finish(Arena& arena, ElView state)
    {
        if (!state) {
            return arena.one();
        } else {
            return ElRef::copy_of(state);
        }
    }
};

template<>
struct BinOpcode<Func::OP_ANY>
{
    // state = nullptr at start; false
    // any not nil => state = one()
    static ElRef binop(Arena& arena, ElView state, ElView arg)
    {
        if (!state && !arg.is_nil()) {
            return arena.one();
        } else {
            return ElRef::copy_of(state);
        }
    }

    static ElRef finish(Arena& arena, ElView state)
    {
        if (!state) {
            return arena.nil();
        } else {
            return ElRef::copy_of(state);
        }
    }
};

template<>
struct BinOpcode<Func::OP_LT_STR>
{
    static ElRef binop(Arena& arena, ElView state, ElView arg)
    {
        if (!arg.is<ATOM>()) return arena.error();
        if (state && !state.is<ATOM>()) return ElRef::copy_of(state);
        if (!state) return ElRef::copy_of(arg);

        auto state_s = state.get<ATOM>()->data();
        auto arg_s = arg.get<ATOM>()->data();

        auto r = std::memcmp(state_s.data(), arg_s.data(), std::min(state_s.size(), arg_s.size()));

        if (r > 0 || (r == 0 && state_s.size() >= arg_s.size())) {
            return arena.New<CONS>(arena.nil(), arena.nil());
        } else {
            return ElRef::copy_of(arg);
        }
    }

    static ElRef finish(Arena& arena, ElView state)
    {
        return arena.mkbool(!state || state.is<ATOM>());
    }
};

template<>
struct BinOpcode<Func::OP_STRLEN> : BinOpcodeBase
{
    static ElRef binop(Arena& arena, ElView state, ElView arg)
    {
        auto st_a = state.get<ATOM>();
        if (auto arg_a = arg.get<ATOM>(); arg_a) {
            int64_t n = (st_a ? st_a->small_int_or(0) : 0);
            return arena.mkel(n + arg_a->data().size());
        } else {
            return arena.error();
        }
    }
};

template<>
struct BinOpcode<Func::OP_CAT> : BinOpcodeBase
{
    static ElRef binop(Arena& arena, ElView state, ElView arg)
    {
        auto st_a = state.get<ATOM>();
        if (auto arg_a = arg.get<ATOM>(); st_a && arg_a) {
            auto st_s = st_a->data();
            auto arg_s = arg_a->data();

            if (arg_s.size() == 0) return ElRef::copy_of(state);
            if (st_s.size() == 0) return ElRef::copy_of(arg);
            Span<uint8_t> res_s;
            auto res = arena.New<ATOM>(arena, st_s.size() + arg_s.size(), res_s);
            std::memcpy(res_s.data(), st_s.data(), st_s.size());
            std::memcpy(res_s.data() + st_s.size(), arg_s.data(), arg_s.size());
            return res.move();
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

// returns true if it can complete processing
static bool blleval_helper(const ElConcept<FUNC>& ec, StepParams& sp)
{
    if (sp.feedback) {
        return false;
    } else if (auto lr = sp.args.get<CONS>(); lr) {
        auto l = lr->left();
        auto r = lr->right();
        sp.wi.new_continuation(ElRef::copy_of(ec), ElRef::copy_of(r), sp.env.copy());
        sp.wi.new_continuation(Func::BLLEVAL, ElRef::copy_of(l), sp.env.move());
        return true;
    } else if (!sp.args.is_nil()) {
        sp.wi.error();
        return true;
    } else {
        return false;
    }
}

static bool extcount_helper(const ElConcept<FUNC>& ec, const FuncExtCount& extcount, StepParams& sp, int min_args, int max_args)
{
    if (blleval_helper(ec, sp)) return true;

    if (sp.feedback) {
        if (extcount.count >= max_args) {
            sp.wi.error();
            return true;
        }
        auto newed = (extcount.count == 0 ? sp.feedback.move() : sp.wi.arena.New<CONS>(sp.feedback.move(), extcount.extdata.copy()));
        auto newfn = sp.wi.arena.NewFunc<FuncExtCount>(ec.get_fnid(), newed.move(), extcount.count + 1);
        sp.wi.new_continuation(newfn.move(), sp.args.move(), sp.env.move());
        return true;
    }

    // no more args to process, so finalise
    if (extcount.count < min_args) {
        sp.wi.error();
        return true;
    }
    return false;
}

template<ElConcept<FUNC>::V Variant>
struct FuncStep<FuncExtCount, Variant>
{
    static_assert(std::is_same_v<ElData<FUNC,Variant>, FuncExtCount>);
    static void step(const ElConcept<FUNC>&ec, const FuncExtCount& extcount, StepParams& sp)
    {
        using FO = FixOpcode<ElConcept<FUNC>::V2FnId(Variant)>;
        if (extcount_helper(ec, extcount, sp, FO::min, FO::max)) return;

        // finalise
        auto arr = mkElRefArray<FO::max>();
        populate(arr, extcount.extdata, extcount.count);
        FO::template fixmacro<FO>(sp, arr);
    }
};

template<ElConcept<FUNC>::V Variant>
struct FuncStep<FuncExt, Variant>
{
    static_assert(std::is_base_of_v<FuncExt, ElData<FUNC,Variant>>);
    static void step(const ElConcept<FUNC>&ec, const FuncExt& ext, StepParams& sp)
    {
        using BO = BinOpcode<ElConcept<FUNC>::V2FnId(Variant)>;
        if (blleval_helper(ec, sp)) return;
        if (sp.feedback) {
            ElRef newstate = BO::binop(sp.wi.arena, ext.extdata, sp.feedback.view());
            if (newstate.is<ERROR>()) {
                sp.wi.fin_value(newstate.move());
            } else {
                auto newfn = sp.wi.arena.NewFunc<FuncExt>(ec.get_fnid(), newstate.move());
                sp.wi.new_continuation(newfn.move(), sp.args.move(), sp.env.move());
            }
        } else {
            sp.wi.fin_value(BO::finish(sp.wi.arena, ext.extdata));
        }
    }
};

template<ElConcept<FUNC>::V Variant>
struct FuncStep<FuncExtNil, Variant> : FuncStep<FuncExt, Variant> { };
#endif

} // Execution namespace

