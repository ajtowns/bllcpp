
#include <element.h>
#include <workitem.h>
#include <elconcept.h>
#include <elimpl.h>

#include <logging.h>

#include <iostream>

int main(void)
{
    Arena arena;
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
    return 0;
}
