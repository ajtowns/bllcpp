#include <execution.h>

#include <func.h>
#include <buddy.h>
#include <saferef.h>
#include <overloaded.h>
#include <crypto/sha256.h>

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
template<auto FuncId> struct FuncDefinition;

template<FuncEnum FE, FE FuncId> struct FuncDispatch;

template<>
struct FuncDispatch<Func, BLLEVAL> {
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
struct FuncDispatch<Func, QUOTE> {
    static void step(StepParams<Func>& params)
    {
        params.program.fin_value(std::move(params.args));
    }
};

template<Func FuncId>
struct FuncDispatch<Func, FuncId> {
    using Derived = FuncDefinition<FuncId>;
    using StateType = Derived::StateType;
    using ArgType = Derived::ArgType;

    static constexpr bool HasInitialState = requires(SafeAllocator a) {
        { a.create(Derived::initial_state()) } -> std::same_as<SafeRef>;
        { a.nil().convert<decltype(Derived::initial_state())>().set_value(Derived::initial_state()) };
   };

    static constexpr bool HasIdempotent = requires(const StateType& s, const ArgType& a) { static_cast<bool>(Derived::idempotent(s,a)); };
    static constexpr bool HasFinish = requires(Program& p, const StateType& s) { Derived::finish(p, s); };
    static constexpr bool HasGetState = requires(StepParams<Func>& p) { StateType{*Derived::get_state(p)}; };

    static void finish(Program& program, SafeView state)
    {
        static_assert(HasFinish || HasInitialState);
        if constexpr (HasFinish) {
            return Derived::finish(program, state);
        } else {
            if (state.is_null()) {
                program.fin_value(program.m_alloc.create(Derived::initial_state()));
            } else {
                program.fin_value(state.copy());
            }
        }
    }

    static auto get_state(StepParams<Func>& params)
    {
        static_assert(HasGetState || HasInitialState);
        if constexpr (HasGetState) {
            return Derived::get_state(params);
        } else {
            auto s = params.state.convert<StateType>();
            if (!s) s.set_value(Derived::initial_state());
            return s;
        }
    }

    static SafeRef partial_step(StepParams<Func>& params)
    {

        auto a = params.feedback.convert<ArgType>();
        if (!a) return params.program.m_alloc.error();
        auto s = get_state(params);
        if (!s) return params.program.m_alloc.error();
        if constexpr (HasIdempotent) {
            if (Derived::idempotent(*s, *a)) {
                return params.func.copy();
            }
        }
        SafeRef r = Derived::binop(params.program, *s, *a);
        if (r.is_error()) {
            return r;
        } else {
            return params.program.m_alloc.takeref(
                    params.program.m_alloc.Allocator().create_func(
                        params.funcid, params.env.copy().take(), r.take()));
        }
    }

    static void step(StepParams<Func>& params)
    {
        if (!params.feedback.is_null()) {
            SafeRef r = partial_step(params);
            if (r.is_error()) {
                params.program.fin_value(std::move(r));
            } else {
                params.program.new_continuation(std::move(r), std::move(params.args));
            }
        } else if (blleval_helper(params)) {
            // blleval handled it
        } else {
            finish(params.program, params.state);
        }
    }
};

template<>
struct FuncDefinition<OP_PARTIAL> {
    using StateType = SafeView;
    using ArgType = SafeView;

    static auto get_state(StepParams<Func>& params) { return params.state.convert<SafeView>(); }
    static SafeRef binop(Program& program, const SafeView& state, const SafeView&)
    {
        SafeRef new_state = state.nullref();
        new_state = program.m_alloc.error(); // XXX unimplementable :(
        return new_state;
#if 0
        if (state.is_null()) {
            if (arg.is_funcy()) {
                new_state = state.copy(); // result of a previous partial
            } else if (auto op = arg.convert<int64_t>(); op) {
                std::visit(util::Overloaded(
                    [&](FuncEnum auto funcid) {
                        new_state = program.m_alloc.create(funcid, params.env.copy()); // params vs program
                    },
                    [&](const std::monostate&) {
                        new_state = program.m_alloc.error(); // invalid opcode
                    }), lookup_opcode(*op));
            } else {
                new_state = program.m_alloc.error(); // not something function-like
            }
        } else {
            new_state = program.m_alloc.error(); // XXX unimplementable :(
        }
        return new_state;
#endif
    }

    static void finish(Program& program, SafeView)
    {
        program.error(); // XXX
    }
};

template<>
struct FuncDefinition<OP_X> {
    using StateType = SafeView;
    using ArgType = SafeView;

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
struct FuncDefinition<OP_RC> {
    using StateType = SafeRef;
    using ArgType = SafeRef;

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
struct FuncDefinition<OP_NOTALL> {
    using StateType = bool;
    using ArgType = bool;

    static bool initial_state() { return false; }
    static SafeRef binop(Program& program, bool state, bool arg)
    {
        return program.m_alloc.create(state || !arg);
    }
};

template<>
struct FuncDefinition<OP_ALL> {
    using StateType = bool;
    using ArgType = bool;

    static bool initial_state() { return true; }
    static SafeRef binop(Program& program, bool state, bool arg)
    {
        return program.m_alloc.create(state && arg);
    }
};

template<>
struct FuncDefinition<OP_ANY> {
    using StateType = bool;
    using ArgType = bool;

    static bool initial_state() { return false; }
    static SafeRef binop(Program& program, bool state, bool arg)
    {
        return program.m_alloc.create(state || arg);
    }
};

template<>
struct FuncDefinition<OP_STRLEN> {
    using StateType = int64_t;
    using ArgType = atomspan;

    static int64_t initial_state() { return 0; }

    static bool idempotent(int64_t, const atomspan& arg) { return arg.size() == 0; }

    static SafeRef binop(Program& program, int64_t state, atomspan arg)
    {
        return program.m_alloc.create(state + static_cast<int64_t>(arg.size()));
    }
};

template<>
struct FuncDefinition<OP_LT_STR> {
    using StateType = SafeView;
    using ArgType = SafeView;

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
struct FuncDefinition<OP_CAT> {
    using StateType = atomspan;
    using ArgType = atomspan;

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
struct FuncDefinition<OP_ADD> {
    using StateType = int64_t;
    using ArgType = int64_t;

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

template<FuncCount FuncId>
struct FuncDispatch<FuncCount, FuncId> {
    using Derived = FuncDefinition<FuncId>;

    using ArgTup = Derived::ArgTup;
    static constexpr size_t MinArgs = Derived::MinArgs;
    static constexpr size_t MaxArgs = std::tuple_size_v<ArgTup>;
    static_assert(MinArgs <= MaxArgs);

    template<typename T> struct ArgTup2ConvTup;
    template<typename... T> struct ArgTup2ConvTup<std::tuple<T...>> { using type = std::tuple<SafeConv::ConvertRef<T>...>; };

    using ConvTup = ArgTup2ConvTup<ArgTup>::type;

    template<size_t I>
    static constexpr bool HasGetDefault = requires(Program& p) { Derived::template get_default<I>(p); };

    template<size_t I>
    static auto get_default(Program& program)
    {
        if constexpr (HasGetDefault<I>) {
            return Derived::template get_default<I>(program);
        } else {
            static_assert(MinArgs + std::tuple_size_v<decltype(Derived::Defaults)> == MaxArgs);
            static_assert(I >= MinArgs && I < MaxArgs);
            return std::get<I-MinArgs>(Derived::Defaults);
        }
    }

    // Derived:
    //  static constexpr std::tuple<...> Defaults{...};
    //  static SafeRef fixop(params, ...);

    static void step(StepParams<FuncCount>& params)
    {
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
                    return get_default<I>(params.program);
                }
            }
            return *std::get<I>(tup_arr);
        };
        auto result = [&]<size_t... Is>(std::index_sequence<Is...>) -> SafeRef {
            return Derived::fixop(params, conv(std::integral_constant<size_t,Is>{})...);
        }(idx_seq);

        return params.program.fin_value(std::move(result));
    }
};

template<>
struct FuncDefinition<OP_APPLY> {
    using ArgTup = std::tuple<SafeView, SafeView>;
    static constexpr size_t MinArgs = 1;

    template<size_t I>
    static SafeView get_default(Program& program)
    {
        return program.m_alloc.nullview();
    }

    static SafeRef fixop(StepParams<FuncCount>& params, SafeView expr, SafeView env)
    {
        if (env.is_null()) env = params.env;
        params.program.new_continuation(BLLEVAL, env.copy(), expr.copy());
        return params.program.m_alloc.nullref();
    }
};

template<>
struct FuncDefinition<OP_IF> {
    using ArgTup = std::tuple<bool, SafeView, SafeView>;
    static constexpr size_t MinArgs = 1;

    template<size_t I>
    static SafeView get_default(Program& program)
    {
        return program.m_alloc.nullview();
    }
    static SafeRef fixop(StepParams<FuncCount>& params, bool v, SafeView tval, SafeView fval) {
        SafeView r = v ? tval : fval;
        return (r.is_null() ? params.program.m_alloc.create(v) : r.copy());
    }
};

template<>
struct FuncDefinition<OP_HEAD> {
    using ArgTup = std::tuple<std::pair<SafeView,SafeView>>;
    static constexpr size_t MinArgs = 1;

    static SafeRef fixop(StepParams<FuncCount>&, const std::pair<SafeView,SafeView>& cons) {
        return cons.first.copy();
    }
};

template<>
struct FuncDefinition<OP_TAIL> {
    using ArgTup = std::tuple<std::pair<SafeView,SafeView>>;
    static constexpr size_t MinArgs = 1;

    static SafeRef fixop(StepParams<FuncCount>&, const std::pair<SafeView,SafeView>& cons) {
        return cons.second.copy();
    }
};

template<>
struct FuncDefinition<OP_LIST> {
    using ArgTup = std::tuple<SafeView>;
    static constexpr size_t MinArgs = 1;

    static SafeRef fixop(StepParams<FuncCount>& params, const SafeView& arg) {
        auto cons = arg.convert<std::pair<SafeView,SafeView>>();
        bool r{cons.has_value()};
        return params.program.m_alloc.create(r);
    }
};

template<>
struct FuncDefinition<OP_SUBSTR> {
    using ArgTup = std::tuple<atomspan,int64_t,int64_t>;
    static constexpr size_t MinArgs = 1;

    static constexpr std::tuple<int64_t,int64_t> Defaults{0,std::numeric_limits<int64_t>::max()};
    static SafeRef fixop(StepParams<FuncCount>& params, atomspan sp, int64_t start, int64_t size)
    {
        start = std::clamp<int64_t>(start, -static_cast<int64_t>(sp.size()), static_cast<int64_t>(sp.size()));
        if (start < 0) start = sp.size() + start;
        size = std::clamp<int64_t>(size, 0, sp.size() - start);
        return params.program.m_alloc.create(sp.subspan(start, size));
    }
};

template<typename T>
inline T* DupeObject(const T* old)
{
    void* x = std::malloc(sizeof(T));
    if (old != nullptr) {
        std::memcpy(x, old, sizeof(T));
        return static_cast<T*>(x);
    } else {
        T* res = new(x) T;
        return res;
    }
}

template<FuncExt FuncId>
struct FuncDispatch<FuncExt, FuncId> {
    using Derived = FuncDefinition<FuncId>;
    using State = Derived::State;
    using ArgType = Derived::ArgType;

    static void step(StepParams<FuncExt>& params)
    {
        if (params.feedback.is_null()) {
            if (blleval_helper(params)) return;
            Derived::finish(params.program, static_cast<const State*>(params.state));
            return;
        }

        auto a = params.feedback.convert<ArgType>();
        if (!a) return params.program.error();

        State* r = Derived::extop(params.program, static_cast<const State*>(params.state), *a); // work()
        if (r == nullptr) return params.program.error(); // internal failure

        params.program.new_continuation(
            params.program.m_alloc.takeref(params.program.m_alloc.Allocator().create_func(
                params.funcid, params.env.copy().take(), static_cast<const void*>(r))),
            std::move(params.args));
    }
};

template<>
struct FuncDefinition<OP_SHA256> {
    using State = CSHA256;
    using ArgType = atomspan;

    static CSHA256* extop(Program&, const CSHA256* state, atomspan arg)
    {
        CSHA256* x = DupeObject<CSHA256>(state);
        x->Write(arg.data(), arg.size());
        return x;
    }

    static void finish(Program& program, const CSHA256* state)
    {
        static const CSHA256 init_state{};
        if (state == nullptr) state = &init_state;
        CSHA256 fin{*state};
        std::array<uint8_t, 32> res;
        fin.Finalize(res.data());
        program.fin_value(program.m_alloc.create(std::span(res)));
    }
};

template<FuncEnum FE>
struct FuncEnumDispatch {
    template<auto Getter, template<typename, FE> typename Dispatcher>
    static constexpr auto mk_dispatch_table() {
        return []<size_t... I>(std::index_sequence<I...>) {
            return std::array{ Getter.template operator()<Dispatcher<FE, static_cast<FE>(I)>>()... };
        }(std::make_index_sequence<FuncEnumSize<FE>>{});
    }

    static constexpr auto get_step_fn = []<typename T>() -> void(*)(StepParams<FE>&) { return &T::step; };
    static constexpr auto get_partial_step_fn = []<typename T>() -> SafeRef(*)(StepParams<FE>&) {
        if constexpr (requires { &T::partial_step; }) {
            return &T::partial_step;
        } else {
            return nullptr;
        }
    };

    static constexpr auto step_dispatch = mk_dispatch_table<get_step_fn, FuncDispatch>();
    static constexpr auto partial_step_dispatch = mk_dispatch_table<get_partial_step_fn, FuncDispatch>();

    static void step(StepParams<FE>&& params)
    {
        return (step_dispatch[static_cast<size_t>(params.funcid)])(params);
    }

    static SafeRef partial_step(StepParams<FE>&& params)
    {
        auto f = partial_step_dispatch[static_cast<size_t>(params.funcid)];
        if (f == nullptr) {
            return params.program.m_alloc.error();
        } else {
            return f(params);
        }
    }
};

} // anonymous namespace

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

    Ref func{cont.func.take()};
    Ref args{cont.args.take()};

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

} // Execution namespace

