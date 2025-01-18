
#include <element.h>
#include <workitem.h>
#include <elconcept.h>
#include <elimpl.h>

#include <logging.h>

#include <iostream>

void test1(Arena& arena)
{
    LogTrace(BCLog::BLL, "Hello\n");
    ElRef x;
    {
        ElRef lucky = arena.New<ATOM>(1300);
        auto yo = lucky.copy();
        LogTrace(BCLog::BLL, "Soon\n");
        yo.reset();
        LogTrace(BCLog::BLL, "Next\n");
        x = arena.New<CONS>(arena.mklist(1,2,arena.mklist(3,arena.mkfn(Func::BLLEVAL),3),4,5, arena.New<ERROR>()), lucky.move());
    }
    std::cout << x.to_string() << std::endl;
    LogTrace(BCLog::BLL, "Goodbye\n");
}

void dump_cont(const WorkItem& wi)
{
    auto fb = wi.get_feedback();
    if (fb) {
        std::cout << "FB: " << fb << std::endl;
    }
    auto& cs = wi.get_continuations();
    for (auto& c : cs) {
        std::cout << c.func.to_string() << " " << c.args.to_string() << " ENV: " << c.env.to_string() << std::endl;
    }
    std::cout << "---" << std::endl;
}

void test2(Arena& arena)
{
    WorkItem wi(arena, arena.mklist(1, 1, 1), arena.nil());

    dump_cont(wi);
    wi.step();
    dump_cont(wi);
}

int main(void)
{
    Arena arena;
    test1(arena);
    test2(arena);
    return 0;
}
