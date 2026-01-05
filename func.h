#ifndef FUNC_H
#define FUNC_H

namespace Buddy {
enum class Func : uint16_t {
    BLLEVAL,
    QUOTE,
    // PARTIAL,
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
    APPLY,
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
    // OP_SHA256,
    // OP_RIPEMD160,
    // OP_HASH160,
    // OP_HASH256,
    // OP_SECP256K1_MULADD,
};
} // Buddy namespace

#endif // FUNC_H
