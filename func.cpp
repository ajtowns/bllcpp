#include <func.h>

#include <overloaded.h>

#include <array>
#include <string>
#include <variant>

namespace {

using namespace Buddy;

struct OpCodeInfo
{
    using opcode_type = uint8_t;
    static constexpr opcode_type BAD_OPCODE{0xFF};

    std::array<opcode_type, FuncEnumSize<Func>> ops_Func;
    std::array<opcode_type, FuncEnumSize<FuncCount>> ops_FuncCount;
    std::array<opcode_type, FuncEnumSize<FuncExt>> ops_FuncExt;

    std::array<FuncVariant, 256> op_funcs;

    template<typename FE>
    static constexpr auto gen_array(const auto& init) {
        std::array<opcode_type, FuncEnumSize<FE>> res;
        res.fill(BAD_OPCODE);
        for (auto [i, op] : init) {
            if (std::holds_alternative<FE>(op)) {
                auto funcid = static_cast<size_t>(std::get<FE>(op));
                if (res[funcid] != BAD_OPCODE) throw; // duplicate opcode
                res[funcid] = i;
            }
        }
        return res;
    }

    static constexpr auto gen_opfuncs(const auto& init) {
        std::array<FuncVariant, 256> res;
        res.fill(std::monostate{});
        for (auto [i, op] : init) {
            if (!std::holds_alternative<std::monostate>(res[i])) {
                throw; // duplicate entry
            }
            if (std::holds_alternative<std::monostate>(op)) {
                throw; // defining opcode as undefined??
            }
            res[i] = op;
        }
        return res;
    }

    constexpr int64_t get_opcode(Func f) const { return ops_Func[static_cast<size_t>(f)]; }
    constexpr int64_t get_opcode(FuncCount f) const { return ops_FuncCount[static_cast<size_t>(f)]; }
    constexpr int64_t get_opcode(FuncExt f) const { return ops_FuncExt[static_cast<size_t>(f)]; }

    constexpr OpCodeInfo(const std::initializer_list<std::pair<opcode_type, FuncVariant>>& init)
      : ops_Func{gen_array<Func>(init)}
      , ops_FuncCount{gen_array<FuncCount>(init)}
      , ops_FuncExt{gen_array<FuncExt>(init)}
      , op_funcs{gen_opfuncs(init)}
    {
    }

    template<FuncEnum... FE>
    constexpr bool HasNoOpcode(FE... funcid) const
    {
        return ((this->get_opcode(funcid) == BAD_OPCODE) && ...);
    }

    constexpr size_t NumNoOpcode() const
    {
        size_t r{0};
        for (auto v : ops_Func) if (v == BAD_OPCODE) ++r;
        for (auto v : ops_FuncCount) if (v == BAD_OPCODE) ++r;
        for (auto v : ops_FuncExt) if (v == BAD_OPCODE) ++r;
        return r;
    }
};
} // anonymous namespace

static constexpr OpCodeInfo OPCODE_INFO{ {
  { 0, QUOTE },
  { 1, OP_APPLY },
  // { 2, SOFTFORK },
  { 3, OP_PARTIAL },
  { 4, OP_X },
  { 5, OP_IF },
  { 6, OP_RC },
  { 7, OP_HEAD },
  { 8, OP_TAIL },
  { 9, OP_LIST },
  // { 10, OP_BINTREE },
  { 11, OP_NOTALL },
  { 12, OP_ALL },
  { 13, OP_ANY },
  // { 14, OP_EQ },
  { 15, OP_LT_STR },
  { 16, OP_STRLEN },
  { 17, OP_SUBSTR },
  { 18, OP_CAT },
  // { 19, OP_NAND_BYTES },
  // { 20, OP_AND_BYTES },
  // { 21, OP_OR_BYTES },
  // { 22, OP_XOR_BYTES },
  { 23, OP_ADD },
  // { 24, OP_SUB },
  // { 25, OP_MUL },
  // { 26, OP_MOD },
  // { 27, OP_SHIFT },
  // { 28, ? },
  // { 29, ? },
  // { 30, OP_LT_NUM },
  // { 31, ? },
  // { 32, OP_RD },
  // { 33, OP_WR },
  { 34, OP_SHA256 },
  // { 35, OP_RIPEMD160 },
  // { 36, OP_HASH160 },
  // { 37, OP_HASH256 },
  // { 38, OP_BIP340_VERIFY },
  // { 39, OP_ECDSA_VERIFY },
  // { 40, OP_SECP256K1_MULADD },
  // { 41, OP_TX },
  // { 42, OP_BIP342_TXMSG },

  // { 0xff, OP_DEEP_EQUAL }, // "===", check structural equality, debug only?
}};
static_assert(OPCODE_INFO.HasNoOpcode(BLLEVAL));
static_assert(OPCODE_INFO.NumNoOpcode() == 1);

namespace Buddy {

int64_t get_opcode(Func f) { return OPCODE_INFO.ops_Func[static_cast<size_t>(f)]; }
int64_t get_opcode(FuncCount f) { return OPCODE_INFO.ops_FuncCount[static_cast<size_t>(f)]; }
int64_t get_opcode(FuncExt f) { return OPCODE_INFO.ops_FuncExt[static_cast<size_t>(f)]; }

FuncVariant lookup_opcode(int64_t op)
{
    if (op < int64_t{OPCODE_INFO.op_funcs.size()}) {
        return OPCODE_INFO.op_funcs[op];
    } else {
        return std::monostate{};
    }
}

#define OP_NAME(x) case x: return #x;
std::string get_funcname(FuncVariant funcid)
{
    return std::visit(util::Overloaded(
        [](Func funcid) -> std::string {
            switch (funcid) {
                OP_NAME(BLLEVAL)
                OP_NAME(QUOTE)
                OP_NAME(OP_PARTIAL)
                OP_NAME(OP_X)
                OP_NAME(OP_RC)
                OP_NAME(OP_NOTALL)
                OP_NAME(OP_ALL)
                OP_NAME(OP_ANY)
                OP_NAME(OP_LT_STR)
                OP_NAME(OP_STRLEN)
                OP_NAME(OP_CAT)
                OP_NAME(OP_ADD)
            }
        },
        [](FuncCount funcid) -> std::string {
            switch (funcid) {
                OP_NAME(OP_APPLY)
                OP_NAME(OP_IF)
                OP_NAME(OP_HEAD)
                OP_NAME(OP_TAIL)
                OP_NAME(OP_LIST)
                OP_NAME(OP_SUBSTR)
            }
        },
        [](FuncExt funcid) -> std::string {
            switch (funcid) {
                OP_NAME(OP_SHA256)
            }
        },
        [](const std::monostate&) -> std::string { return {}; }),
        funcid);
}
#undef OP_NAME

} // Buddy namespace

