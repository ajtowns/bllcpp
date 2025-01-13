/* element arena
 *
 * creates a fixed size arena, which
 * stores refcounted elements of type ATOM, CONS, ERROR, FUNCx
 */

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

namespace ElementConcept {

using enum ElType;

template<unsigned int MAX>
struct Bounded
{
    using T = unsigned int;

    static_assert(MAX > 0);

    const T v;

    explicit (false) constexpr Bounded(const T& v) : v{v}
    {
        if (v >= MAX) throw std::out_of_range("out of range");
    }

    explicit (false) constexpr operator T() const
    {
        return v;
    }
};

template<ElType ET>
class ElConcept;

template<typename EC>
class ElConceptParent
{
protected:
    int variant;
    ElView elview;

public:
    ElConceptParent(int variant, ElRef& er LIFETIMEBOUND) : variant{variant}, elview{er.view()} { }
    ElConceptParent(int variant, ElView& ev LIFETIMEBOUND) : variant{variant}, elview{ev} { }

    ElRef copy() { return elview.copy(); }
};

template<>
class ElConcept<ATOM> : public ElConceptParent<ElConcept<ATOM>>
{
public:
    static constexpr int variants = 1;
    template<Bounded<variants> V> struct Variant;

    template<> struct Variant<0>
    {
        struct ElData {
            int64_t n{0};
            ElData(int64_t n) : n{n} { };
        };
    };

    // parent's constructor
    using ElConceptParent<ElConcept<ATOM>>::ElConceptParent;

    void dealloc(ElRef&, ElRef&) { return; }

    Span<const uint8_t> data() const;
};

template<>
class ElConcept<CONS> : public ElConceptParent<ElConcept<CONS>>
{
public:
    static constexpr int variants = 1;
    template<Bounded<variants> V> struct Variant;

    template<> struct Variant<0>
    {
        struct ElData {
            ElRef left, right;
            ElData(ElRef&& left, ElRef&& right) : left{left.move()}, right{right.move()} { }
        };
    };

    // data members
    ElView left, right;

    // custom constructor to populate left/right
    ElConcept(int variant, ElView& ev LIFETIMEBOUND);
    ElConcept(int variant, ElRef& ev LIFETIMEBOUND);

    void dealloc(ElRef& child1, ElRef& child2);
};

template<>
class ElConcept<ERROR> : public ElConceptParent<ElConcept<ERROR>>
{
public:
    static constexpr int variants = 1;
    template<Bounded<variants> V> struct Variant;

    template<> struct Variant<0>
    {
        struct ElData { };
    };

    // parent's constructor
    using ElConceptParent<ElConcept<ERROR>>::ElConceptParent;

    void dealloc(ElRef&, ElRef&) { };

    // XXX...
};

template<>
class ElConcept<FUNC> : public ElConceptParent<ElConcept<FUNC>>
{
public:
    static constexpr int variants = 1; // XXX
    template<Bounded<variants> V> struct Variant;

    // parent's constructor
    using ElConceptParent<ElConcept<FUNC>>::ElConceptParent;

    void dealloc(ElRef& child1, ElRef& child2);

    // XXX...
};

} // namespace ElementConcept

#endif // ELCONCEPT_H
