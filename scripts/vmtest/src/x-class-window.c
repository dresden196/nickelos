// Runs in the test VM as the session user (nickel-session-exec): an X11
// window with the WM_CLASS and title given, as a game under Proton has
// (WM_CLASS steam_app_<id>), shown for SECONDS.
//   x-class-window CLASS TITLE SECONDS
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

int main(int argc, char** argv) {
  if (argc < 4) {
    return 2;
  }
  xcb_connection_t* c = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(c)) {
    return 1;
  }
  xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
  xcb_window_t w = xcb_generate_id(c);
  uint32_t values[] = {screen->white_pixel};
  xcb_create_window(c, XCB_COPY_FROM_PARENT, w, screen->root, 100, 100, 640,
                    400, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen->root_visual, XCB_CW_BACK_PIXEL, values);
  // WM_CLASS: instance and class, each ending in a NUL.
  size_t n = strlen(argv[1]);
  char* wm_class = malloc(2 * n + 2);
  memcpy(wm_class, argv[1], n + 1);
  memcpy(wm_class + n + 1, argv[1], n + 1);
  xcb_change_property(c, XCB_PROP_MODE_REPLACE, w, XCB_ATOM_WM_CLASS,
                      XCB_ATOM_STRING, 8, 2 * n + 2, wm_class);
  xcb_change_property(c, XCB_PROP_MODE_REPLACE, w, XCB_ATOM_WM_NAME,
                      XCB_ATOM_STRING, 8, strlen(argv[2]), argv[2]);
  xcb_map_window(c, w);
  xcb_flush(c);
  sleep(atoi(argv[3]));
  xcb_disconnect(c);
  return 0;
}
