#include <element.h>

#include <array>
#include <optional>
#include <vector>

class Arena
{
private:
    ElRef m_nil;

public:
    template<ElType ET, typename... T>
    ElRef New(T&&... args) 
    {
        Element* el = new Element;
        auto& eldata = el->force_as<ET>();
        eldata.init(std::forward<decltype(args)>(args)...);
        return ElRef{std::move(el)};
    }

    ElRef nil() {
        if (m_nil.is_nullptr()) {
            m_nil = New<SMLATOM>(0);
        }
        return ElRef{m_nil};
    }
};

struct Continuation
{
    ElRef func;
    ElRef args;
    ElRef env;
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
        return res;
    }

    Continuation pop_continuation()
    {
        Continuation c{std::move(continuations.back())};
        continuations.pop_back();
        return c;
    }

    void cont(Continuation&& cont, ElRef feedback);
    void step(Continuation&& cont);

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

    void eval_sexpr(ElRef&& sexpr, ElRef&& env)
    {
        new_continuation(arena.New<BLLEVAL>(arena.nil()), std::move(sexpr), std::move(env));
    }

    void fin_value(ElRef&& val)
    {
        feedback = std::move(val);
    }

    void error()
    {
        fin_value(arena.New<ERROR>());
    }

    void step()
    {
        if (continuations.empty()) return;

        if (feedback) {
            cont(pop_continuation(), pop_feedback());
        } else {
            step(pop_continuation());
        }
    }
};


