/* NickelOS: an X11 window that prints the keys it gets ("press KEYCODE
 * STATE", "release KEYCODE STATE"), for testing that keys a global shortcut
 * takes never reach the window with the focus, while other keys do
 * (hw/gs-test.sh focus). Its WM_CLASS is nickel-key-test.
 *   key-test SECONDS
 * Build: vmtest/build-test-client.sh key-test xcb. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>

int main(int argc, char** argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 30;
  xcb_connection_t* c = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(c)) {
    printf("no display\n");
    return 1;
  }
  xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
  xcb_window_t window = xcb_generate_id(c);
  uint32_t values[] = {screen->white_pixel, XCB_EVENT_MASK_KEY_PRESS |
                                                XCB_EVENT_MASK_KEY_RELEASE |
                                                XCB_EVENT_MASK_FOCUS_CHANGE};
  xcb_create_window(c, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, 400,
                    300, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                    XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);
  const char wm_class[] = "nickel-key-test\0nickel-key-test";
  xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS,
                      XCB_ATOM_STRING, 8, sizeof(wm_class), wm_class);
  const char title[] = "Key test";
  xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME,
                      XCB_ATOM_STRING, 8, strlen(title), title);
  xcb_map_window(c, window);
  xcb_flush(c);
  printf("mapped\n");
  fflush(stdout);

  time_t end = time(NULL) + (time_t)seconds;
  while (time(NULL) < end) {
    xcb_generic_event_t* e = xcb_poll_for_event(c);
    if (!e) {
      if (xcb_connection_has_error(c)) {
        break;
      }
      usleep(10000);
      continue;
    }
    switch (e->response_type & ~0x80) {
      case XCB_KEY_PRESS:
      case XCB_KEY_RELEASE: {
        xcb_key_press_event_t* k = (xcb_key_press_event_t*)e;
        printf("%s %d %d\n",
               (e->response_type & ~0x80) == XCB_KEY_PRESS ? "press"
                                                           : "release",
               k->detail, k->state);
        fflush(stdout);
        break;
      }
      case XCB_FOCUS_IN:
        printf("focus\n");
        fflush(stdout);
        break;
    }
    free(e);
  }
  printf("done\n");
  return 0;
}
