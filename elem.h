/* Elem -- generic structure for holding elements in memory
 */

#ifndef ELEM_H
#define ELEM_H

#include <attributes.h>

#include <limits>
#include <stddef.h>
#include <stdint.h>

using ElBaseType = uint8_t;

class Elem
{
protected:
    struct GenElData { void* a; void* b; };

    alignas(GenElData) uint8_t eldata[sizeof(GenElData)];

    int32_t refcount{1};
    ElBaseType type{0};

    template<typename ElData>
    static constexpr bool SaneElData = sizeof(ElData) <= sizeof(GenElData) && alignof(ElData) <= alignof(GenElData);

    template<typename ElData, typename Self>
    static ElData* dataptr(Self&& self)
    {
        static_assert(SaneElData<ElData>);
        return reinterpret_cast<ElData*>(&self.eldata);
    }

public:
    void incref() { ++refcount; }
    [[nodiscard]] bool decref() { return --refcount <= 0; }
    int get_refcount() const { return refcount; }

    void set_type(ElBaseType new_type) { type = new_type; }
    ElBaseType get_type() const { return type; }

    template<typename ElData> auto& data_rw() LIFETIMEBOUND { return *Elem::dataptr<ElData>(*this); }
    template<typename ElData> auto& data_ro() const LIFETIMEBOUND { return *Elem::dataptr<const ElData>(*this); }
};

static_assert(alignof(Elem) >= alignof(void*));

#endif // ELEM_H
