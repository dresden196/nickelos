/* NickelOS: the one xcb-util function xcb-imdkit uses (xcb-util is not in
 * the image), as xcb-util has it. */
#ifndef NICKEL_XCB_AUX_H
#define NICKEL_XCB_AUX_H
#include <xcb/xcb.h>

static inline xcb_screen_t *xcb_aux_get_screen(xcb_connection_t *c,
                                               int screen) {
    xcb_screen_iterator_t i = xcb_setup_roots_iterator(xcb_get_setup(c));
    for (; i.rem; --screen, xcb_screen_next(&i))
        if (screen == 0)
            return i.data;
    return 0;
}
#endif
