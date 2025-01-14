
#include <element.h>
#include <workitem.h>

#include <logging.h>

int main(void)
{
    Arena arena;
    LogTrace(BCLog::BLL, "Hello\n");
    ElRef x;
    {
        ElRef lucky = arena.New<ATOM>(13);
        auto yo = lucky.copy();
        LogTrace(BCLog::BLL, "Soon\n");
        yo.reset();
        LogTrace(BCLog::BLL, "Next\n");

        x = arena.New<CONS>(lucky.move(), arena.New<ATOM>(17));
    }
    LogTrace(BCLog::BLL, "Goodbye\n");
    return 0;
}
