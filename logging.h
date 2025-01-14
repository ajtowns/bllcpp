#ifndef LOGGING_H
#define LOGGING_H

#include <tinyformat.h>

enum class BCLog {
    BLL,
};

template<typename... Args>
void LogTrace(BCLog cat, util::ConstevalFormatString<sizeof...(Args)> fmt, Args&&... args)
{
    (void)cat;
    auto l = tfm::format(fmt, args...);
    std::cout << l;
}

#endif
