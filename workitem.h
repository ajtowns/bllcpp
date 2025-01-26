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
public:
    Arena& arena;

private:
    std::vector<Continuation> continuations;
    ElRef feedback; // may be nullptr

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


public:
    template<typename T> struct Logic;
    explicit WorkItem(Arena& arena LIFETIMEBOUND, ElRef&& sexpr, ElRef&& env)
        : arena{arena}, feedback{nullptr}
    {
        continuations.reserve(1024);
        eval_sexpr(std::move(sexpr), std::move(env));
    }

    ~WorkItem() = default;

    WorkItem() = delete;
    WorkItem(const WorkItem&) = delete;
    WorkItem(WorkItem&&) = delete;

    ElView get_feedback() const LIFETIMEBOUND
    {
        return feedback.view();
    }
    const std::vector<Continuation>& get_continuations() const LIFETIMEBOUND
    {
        return continuations;
    }

    void new_continuation(ElRef&& func, ElRef&& args, ElRef&& env)
    {
        continuations.emplace_back(std::move(func), std::move(args), std::move(env));
    }

    void new_continuation(Func::Func fn, ElRef&& args, ElRef&& env)
    {
        new_continuation(arena.mkfn(fn), std::move(args), std::move(env));
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

    void step();

    bool finished() { return continuations.empty(); }
};

struct StepParams
{
    WorkItem& wi;
    ElRef args;
    ElRef env;
    ElRef feedback;

    StepParams(WorkItem& wi, ElRef&& args, ElRef&& env, ElRef&& feedback)
    : wi{wi}, args{std::move(args)}, env{std::move(env)}, feedback{std::move(feedback)}
    {
    }

    StepParams() = delete;
    StepParams(const StepParams&) = delete;
    StepParams(StepParams&&) = default;
    ~StepParams() = default;
};

#endif // WORKITEM_H
