#include <execution.h>

#include <func.h>
#include <buddy.h>
#include <saferef.h>
#include <overloaded.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <tuple>

using namespace Buddy;

using atomspan = std::span<const uint8_t>;

namespace Execution {

Program::~Program()
{
    Allocator& rawalloc = m_alloc.Allocator();
    Ref feedback{pop_feedback()};
    rawalloc.deref(feedback.take());
    while (!m_continuations.empty()) {
        Continuation c{pop_continuation()};
        rawalloc.deref(c.func.take());
        rawalloc.deref(c.args.take());
    }
}

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
             std::move(lr->second));
        params.program.new_continuation(BLLEVAL,
             params.env.copy(),
             std::move(lr->first));
        return true;
    } else if (auto a = params.args.template convert<atomspan>(); a) {
        if (a->size() == 0) {
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
                env = lr->first;
            } else {
                env = lr->second;
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
            int64_t env_index{*s};
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
            if (auto op = c->first.convert<int64_t>(); op) {
                std::visit(util::Overloaded(
                    [&](FuncEnum auto funcid) {
                        params.program.new_continuation(funcid, params.env.copy(), std::move(c->second));
                    },
                    [&](const std::monostate&) {
                        return params.program.error(); // invalid opcode
                    }), lookup_opcode(*op));
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

template<typename T>
concept HasInitialState = requires(SafeAllocator a) {
    { a.create(T::initial_state()) } -> std::same_as<SafeRef>;
    { a.nil().convert<decltype(T::initial_state())>().set_value(T::initial_state()) };
};

template<typename Derived, typename StateType, typename ArgType=StateType>
struct BinOpHelper {
    static void finish(Program& program, SafeView state)
        requires HasInitialState<Derived>
    {
        if (state.is_null()) {
            program.fin_value(program.m_alloc.create(Derived::initial_state()));
        } else {
            program.fin_value(state.copy());
        }
    }

    static auto get_state(StepParams<Func>& params)
        requires HasInitialState<Derived>
    {
        auto s = params.state.convert<StateType>();
        if (!s) s.set_value(Derived::initial_state());
        return s;
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
        auto s = Derived::get_state(params);
        if (!s) return params.program.error();
        if (Derived::idempotent(*s, *a)) {
            params.program.new_continuation(
                params.func.copy(), params.args.copy());
            return;
        }
        SafeRef r = Derived::binop(params.program, *s, *a); // work()
        if (r.is_error()) {
            return params.program.fin_value(std::move(r));
        }
        params.program.new_continuation(
            params.program.m_alloc.takeref(params.program.m_alloc.Allocator().create_func(
                params.funcid, params.env.copy().take(), r.take())),
            std::move(params.args));
    }
};

template<>
struct FuncDispatch<OP_X> : public BinOpHelper<FuncDispatch<OP_X>, SafeView, SafeView> {
    static bool idempotent(const SafeView&, const SafeView&) { return true; }

    static std::optional<SafeView> get_state(StepParams<Func>& params)
    {
        return std::optional{params.state.copy()};
    }
    static SafeRef binop(Program&, const SafeView& state, const SafeView&) { return state.copy(); }

    static void finish(Program& program, SafeView)
    {
        program.error();
    }
};

template<>
struct FuncDispatch<OP_RC> : public BinOpHelper<FuncDispatch<OP_RC>, SafeRef, SafeRef> {
    static std::optional<SafeRef> get_state(StepParams<Func>& params)
    {
        return std::optional{params.state.copy()};
    }
    static SafeRef binop(Program& program, SafeRef& state, SafeRef& arg)
    {
        if (state.is_null()) {
            return std::move(arg);
        } else {
            return program.m_alloc.cons(std::move(arg), std::move(state));
        }
    }
    static void finish(Program& program, SafeView state)
    {
        if (state.is_null()) {
            program.fin_value(program.m_alloc.nil());
        } else {
            program.fin_value(state.copy());
        }
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
struct FuncDispatch<OP_STRLEN> : public BinOpHelper<FuncDispatch<OP_STRLEN>, int64_t, atomspan> {
    static int64_t initial_state() { return 0; }

    static bool idempotent(int64_t, const atomspan& arg) { return arg.size() == 0; }

    static SafeRef binop(Program& program, int64_t state, atomspan arg)
    {
        return program.m_alloc.create(state + static_cast<int64_t>(arg.size()));
    }
};

template<>
struct FuncDispatch<OP_LT_STR> : public BinOpHelper<FuncDispatch<OP_LT_STR>, SafeView, SafeView> {
    static std::optional<SafeView> get_state(StepParams<Func>& params)
    {
        return params.state;
    }

    static bool idempotent(const SafeView& state, const SafeView&)
    {
        auto cons = state.convert<std::pair<SafeView,SafeView>>();
        return cons.has_value();
    }

    static SafeRef binop(Program& program, SafeView state, SafeView arg)
    {
        if (auto argatom = arg.convert<atomspan>(); argatom) {
            if (state.is_null()) return arg.copy();

            if (auto stateatom = state.convert<atomspan>(); stateatom) {
                if (std::lexicographical_compare(stateatom->begin(), stateatom->end(), argatom->begin(), argatom->end())) {
                    return arg.copy();
                }
            }
            return program.m_alloc.cons(program.m_alloc.nil(), program.m_alloc.nil());
        } else {
            return program.m_alloc.error(); // LT_STR only accepts atoms
        }
    }

    static void finish(Program& program, SafeView state)
    {
        if (state.is_null()) {
            program.fin_value(program.m_alloc.one());
        } else if (auto a = state.convert<atomspan>(); a) {
            program.fin_value(program.m_alloc.one());
        } else {
            program.fin_value(program.m_alloc.nil());
        }
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

template<typename Derived, typename _ArgTup, size_t _MinArgs=std::tuple_size_v<_ArgTup>>
struct FixOpHelper {
    using ArgTup = _ArgTup;
    static constexpr size_t MinArgs = _MinArgs;
    static constexpr size_t MaxArgs = std::tuple_size_v<ArgTup>;

    template<typename T> struct ArgTup2ConvTup;
    template<typename... T> struct ArgTup2ConvTup<std::tuple<T...>> { using type = std::tuple<SafeConv::ConvertRef<T>...>; };

    using ConvTup = ArgTup2ConvTup<ArgTup>::type;

    template<size_t I>
    static auto& get_default(Program&)
        requires (MinArgs + std::tuple_size_v<decltype(Derived::Defaults)> == MaxArgs)
    {
        static_assert(I >= MinArgs && I < MaxArgs);
        return std::get<I-MinArgs>(Derived::Defaults);
    }

    // Derived:
    //  static constexpr std::tuple<...> Defaults{...};
    //  static SafeRef fixop(...);

    static void step(StepParams<FuncCount>& params)
    {
        static_assert(std::derived_from<Derived, FixOpHelper<Derived,ArgTup,MinArgs>>, "Derived must inherit from FixOpHelper<Derived> (CRTP requirement)");
        static_assert(MinArgs <= MaxArgs);

        if (!params.feedback.is_null()) {
            if (params.counter >= MaxArgs) return params.program.error(); // too many arguments
            SafeRef new_state = params.program.m_alloc.cons(std::move(params.feedback), params.state.copy());
            params.program.new_continuation(
                params.program.m_alloc.takeref(params.program.m_alloc.Allocator().create_func(
                    params.funcid, params.env.copy().take(), new_state.take(), params.counter+1)),
                std::move(params.args));
            return;
        }

        if (blleval_helper(params)) return;
        if (params.counter < MinArgs) return params.program.error(); // too few arguments
        if (params.counter > MaxArgs) return params.program.error(); // internal error: too many arguments, should be caught earlier

        // finalisation
        auto ref_arr = make_filled_array<SafeView,MaxArgs>(params.program.m_alloc.nullref());
        SafeView backarg = params.state;
        for (int64_t i = params.counter - 1; i >= 0; --i) {
            if (auto cons = backarg.convert<std::pair<SafeView,SafeView>>(); cons) {
                ref_arr[i] = cons->first;
                backarg = cons->second;
            } else {
                return params.program.error(); // internal error, state/counter mismatch (counter high)
            }
        }
        if (!backarg.is_null()) return params.program.error(); // internal error, state/counter mismatch (counter low)

        constexpr auto idx_seq = std::make_index_sequence<MaxArgs>{};
        auto tup_arr = [&]<size_t... Is>(std::index_sequence<Is...>) -> ConvTup {
            return ConvTup{std::get<Is>(ref_arr).template convert<std::tuple_element_t<Is, ArgTup>>()...};
        }(idx_seq);

        auto check = [&]<size_t... Is>(std::index_sequence<Is...>) -> bool {
            return ((Is >= params.counter || std::get<Is>(tup_arr).has_value()) && ...);
        };
        if (!check(idx_seq)) return params.program.error(); // bad arguments
        auto conv = [&]<size_t I>(std::integral_constant<size_t, I>) {
            static_assert(I < MaxArgs);
            if constexpr (I >= MinArgs) {
                if (I >= params.counter) {
                    return Derived::template get_default<I>(params.program);
                }
            }
            return *std::get<I>(tup_arr);
        };
        auto result = [&]<size_t... Is>(std::index_sequence<Is...>) -> SafeRef {
            return Derived::fixop(params.program, conv(std::integral_constant<size_t,Is>{})...);
        }(idx_seq);

        return params.program.fin_value(std::move(result));
    }
};

template<>
struct FuncDispatch<OP_IF> : public FixOpHelper<FuncDispatch<OP_IF>, std::tuple<bool, SafeView, SafeView>, 1> {
    template<size_t I>
    static SafeView get_default(Program& program)
    {
        return program.m_alloc.nullview();
    }
    static SafeRef fixop(Program& program, bool v, SafeView tval, SafeView fval) {
        SafeView r = v ? tval : fval;
        return (r.is_null() ? program.m_alloc.create(v) : r.copy());
    }
};

template<>
struct FuncDispatch<OP_HEAD> : public FixOpHelper<FuncDispatch<OP_HEAD>, std::tuple<std::pair<SafeView,SafeView>>> {
    static SafeRef fixop(Program&, const std::pair<SafeView,SafeView>& cons) {
        return cons.first.copy();
    }
};

template<>
struct FuncDispatch<OP_TAIL> : public FixOpHelper<FuncDispatch<OP_TAIL>, std::tuple<std::pair<SafeView,SafeView>>> {
    static SafeRef fixop(Program&, const std::pair<SafeView,SafeView>& cons) {
        return cons.second.copy();
    }
};

template<>
struct FuncDispatch<OP_LIST> : public FixOpHelper<FuncDispatch<OP_LIST>, std::tuple<SafeView>> {
    static SafeRef fixop(Program& program, const SafeView& arg) {
        auto cons = arg.convert<std::pair<SafeView,SafeView>>();
        bool r{cons.has_value()};
        return program.m_alloc.create(r);
    }
};

// XXX unimplemented functions, just to make it compile
#if 0
#endif
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

    if (rawalloc.is_error(m_feedback)) {
         // terminal error state: clear/free continuations
         while (!m_continuations.empty()) {
              Continuation c{pop_continuation()};
              rawalloc.deref(c.func.take());
              rawalloc.deref(c.args.take());
         }
         return;
    }

    Ref feedback{pop_feedback()};
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

#endif

} // Execution namespace

