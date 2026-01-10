#ifndef FUNC_H
#define FUNC_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

namespace Buddy {
enum class Func : uint16_t {
    BLLEVAL, // internal only, no opcode
    QUOTE,
    OP_PARTIAL,
    OP_X,
    OP_RC,
    // OP_BINTREE,
    // OP_EQ,
    OP_NOTALL,
    OP_ALL,
    OP_ANY,
    OP_LT_STR,
    OP_STRLEN,
    OP_CAT,
    // OP_NAND_BYTES,
    // OP_AND_BYTES,
    // OP_OR_BYTES,
    // OP_XOR_BYTES,
    OP_ADD,
    // OP_SUB,
    // OP_MUL,
    // OP_LT_NUM,
    // OP_TX,
};

enum class FuncCount : uint16_t {
    OP_APPLY,
    // SOFTFORK,
    OP_IF,
    OP_HEAD,
    OP_TAIL,
    OP_LIST,
    OP_SUBSTR,
    // OP_MOD,
    // OP_SHIFT,
    // OP_RD,
    // OP_WR,
    // OP_BIP340_VERIFY,
    // OP_ECDSA_VERIFY,
    // OP_BIP342_TXMSG,
};

enum class FuncExt : uint8_t {
    OP_SHA256,
    // OP_RIPEMD160,
    // OP_HASH160,
    // OP_HASH256,
    // OP_SECP256K1_MULADD,
};

template<typename T>
concept FuncEnum =
    std::is_same_v<T, Func> ||
    std::is_same_v<T, FuncCount> ||
    std::is_same_v<T, FuncExt>;

template<FuncEnum FE> struct FuncEnum_help;
template<> struct FuncEnum_help<Func> { static constexpr size_t value = 12; };
template<> struct FuncEnum_help<FuncCount> { static constexpr size_t value = 6; };
template<> struct FuncEnum_help<FuncExt> { static constexpr size_t value = 1; };

template<FuncEnum FE>
inline constexpr size_t FuncEnumSize{FuncEnum_help<FE>::value};

int64_t get_opcode(Func f);
int64_t get_opcode(FuncCount f);
int64_t get_opcode(FuncExt f);

using FuncVariant = std::variant<std::monostate, Func, FuncCount, FuncExt>;

FuncVariant lookup_opcode(int64_t);

std::string get_funcname(FuncVariant funcid);

} // Buddy namespace

using enum Buddy::Func;
using enum Buddy::FuncCount;
using enum Buddy::FuncExt;

#endif // FUNC_H
