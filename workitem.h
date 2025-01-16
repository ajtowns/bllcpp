#ifndef WORKITEM_H
#define WORKITEM_H

#include <elem.h>
#include <element.h>
#include <elconcept.h>
#include <arena.h>

#include <logging.h>

#include <array>
#include <optional>
#include <vector>

struct Continuation
{
    ElRef func;
    ElRef args;
    ElRef env;

    Continuation(ElRef&& func, ElRef&& args, ElRef&& env) : func{std::move(func)}, args{std::move(args)}, env{std::move(env)} { }
    Continuation(Continuation&& cont) = default;
    ~Continuation() = default;
};

class WorkItem
{
private:
    std::vector<Continuation> continuations;
    ElRef feedback; // may be nullptr

    Arena arena;
    // costings

    // CTransactionRef tx;
    // int input_idx;
    // std::vector<CTxOut> spent_coins;
    //   -- no access to Coin's fCoinbase or nHeight?

    ElRef pop_feedback()
    {
        ElRef res{feedback.move()};
        return res.move();
    }

    Continuation pop_continuation()
    {
        Continuation c{std::move(continuations.back())};
        continuations.pop_back();
        return c;
    }

    template<ElType ET> struct Logic;

    template<ElType ET>
    void step(ElConcept<ET>& fn, ElRef&& args, ElRef&& env, ElRef&& feedback);

public:
    explicit WorkItem(ElRef&& sexpr, ElRef&& env)
    {
        continuations.reserve(1024);
        eval_sexpr(std::move(sexpr), std::move(env));
    }

    void new_continuation(ElRef&& func, ElRef&& args, ElRef&& env)
    {
        continuations.emplace_back(std::move(func), std::move(args), std::move(env));
    }

    void eval_sexpr(ElRef&& sexpr, ElRef&& env);

    void fin_value(ElRef&& val)
    {
        feedback = std::move(val);
    }

    void error()
    {
        fin_value(arena.error());
    }

    void cont();

};

#endif // WORKITEM_H
