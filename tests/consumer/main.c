#ifdef _WIN32
#if ARC_HEADER_FIRST
#include "arc.h"
#include <windows.h>
#else
#include <windows.h>
#include "arc.h"
#endif
#else
#include "arc.h"
#endif

int main(void) {
#ifdef _WIN32
    /* The real GDI declaration must remain available. */
    BOOL(WINAPI * draw_arc)(HDC, int, int, int, int, int, int, int, int) = &Arc;
    (void)draw_arc;
#endif
    ArcWriter *writer = NULL;
    ArcResult result = arc_create("consumer.tar", &writer);
    if (result.code != ARC_OK)
        return 1;
    result = arc_add_text(writer, "hello.txt", "hello");
    if (result.code != ARC_OK) {
        arc_discard(&writer);
        return 2;
    }
    return arc_close(&writer).code != ARC_OK;
}
