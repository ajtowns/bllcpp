
#include <workitem.h>
#include <elconcept.h>
#include <element.h>
#include <arena.h>
#include <elimpl.h>
#include <funcimpl.h>

#include <cstring>
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
  { 9, Func::OP_LIST },
  // { 10, Func::OP_BINTREE },
  // { 11, Func::OP_NOTALL },
  // { 12, Func::OP_ALL },
  // { 13, Func::OP_ANY },
  // { 14, Func::OP_EQ },
  { 15, Func::OP_LT_STR },
  { 16, Func::OP_STRLEN },
  { 17, Func::OP_SUBSTR },
  { 18, Func::OP_CAT },
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

int64_t get_opcode(Func::Func fn)
{
    for (const auto& [opcode, fnid] : bll_opcodes) {
        if (fn == fnid) return opcode;
    }
    return -1;
}

using func_name_array = std::array<std::string, ElConceptDef<FUNC>::variants>;

#define CASE_FUNC_NAME(F) case F: res[F] = #F; break
static func_name_array gen_func_names()
{
    func_name_array res;
    for (size_t i = 0; i < res.size(); ++i) {
        switch(static_cast<Func::Func>(i)) {
            CASE_FUNC_NAME(Func::QUOTE);
            CASE_FUNC_NAME(Func::OP_X);
            CASE_FUNC_NAME(Func::OP_HEAD);
            CASE_FUNC_NAME(Func::OP_TAIL);
            CASE_FUNC_NAME(Func::OP_LIST);
            CASE_FUNC_NAME(Func::OP_IF);
            CASE_FUNC_NAME(Func::OP_LT_STR);
            CASE_FUNC_NAME(Func::OP_STRLEN);
            CASE_FUNC_NAME(Func::OP_SUBSTR);
            CASE_FUNC_NAME(Func::OP_CAT);
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
    LogTrace(BCLog::BLL, "QUOTE step\n");
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
        ElRef res = apply_suffix(FO::fixop, arr, sp.wi.arena);
        sp.wi.fin_value(res.move());
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

