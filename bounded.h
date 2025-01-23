#ifndef BOUNDED_H
#define BOUNDED_H

#include <elem.h>

#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>

template<ElBaseType _MAX>
class Bounded
{
private:
    using T = ElBaseType;

    struct internal_only_tag {};
    static constexpr  internal_only_tag internal_only;

    explicit Bounded(const T& v, internal_only_tag) : v{v} { }

public:
    const T v;

    static consteval bool in_range(const T& val) { return 0 <= val && val < MAX; }

    static constexpr T MAX{_MAX};
    static_assert(MAX > 0);
    static constexpr T LAST{_MAX-1};

    static Bounded make_checked(const T& checked) { return Bounded{checked, internal_only}; }

    constexpr Bounded() : v{0} { };
    constexpr Bounded(const Bounded& v) : v{v.v} { }
    constexpr Bounded(Bounded&& v) : v{v.v} { }

    explicit (false) consteval Bounded(const T& v) : v{v}
    {
        if (v >= MAX) throw std::out_of_range("out of range");
    }

    explicit (false) constexpr operator T() const
    {
        return v;
    }
};

#endif // BOUNDED_H
