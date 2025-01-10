#include <elem.h>
#include <element.h>
#include <elconcept.h>

#include <array>
#include <optional>
#include <vector>

class Arena
{
private:
    ElRef m_nil;

public:
    Arena();

    template<ElType ET, int Variant, typename... T>
    ElRef New(T&&... args)
    {
        Elem* el = new Elem;
        ElRef res{std::move(el)};
        res.template init_as<ET, Variant>(std::forward<decltype(args)>(args)...);
        return res.move();
    }

    ElRef nil() { return m_nil.copy(); }
};

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
    std::optional<ElRef> feedback{std::nullopt};

    Arena arena;
    // costings

    ElRef pop_feedback()
    {
        ElRef res{*std::move(feedback)};
        feedback.reset();
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
    void step(ElementConcept::ElConcept<ET>& fn, ElRef&& args, ElRef&& env, ElRef&& feedback);

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
#if 0
    void eval_sexpr(ElRef&& sexpr, ElRef&& env)
    {
        new_continuation(arena.New<FUNC,OP_BLLEVAL>(arena.nil()), std::move(sexpr), std::move(env));
    }
#endif

    void fin_value(ElRef&& val)
    {
        feedback = std::move(val);
    }

    void error()
    {
        using enum ElType;
        fin_value(arena.New<ERROR,0>());
    }

    void cont();

};

