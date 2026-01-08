#include <execution.h>

#include <func.h>
#include <buddy.h>
#include <saferef.h>
#include <overloaded.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

using namespace Buddy;

using atomspan = std::span<const uint8_t>;

namespace Execution {

void Program::new_continuation(Ref&& func, Ref&& args)
{
    m_continuations.emplace_back(func.take(), args.take());
}

void Program::fin_value(Ref&& val)
{
    assert(m_feedback.is_null());
    m_feedback = val.take();
}

template<FuncEnum FE> struct StepParams;

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

static bool blleval_helper(auto& params)
{
    assert(params.feedback.is_null()); // shouldn't call this function if there's feedback
    assert(!params.args.is_null());

    if (auto lr = params.args.template convert<std::pair<SafeRef, SafeRef>>(); lr) {
        params.program.new_continuation(
             params.func.copy(),
             std::move(lr->value().second));
        params.program.new_continuation(BLLEVAL,
             params.env.copy(),
             std::move(lr->value().first));
        return true;
    } else if (auto a = params.args.template convert<atomspan>(); a) {
        if (a->value().size() == 0) {
            return false; // end of arg list
        } else {
            params.program.error();
            return true;
        }
    } else {
        params.program.error();
        return true;
    }
}

static SafeRef getenv(SafeView env, int64_t env_index)
{
    SafeAllocator& alloc = env.Allocator();
    SafeRef res{alloc.nullref()};
    if (env_index <= 0) {
        res = alloc.nil();
    } else {
        while (env_index > 1) {
            auto lr = env.convert<std::pair<SafeView, SafeView>>();
            if (!lr) break;
            if (env_index % 2 == 0) {
                env = lr->value().first;
            } else {
                env = lr->value().second;
            }
        }
        res = env.copy();
    }
    return res;
}

namespace {
template<auto FUNC> struct FuncDispatch;

template<>
struct FuncDispatch<BLLEVAL> {
    static void step(StepParams<Func>& params)
    {
        if (!params.feedback.is_null()) return params.program.error(); // BLLEVAL does not delegate, so should not receive feedback
        if (auto s = params.args.convert<int64_t>(); s) {
            int64_t env_index{s->value()};
            if (env_index == 0) {
                return params.program.fin_value(params.program.m_alloc.nil());
            } else if (env_index > 0) {
                auto env = getenv(params.env, env_index);
                if (env.is_null()) return params.program.error();
                return params.program.fin_value(env.copy());
            } else {
                return params.program.error(); // negative env is impossible
            }
        } else if (auto c = params.args.convert<std::pair<SafeRef,SafeRef>>(); c) {
            auto [l, r] = std::move(c->value());
            if (auto op = l.convert<int64_t>(); op) {
                std::visit(util::Overloaded(
                    [&](FuncEnum auto funcid) {
                        params.program.new_continuation(funcid, params.env.copy(), std::move(r));
                    },
                    [&](const std::monostate&) {
                        return params.program.error(); // invalid opcode
                    }), lookup_opcode(op->value()));
            } else {
                return params.program.error(); // atom way too big to be an opcode
            }
        } else {
            return params.program.error(); // trying to parse something strange
        }
    }
};

template<>
struct FuncDispatch<QUOTE> {
    static void step(StepParams<Func>& params)
    {
        params.program.fin_value(std::move(params.args));
    }
};

template<typename Derived, typename StateType, typename ArgType=StateType>
struct BinOpHelper {
    static void finish(Program& program, SafeView state)
    {
        if (state.is_null()) {
            program.fin_value(program.m_alloc.create(Derived::initial_state()));
        } else {
            program.fin_value(state.copy());
        }
    }

    static bool idempotent(const StateType&, const ArgType&) { return false; }

    static void step(StepParams<Func>& params)
    {
        static_assert(std::derived_from<Derived, BinOpHelper<Derived,StateType,ArgType>>, "Derived must inherit from BinOpHelper<Derived> (CRTP requirement)");

        if (params.feedback.is_null()) {
            if (!blleval_helper(params)) Derived::finish(params.program, params.state);
            return;
        }

        auto a = params.feedback.convert<ArgType>();
        if (!a) return params.program.error();
        auto s = params.state.convert_default<StateType>(Derived::initial_state());
        if (!s) return params.program.error();
        if (Derived::idempotent(s->value(), a->value())) {
            params.program.new_continuation(
                params.func.copy(), params.args.copy());
            return;
        }
        SafeRef r = Derived::binop(params.program, s->value(), a->value()); // work()
        if (r.is_null()) return; // error
        params.program.new_continuation(
            params.program.m_alloc.takeref(params.program.m_alloc.Allocator().create_func(
                params.funcid, params.env.copy().take(), r.take())),
            std::move(params.args));
    }
};

template<>
struct FuncDispatch<OP_NOTALL> : public BinOpHelper<FuncDispatch<OP_NOTALL>, bool> {
    static bool initial_state() { return false; }
    static SafeRef binop(Program& program, bool state, bool arg)
    {
        return program.m_alloc.create(state || !arg);
    }
};

template<>
struct FuncDispatch<OP_ALL> : public BinOpHelper<FuncDispatch<OP_ALL>, bool> {
    static bool initial_state() { return true; }
    static SafeRef binop(Program& program, bool state, bool arg)
    {
        return program.m_alloc.create(state && arg);
    }
};

template<>
struct FuncDispatch<OP_ANY> : public BinOpHelper<FuncDispatch<OP_ANY>, bool> {
    static bool initial_state() { return false; }
    static SafeRef binop(Program& program, bool state, bool arg)
    {
        return program.m_alloc.create(state || arg);
    }
};

template<>
struct FuncDispatch<OP_CAT> : public BinOpHelper<FuncDispatch<OP_CAT>, atomspan> {
    static atomspan initial_state() { return {}; }

    static bool idempotent(const atomspan&, const atomspan& arg) { return arg.size() == 0; }

    static SafeRef binop(Program& program, atomspan state, atomspan arg)
    {
        size_t sz = state.size() + arg.size();
        std::span<uint8_t> dst;
        std::array<uint8_t, 123> arr;
        uint8_t* owned{nullptr};
        if (sz <= arr.size()) {
            dst = std::span{arr}.subspan(0, sz);
        } else {
            owned = static_cast<uint8_t*>(std::malloc(sz));
            dst = std::span{owned, sz};
        }
        auto mid = std::copy(state.begin(), state.end(), dst.begin());
        std::copy(arg.begin(), arg.end(), mid);
        if (owned == nullptr) {
            return program.m_alloc.create(dst);
        } else {
            return program.m_alloc.create_owned(dst);
        }
    }
};

template<>
struct FuncDispatch<OP_ADD> : public BinOpHelper<FuncDispatch<OP_ADD>, int64_t> {
    static int64_t initial_state() { return 0; }

    static bool idempotent(int64_t, int64_t arg) { return arg == 0; }

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

// XXX unimplemented functions, just to make it compile
template<auto FUNC> requires std::same_as<decltype(FUNC), Func>
struct FuncDispatch<FUNC> {
    static void step(StepParams<Func>& ) { return; }
};

template<auto FUNCCNT> requires std::same_as<decltype(FUNCCNT), FuncCount>
struct FuncDispatch<FUNCCNT> {
    static void step(StepParams<FuncCount>& ) { return; }
};

template<auto FUNCEXT> requires std::same_as<decltype(FUNCEXT), FuncExt>
struct FuncDispatch<FUNCEXT> {
    static void step(StepParams<FuncExt>& ) { return; }
};
} // anonymous namespace

template<typename FE, template<FE> class Dispatcher>
static constexpr auto step_dispatch_table() {
    return []<size_t... I>(std::index_sequence<I...>) -> std::array<void(*)(StepParams<FE>&),FuncEnumSize<FE>> {
        return { [](StepParams<FE>& params) { Dispatcher<static_cast<FE>(I)>::step(params); }... };
    }(std::make_index_sequence<FuncEnumSize<FE>>{});
}

template<FuncEnum FE>
struct FuncEnumDispatch {
    static constexpr auto dispatch = step_dispatch_table<FE, FuncDispatch>();
    static void step(StepParams<FE>&& params)
    {
        return (dispatch[static_cast<size_t>(params.funcid)])(params);
    }
};

void Program::step()
{
    if (m_continuations.empty()) return; // nothing to do

    Allocator& rawalloc = m_alloc.Allocator();

    Ref feedback{pop_feedback()};
    if (rawalloc.is_error(feedback)) {
         // shortcut
         while (!m_continuations.empty()) {
              Continuation c{pop_continuation()};
              rawalloc.deref(c.func.take());
              rawalloc.deref(c.args.take());
         }
         fin_value(m_alloc.takeref(feedback.take()));
         return;
    }

    Continuation cont{pop_continuation()};

    Ref args{cont.args.take()};
    Ref func{cont.func.take()}; // funcid, state, environment

    rawalloc.dispatch(func, util::Overloaded(
        [&](const TagView<Tag::FUNC,16>& f) {
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
        [&](const TagView<Tag::FUNC_COUNT,16>& f) {
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
        [&](const TagView<Tag::FUNC_EXT,16>& f) {
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

// returns true this function updated the continuation
static bool blleval_helper(const ElConcept<FUNC>& ec, StepParams& sp)
{
    if (!sp.feedback.is_null()) return false; // caller has to deal with feedback

    if (auto lr = sp.args.get<CONS>(); lr) {
        auto l = lr->left();
        auto r = lr->right();
        sp.wi.new_continuation(ElRef::copy_of(ec), ElRef::copy_of(r), sp.env.copy());
        sp.wi.new_continuation(Func::BLLEVAL, ElRef::copy_of(l), sp.env.move());
        return true;
    } else if (!sp.args.is_nil()) {
        sp.wi.error();
        return true;
    }
    return false; // caller has to deal with finalisation
}

static bool extcount_helper(const ElConcept<FUNC>& ec, const FuncExtCount& extcount, StepParams& sp, int min_args, int max_args)
{
    if (sp.feedback) {
        if (extcount.count >= max_args) {
            sp.wi.error();
        } else {
            auto newed = (extcount.count == 0 ? sp.feedback.move() : sp.wi.arena.New<CONS>(sp.feedback.move(), extcount.extdata.copy()));
            auto newfn = sp.wi.arena.NewFunc<FuncExtCount>(ec.get_fnid(), newed.move(), extcount.count + 1);
            sp.wi.new_continuation(newfn.move(), sp.args.move(), sp.env.move());
        }
        return true;
    }

    if (blleval_helper(ec, sp)) return true;

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

