#ifndef ELCONCEPT_H
#define ELCONCEPT_H

#include <elem.h>
#include <element.h>
#include <span.h>
#include <attributes.h>

#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>

class WorkItem;

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


template<ElType>
struct ElConceptDef;

template<> struct ElConceptDef<ATOM> { static constexpr ElBaseType variants = 1; };
template<> struct ElConceptDef<CONS> { static constexpr ElBaseType variants = 1; };
template<> struct ElConceptDef<ERROR> { static constexpr ElBaseType variants = 1; };
template<> struct ElConceptDef<FUNC> { static constexpr ElBaseType variants = 1; };

template<ElType ET>
class ElConcept;

template<ElType _ET>
class ElConceptParent
{
public:
    using ET = _ET;
    static constexpr ElBaseType variants{ElConceptDef<ET>::variants};
    using V = Bounded<variants>;

    template<bool O>
    ElConceptParent(const V& variant, const ElRefView<O>& er LIFETIMEBOUND) : variant{variant}, elview{er.view()} { }

    ElRef copy() { return elview.copy(); }

protected:
    const V variant;
    ElView elview;

    friend class ElConceptHelper;
};

template<ElType ET, Bounded<ElConceptDef<ET>::variants> V>
struct ElVariant;

template<>
class ElConcept<ATOM> : public ElConceptParent<ATOM>
{
public:
    // parent's constructor
    using ElConceptParent<ATOM>::ElConceptParent;

    void dealloc(ElRef&, ElRef&) { return; }

    Span<const uint8_t> data() const LIFETIMEBOUND;
};

template<>
class ElConcept<CONS> : public ElConceptParent<CONS>
{
public:
    // parent's constructor
    using ElConceptParent<CONS>::ElConceptParent;

    ElView left();
    ElView right();

    void dealloc(ElRef& child1, ElRef& child2);
};

template<>
class ElConcept<ERROR> : public ElConceptParent<ERROR>
{
public:
    // parent's constructor
    using ElConceptParent<ERROR>::ElConceptParent;

    void dealloc(ElRef&, ElRef&) { };
};

template<>
class ElConcept<FUNC> : public ElConceptParent<FUNC>
{
public:
    // parent's constructor
    using ElConceptParent<FUNC>::ElConceptParent;

    void dealloc(ElRef& child1, ElRef& child2);
};

#endif // ELCONCEPT_H
