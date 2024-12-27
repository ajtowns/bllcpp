/* element arena
 *
 * creates a fixed size arena, which
 * stores refcounted elements of type ATOM, CONS, ERROR, FUNCx
 */

#include <element.h>

#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>

void Element::deref(Element* el)
{
    if (!el) return;

    Element* rest{nullptr};

    while (el != nullptr) {
        --el->refcount;
        if (el->refcount == 0) {
            Element* a{nullptr};
            Element* b{nullptr};

            el->visit([&](auto* d) { d->dealloc(&a, &b); });

            if (a && b) {
                auto& elcons = el->force_as<CONS>();
                elcons.left = b;
                elcons.right = rest;
                rest = el;
                el = a;
            } else {
                if (b) std::swap(a, b);
                free(el); // XXX Allocator
                el = a;
            }
        }
        while (!el && rest) {
            auto& restcons = *rest->get<CONS>();
            el = restcons.left;
            Element* next = restcons.right;
            free(rest); // XXX Allocator
            rest = next;
         }
     }
}

