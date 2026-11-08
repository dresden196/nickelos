/* NickelOS: an X11 app with an old-style tray icon (XEmbed, the freedesktop
 * System Tray Protocol), as Wine programs and Java apps have one, for testing
 * that such icons reach ChromeOS's shelf too (nickel-xembed-tray). It docks a
 * small icon window with the tray (_NET_SYSTEM_TRAY_S0) and prints
 * "docked" once embedded, then "button N" for each click that reaches it.
 * WM_CLASS and _NET_WM_PID name it nickel-xembed-test, the Id of its
 * desktop file (hw/tray-test.sh).
 *   xembed-test SECONDS
 * Build: vmtest/build-test-client.sh xembed-test xcb. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>

static xcb_atom_t intern(xcb_connection_t* c, const char* name) {
  xcb_intern_atom_reply_t* r =
      xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, strlen(name), name), NULL);
  xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
  free(r);
  return a;
}

int main(int argc, char** argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 30;
  xcb_connection_t* c = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(c)) {
    printf("no display\n");
    return 1;
  }
  xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
  xcb_window_t icon = xcb_generate_id(c);
  uint32_t values[] = {screen->white_pixel,
                       XCB_EVENT_MASK_BUTTON_PRESS |
                           XCB_EVENT_MASK_BUTTON_RELEASE |
                           XCB_EVENT_MASK_STRUCTURE_NOTIFY};
  xcb_create_window(c, XCB_COPY_FROM_PARENT, icon, screen->root, 0, 0, 16, 16,
                    0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                    XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);
  const char wm_class[] = "nickel-xembed-test\0nickel-xembed-test";
  xcb_change_property(c, XCB_PROP_MODE_REPLACE, icon, XCB_ATOM_WM_CLASS,
                      XCB_ATOM_STRING, 8, sizeof(wm_class), wm_class);
  uint32_t pid = getpid();
  xcb_change_property(c, XCB_PROP_MODE_REPLACE, icon,
                      intern(c, "_NET_WM_PID"), XCB_ATOM_CARDINAL, 32, 1,
                      &pid);
  uint32_t info[] = {0, 1};  // version 0, XEMBED_MAPPED
  xcb_atom_t xembed_info = intern(c, "_XEMBED_INFO");
  xcb_change_property(c, XCB_PROP_MODE_REPLACE, icon, xembed_info, xembed_info,
                      32, 2, info);

  xcb_atom_t selection = intern(c, "_NET_SYSTEM_TRAY_S0");
  xcb_get_selection_owner_reply_t* owner = xcb_get_selection_owner_reply(
      c, xcb_get_selection_owner(c, selection), NULL);
  if (!owner || owner->owner == XCB_NONE) {
    printf("no tray\n");
    return 1;
  }
  xcb_client_message_event_t dock;
  memset(&dock, 0, sizeof(dock));
  dock.response_type = XCB_CLIENT_MESSAGE;
  dock.format = 32;
  dock.window = owner->owner;
  dock.type = intern(c, "_NET_SYSTEM_TRAY_OPCODE");
  dock.data.data32[0] = XCB_CURRENT_TIME;
  dock.data.data32[1] = 0;  // SYSTEM_TRAY_REQUEST_DOCK
  dock.data.data32[2] = icon;
  xcb_send_event(c, 0, owner->owner, XCB_EVENT_MASK_NO_EVENT,
                 (const char*)&dock);
  xcb_flush(c);
  free(owner);
  printf("requested\n");
  fflush(stdout);

  xcb_atom_t xembed = intern(c, "_XEMBED");
  time_t end = time(NULL) + (time_t)seconds;
  while (time(NULL) < end) {
    xcb_generic_event_t* e = xcb_poll_for_event(c);
    if (!e) {
      if (xcb_connection_has_error(c)) {
        break;
      }
      usleep(20000);
      continue;
    }
    switch (e->response_type & ~0x80) {
      case XCB_BUTTON_PRESS:
        printf("button %d\n", ((xcb_button_press_event_t*)e)->detail);
        fflush(stdout);
        break;
      case XCB_CLIENT_MESSAGE:
        if (((xcb_client_message_event_t*)e)->type == xembed &&
            ((xcb_client_message_event_t*)e)->data.data32[1] == 0) {
          printf("docked\n");
          fflush(stdout);
        }
        break;
    }
    free(e);
  }
  printf("done\n");
  return 0;
}
