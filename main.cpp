#include <buddy.h>
#include <saferef.h>
#include <element.h>
#include <workitem.h>
#include <elconcept.h>
#include <elimpl.h>
#include <execution.h>
#include <func.h>

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
        ElRef trouble = arena.New<ATOM>(arena, MakeUCharSpan("Hello, world"));
        LogTrace(BCLog::BLL, "Wotzit\n");
        auto yo = lucky.copy();
        LogTrace(BCLog::BLL, "Soon\n");
        yo.reset();
        LogTrace(BCLog::BLL, "Next\n");
        x = arena.New<CONS>(arena.mklist(1,2,arena.mklist(3,arena.mkfn(Func::BLLEVAL),3,trouble.move()),4,5, arena.error()), lucky.move());
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

void dump_cont(const Execution::Program& wi)
{
    SafeView fb = wi.inspect_feedback();
    auto& cs = wi.inspect_continuations();
    std::cout << strprintf("Conts: %d ; FB: %s", cs.size(), fb.is_null() ? "-null-" : fb.to_string()) << std::endl;
    for (auto& c : cs | std::views::reverse) {
        std::cout << Buddy::to_string(wi.m_alloc.Allocator(), c.func) << " " << Buddy::to_string(wi.m_alloc.Allocator(), c.args) << std::endl;
    }
    std::cout << "---" << std::endl;
}

void run(WorkItem& wi)
{
    std::cout << "START workitem" << std::endl;
    dump_cont(wi);
    while (!wi.finished()) {
        wi.step();
        dump_cont(wi);
    }
    std::cout << "END" << std::endl;
}

void run(Execution::Program& program)
{
    std::cout << "START program" << std::endl;
    dump_cont(program);
    while (!program.finished()) {
        program.step();
        dump_cont(program);
    }
    std::cout << "END" << std::endl;
}

void test2(Arena& arena)
{
    WorkItem wi(arena, arena.mklist(Func::OP_HEAD, arena.mklist(Func::QUOTE, 1, 9)), arena.nil());
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

void test5(Arena& arena)
{
    auto q = [&](auto e) { return arena.New<CONS>(arena.nil(), arena.mkel(e)); };
    WorkItem wi(arena, arena.mklist(Func::OP_STRLEN, q(1), q(1000), q(100000)), arena.nil());
    run(wi);
}

void test6(Arena& arena)
{
    std::cout << "test 6" << std::endl;
    auto q = [&](auto e) { return arena.New<CONS>(arena.nil(), arena.mkel(e)); };
    WorkItem wi(arena, arena.mklist(Func::OP_CAT, q(1), q(1000), q(100000)), arena.nil());
    run(wi);
}

void test7(Arena& arena)
{
    auto q = [&](auto e) { return arena.New<CONS>(arena.nil(), arena.mkel(e)); };
    WorkItem wi(arena, arena.mklist(Func::OP_ADD, q(1), q(2), q(3), q(4), q(5), q(6), q(-7)), arena.nil());
    run(wi);
}

void test8(Arena& arena)
{
    std::cout << "test 6" << std::endl;
    auto q = [&](auto e) { return arena.New<CONS>(arena.nil(), arena.mkel(e)); };
    WorkItem wi(arena, arena.mklist(Func::OP_CAT, q(1), arena.mklist(arena.nil(), 1, 2, 3), q(100000)), arena.nil());
    run(wi);
}

void test9(Buddy::Allocator& alloc)
{
    alloc.DumpChunks();

    Buddy::Ref r[] = {
        alloc.create<Buddy::Tag::CONS, 16>({.left=alloc.nil(), .right=alloc.one()}),
        alloc.create("hello"),
        alloc.create("hello, world!"),
        alloc.create("the quick brown fox jumps over the lazy dog"),
        alloc.create("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"),
        alloc.create<Buddy::Tag::CONS, 16>({.left=alloc.nil(), .right=alloc.nil()}),
        alloc.create_list("hello", "there", "you", "munchkin"),
        alloc.create_list("primes", 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31),
    };
    alloc.DumpChunks();

    std::cout << "r[] = {";
    bool first = true;
    for (auto& x : r) {
         std::cout << (first ? "" : " ; ") << to_string(alloc, x);
         first = false;
    }
    std::cout << "}" << std::endl;

    auto r2 = alloc.create<Buddy::Tag::CONS, 16>({.left=alloc.bumpref(r[0]), .right=alloc.bumpref(r[1])});
    alloc.DumpChunks();

    alloc.deref(std::move(r[0]));
    alloc.DumpChunks();

    Buddy::Ref r3 = alloc.nil();
    for (int i = 0; i < 4000; ++i) {
        r3 = alloc.create_cons((i % 2 == 0 ? alloc.nil() : alloc.one()), std::move(r3));
    }
    std::cout << "r3 = " << Buddy::to_string(alloc, r3) << std::endl;

    alloc.deref(std::move(r2));
    alloc.DumpChunks();

    alloc.deref(std::move(r3));
    alloc.DumpChunks();

    r3 = alloc.create_cons(alloc.one(), alloc.one());
    alloc.DumpChunks();

    for (auto& x : r) { alloc.deref(std::move(x)); }
    alloc.DumpChunks();
}

void test10(Buddy::Allocator& raw_alloc)
{
    SafeAllocator alloc(raw_alloc);
    alloc.DumpChunks();

    constexpr auto q = Buddy::quote; // short alias for quoting

    SafeRef r[] = {
        alloc.cons(alloc.nil(), alloc.one()),
        alloc.create("hello"),
        alloc.create("hello, world!"),
        alloc.create("the quick brown fox jumps over the lazy dog"),
        alloc.create("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"),
        alloc.cons(alloc.nil(), alloc.nil()),
        alloc.create_list("hello", "there", "you", "munchkin"),
        alloc.create_list("primes", 2, 3, q(5), 7, q(11), 13, 17, 19, 23, 29, 31),
        alloc.create_list(OP_ADD, q(1), q(2), q(3)),
    };
    alloc.DumpChunks();

    std::cout << "r[] = {";
    bool first = true;
    for (auto& x : r) {
         std::cout << (first ? "" : " ; ") << x.to_string();
         first = false;
    }
    std::cout << "}" << std::endl;

    auto r2 = alloc.cons(r[0].copy(), r[1].copy());
    alloc.DumpChunks();

    r[0] = r[1].copy();
    alloc.DumpChunks();

    SafeRef r3 = alloc.nil();
    for (int i = 0; i < 4000; ++i) {
        r3 = alloc.cons((i % 2 == 0 ? alloc.nil() : alloc.one()), std::move(r3));
    }
    std::cout << "r3 = " << r3.to_string() << std::endl;

    r2 = alloc.nil();
    alloc.DumpChunks();

    r3 = alloc.nil();
    alloc.DumpChunks();

    r3 = alloc.cons(alloc.one(), alloc.one());
    alloc.DumpChunks();

    for (auto& x : r) { x = alloc.nil(); }
    alloc.DumpChunks();
}

void test11(Buddy::Allocator& raw_alloc)
{
    SafeAllocator alloc(raw_alloc);
    alloc.DumpChunks();

    constexpr auto q = Buddy::quote; // short alias for quoting

    SafeRef sexpr = alloc.create_list(OP_CAT, q("hello"), q(" "), q("world"), alloc.create_list(OP_ADD, q(1), q(2), q(3), q(4), q(5), q(6), q(7), q(5)));
    SafeRef env = alloc.nil();

    std::cout << "test11 sexpr=" << sexpr.to_string() << "; env=" << env.to_string() << std::endl;
    Execution::Program p{alloc, std::move(sexpr), std::move(env)};
    run(p);
}

int main(void)
{
  {
    Arena arena;
    test1(arena);
    test2(arena);
    test3(arena);
    test4(arena);
    test5(arena);
    test6(arena);
    test7(arena);
    test8(arena);

    std::cout << "======================" << std::endl;
  }
    Buddy::Allocator alloc;
    test9(alloc);
    test10(alloc);
    test11(alloc);
    alloc.DumpChunks();
    return 0;
}
