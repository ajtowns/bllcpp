#ifndef ELCONCEPT_H
#define ELCONCEPT_H

#include <elem.h>
#include <bounded.h>

#include <span.h>
#include <attributes.h>
#include <overloaded.h>

#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>
#include <optional>

class Arena;
class ElView;
class ElRef;
struct StepParams;

struct ElTypeTag { };
struct ATOM : ElTypeTag { static constexpr std::string name = "ATOM"; };
struct CONS : ElTypeTag { static constexpr std::string name = "CONS"; };
struct ERROR : ElTypeTag { static constexpr std::string name = "ERROR"; };
struct FUNC : ElTypeTag { static constexpr std::string name = "FUNC"; };

using ElTypeOrder = std::tuple<ATOM,CONS,ERROR,FUNC>;

template<typename T>
concept ElType = std::is_base_of_v<ElTypeTag,T>;

template<typename Fn>
static constexpr void PerConcept(Fn&& fn)
{
    std::apply([&](auto&&... args) { (fn(args), ...); }, ElTypeOrder{});
}

template<ElType>
struct ElConceptDef;

template<> struct ElConceptDef<ATOM> { static constexpr ElBaseType variants = 1; };
template<> struct ElConceptDef<CONS> { static constexpr ElBaseType variants = 1; };
template<> struct ElConceptDef<ERROR> { static constexpr ElBaseType variants = 1; };

namespace Func {
// use namespace so that we write Func::BLLEVAL like an enum class,
// but don't use an enum class so that they convert directly into an int
enum Func : ElBaseType {
    QUOTE,
    // APPLY,
    // SOFTFORK,
    // PARTIAL,
    OP_X,
    OP_HEAD,
    OP_TAIL,
    OP_LIST,
    // OP_RCONS,
    OP_IF,

    BLLEVAL,
};
} // namespace

template<> struct ElConceptDef<FUNC> {
    static constexpr ElBaseType variants{7};
    static const std::array<std::string, variants> func_name;

    static_assert(variants == Func::BLLEVAL + 1);
};

template<ElType Target>
static consteval ElBaseType ConceptOffset()
{
    int offset = 0;
    bool done = false;
    PerConcept(util::Overloaded(
        [&](Target) { done = true; },
        [&](auto et) {
            if (!done) offset += ElConceptDef<decltype(et)>::variants;
        }
    ));
    if (!done) throw std::out_of_range("not a valid ElType");
    return offset;
}

template<ElType ET>
bool TypeIsConcept(ElBaseType type)
{
    constexpr auto offset = ConceptOffset<ET>();
    return (offset <= type && type < offset + ElConceptDef<ET>::variants);
}

template<ElType ET>
class ElConcept;

template<ElType _ET>
class ElConceptBase
{
public:
    using ET = _ET;
    static constexpr ElBaseType variants{ElConceptDef<ET>::variants};
    using V = Bounded<variants>;

    ElConceptBase(const Elem& el LIFETIMEBOUND) : el{el} { }
    V variant() const {
        return V::make_checked(el.get_type() - ConceptOffset<ET>());
    }

    const Elem& get_el() const LIFETIMEBOUND { return el; }

private:
    const Elem& el;
};

template<>
class ElConcept<ATOM> : public ElConceptBase<ATOM>
{
public:
    // parent's constructor
    using ElConceptBase<ATOM>::ElConceptBase;

    static ElConcept<ATOM> init_as(Elem& el, int64_t n);

    void dealloc(ElRef&, ElRef&) { return; }

    Span<const uint8_t> data() const LIFETIMEBOUND;
    std::optional<int64_t> small_int() const;
};

template<>
class ElConcept<CONS> : public ElConceptBase<CONS>
{
public:
    // parent's constructor
    using ElConceptBase<CONS>::ElConceptBase;

    static ElConcept<CONS> init_as(Elem& el, ElRef&& left, ElRef&& right);

    ElView left() const LIFETIMEBOUND;
    ElView right() const LIFETIMEBOUND;

    void dealloc(ElRef& child1, ElRef& child2);
};

template<>
class ElConcept<ERROR> : public ElConceptBase<ERROR>
{
public:
    // parent's constructor
    using ElConceptBase<ERROR>::ElConceptBase;

    static ElConcept<ERROR> init_as(Elem& el);

    void dealloc(ElRef&, ElRef&) { };
};

template<>
class ElConcept<FUNC> : public ElConceptBase<FUNC>
{
public:
    // parent's constructor
    using ElConceptBase<FUNC>::ElConceptBase;

    static ElConcept<FUNC> init_as(Elem& el, Arena& arena, Func::Func fnid);

    template<typename... T>
    static ElConcept<FUNC> init_as(Elem& el, Arena& arena, ElConcept<FUNC> alike, T&&...);

    static constexpr Func::Func V2FnId(V variant) { return Func::Func(uint8_t{variant}); }

    Func::Func get_fnid() const { return V2FnId(variant()); }
    void step(StepParams& sp) const;

    void dealloc(ElRef& child1, ElRef& child2);
};

template<ElType ET,ElConcept<ET>::V>
struct ElVariant;

template<ElType ET,ElConcept<ET>::V V>
using ElData = ElVariant<ET, V>::ElData;

#endif // ELCONCEPT_H
