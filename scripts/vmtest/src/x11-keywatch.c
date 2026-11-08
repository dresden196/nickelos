/* NickelOS: an X11 app that watches the keyboard the ways push-to-talk in
 * X11 apps does, without a window of its own, for testing that shortcut
 * keys reach X11 apps while other windows have the focus (nickel-x11-keys,
 * Chrome patch 0047) and that text does not. It prints:
 *   raw press|release KEYCODE    XInput 2 raw events (any key)
 *   grab press|release KEYCODE   its passive grab of Ctrl+Alt+J on the root
 *   keymap down|up KEYCODE       J's state in the keyboard map (polled)
 * X keycodes are evdev + 8: J is 44, Ctrl 37, Alt 64, F8 74.
 *   x11-keywatch SECONDS
 * Build: vmtest/build-test-client.sh x11-keywatch xcb xcb-xinput. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xinput.h>

#define KEY_J 44

int main(int argc, char** argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 60;
  xcb_connection_t* c = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(c)) {
    printf("no display\n");
    return 1;
  }
  xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;
  const xcb_query_extension_reply_t* xi =
      xcb_get_extension_data(c, &xcb_input_id);
  if (!xi || !xi->present) {
    printf("no XInput\n");
    return 1;
  }
  xcb_input_xi_query_version_reply_t* version = xcb_input_xi_query_version_reply(
      c, xcb_input_xi_query_version(c, 2, 2), NULL);
  free(version);
  struct {
    xcb_input_event_mask_t head;
    uint32_t mask;
  } mask = {{XCB_INPUT_DEVICE_ALL_MASTER, 1},
            XCB_INPUT_XI_EVENT_MASK_RAW_KEY_PRESS |
                XCB_INPUT_XI_EVENT_MASK_RAW_KEY_RELEASE};
  xcb_input_xi_select_events(c, root, 1, &mask.head);
  /* Ctrl+Alt+J, with and without Num Lock (Mod2) and Caps Lock. */
  const uint16_t locks[] = {0, XCB_MOD_MASK_2, XCB_MOD_MASK_LOCK,
                            XCB_MOD_MASK_2 | XCB_MOD_MASK_LOCK};
  for (size_t i = 0; i < sizeof(locks) / sizeof(locks[0]); ++i) {
    xcb_grab_key(c, 1, root,
                 XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | locks[i], KEY_J,
                 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  }
  xcb_flush(c);
  printf("watching\n");
  fflush(stdout);

  int down = 0;
  time_t end = time(NULL) + (time_t)seconds;
  while (time(NULL) < end) {
    xcb_generic_event_t* e;
    while ((e = xcb_poll_for_event(c))) {
      uint8_t type = e->response_type & ~0x80;
      if (type == XCB_GE_GENERIC) {
        xcb_input_raw_key_press_event_t* raw = (void*)e;
        if (raw->event_type == XCB_INPUT_RAW_KEY_PRESS ||
            raw->event_type == XCB_INPUT_RAW_KEY_RELEASE) {
          printf("raw %s %u\n",
                 raw->event_type == XCB_INPUT_RAW_KEY_PRESS ? "press"
                                                            : "release",
                 raw->detail);
        }
      } else if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) {
        printf("grab %s %u\n", type == XCB_KEY_PRESS ? "press" : "release",
               ((xcb_key_press_event_t*)e)->detail);
      }
      fflush(stdout);
      free(e);
    }
    if (xcb_connection_has_error(c)) {
      break;
    }
    xcb_query_keymap_reply_t* keymap =
        xcb_query_keymap_reply(c, xcb_query_keymap(c), NULL);
    if (keymap) {
      int now = (keymap->keys[KEY_J / 8] >> (KEY_J % 8)) & 1;
      if (now != down) {
        printf("keymap %s %d\n", now ? "down" : "up", KEY_J);
        fflush(stdout);
        down = now;
      }
      free(keymap);
    }
    usleep(20000);
  }
  printf("done\n");
  return 0;
}
