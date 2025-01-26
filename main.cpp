#include <element.h>
#include <workitem.h>
#include <elconcept.h>
#include <elimpl.h>

#include <logging.h>

#include <ranges>
#include <iostream>

void test3(ElView ev=ElView{nullptr}) { (void)ev; }

void test1(Arena& arena)
{
    LogTrace(BCLog::BLL, "Hello\n");
    ElRef x{nullptr};
    {
        ElRef lucky = arena.New<ATOM>(1300);
        LogTrace(BCLog::BLL, "Wotzit\n");
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
        std::cout << "FB: " << fb.to_string() << std::endl;
    }
    auto& cs = wi.get_continuations();
    for (auto& c : cs | std::views::reverse) {
        std::cout << c.func.to_string() << " " << c.args.to_string() << " ENV: " << c.env.to_string() << std::endl;
    }
    std::cout << "---" << std::endl;
}

void run(WorkItem& wi)
{
    std::cout << "START" << std::endl;
    dump_cont(wi);
    while (!wi.finished()) {
        wi.step();
        dump_cont(wi);
    }
    std::cout << "END" << std::endl;
}

void test2(Arena& arena)
{
    WorkItem wi(arena, arena.mklist(7, arena.mklist(0, 1, 9)), arena.nil());
    run(wi);
}

void test3(Arena& arena)
{
    WorkItem wi(arena, arena.mklist(5, arena.New<CONS>(arena.nil(), arena.nil()), arena.mklist(0, 1, 9), arena.mklist(0, 33)), arena.nil());
    run(wi);
}

void test4(Arena& arena)
{
    WorkItem wi(arena, arena.mkel(31), arena.mklist(1,2,3,4,5));
    run(wi);
}

int main(void)
{
    Arena arena;
    test1(arena);
    test2(arena);
    test3(arena);
    test4(arena);
    return 0;
}
