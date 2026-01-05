#ifndef EXECUTION_H
#define EXECUTION_H

#include <buddy.h>
#include <saferef.h>
#include <func.h>

#include <logging.h>

#include <array>
#include <optional>
#include <vector>

namespace Execution {

struct Continuation
{
    Buddy::Ref func; // function, state and environment
    Buddy::Ref args; // further args for the function to process

    Continuation(Buddy::Ref&& func, Buddy::Ref&& args) : func{func.take()}, args{args.take()} { }

    // no copy
    Continuation(const Continuation&& cont) = delete;
    Continuation& operator=(const Continuation&& cont) = delete;

    // only move
    Continuation(Continuation&& cont) : func{cont.func.take()}, args{cont.args.take()} { }

    Continuation& operator=(Continuation&& cont)
    {
        func = cont.func.take();
        args = cont.args.take();
        return *this;
    }

    ~Continuation() = default;
};

class Program
{
public:
    SafeAllocator& m_alloc;

private:
    static constexpr auto NULLREF = Buddy::NULLREF;

    std::vector<Continuation> m_continuations;
    Buddy::Ref m_feedback{NULLREF};

    // costings

    // CTransactionRef tx;
    // int input_idx;
    // std::vector<CTxOut> spent_coins;
    //   -- no access to Coin's fCoinbase or nHeight?

    [[nodiscard]] Buddy::Ref pop_feedback()
    {
        return m_feedback.take();
    }

    [[nodiscard]] Continuation pop_continuation()
    {
        Continuation c{std::move(m_continuations.back())};
        m_continuations.pop_back();
        return c;
    }


public:
    template<typename T> struct Logic;
    explicit Program(SafeAllocator& alloc LIFETIMEBOUND, Buddy::Ref&& sexpr, Buddy::Ref&& env)
        : m_alloc{alloc}, m_feedback{NULLREF}
    {
        m_continuations.reserve(1024);
        eval_sexpr(sexpr.take(), env.take());
    }

    ~Program() = default;

    Program() = delete;
    Program(const Program&) = delete;
    Program(Program&&) = delete;

    Buddy::Ref inspect_feedback() const LIFETIMEBOUND
    {
        return m_feedback;
    }

    const std::vector<Continuation>& inspect_continuations() const LIFETIMEBOUND
    {
        return m_continuations;
    }

    void new_continuation(Buddy::Ref&& func, Buddy::Ref&& env)
    {
        m_continuations.emplace_back(func.take(), env.take());
    }

    void fin_value(Buddy::Ref&& val)
    {
        assert(m_feedback.is_null());
        m_feedback = val.take();
    }

    void error(std::source_location sloc=std::source_location::current())
    {
        fin_value(m_alloc.Allocator().create_error(sloc));
    }

    void eval_sexpr(Buddy::Ref&& sexpr, Buddy::Ref&& env);

    void step();

    bool finished() { return m_continuations.empty(); }
};

} // Execution namespace

#endif // EXECUTION_H
