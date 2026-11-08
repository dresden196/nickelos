// Lists the X11 windows below the root that have a WM_CLASS (two levels
// down: Xwayland's rootless windows and their frames), one per line:
//   <window id> <mapped|unmapped> pid=<_NET_WM_PID> class=<instance>/<class> title=<WM_NAME>
// Reads only. Run as the session user with DISPLAY and XAUTHORITY set:
// what the shelf matches X11 apps by (wmclass.<class>).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

static xcb_atom_t Atom(xcb_connection_t* c, const char* name) {
  xcb_intern_atom_reply_t* r = xcb_intern_atom_reply(
      c, xcb_intern_atom(c, 1, strlen(name), name), NULL);
  xcb_atom_t atom = r ? r->atom : XCB_ATOM_NONE;
  free(r);
  return atom;
}

static xcb_get_property_reply_t* Property(xcb_connection_t* c,
                                          xcb_window_t w,
                                          xcb_atom_t atom) {
  return xcb_get_property_reply(
      c, xcb_get_property(c, 0, w, atom, XCB_GET_PROPERTY_TYPE_ANY, 0, 1024),
      NULL);
}

static void Show(xcb_connection_t* c, xcb_window_t w, xcb_atom_t pid_atom) {
  xcb_get_property_reply_t* cls = Property(c, w, XCB_ATOM_WM_CLASS);
  if (!cls || xcb_get_property_value_length(cls) == 0) {
    free(cls);
    return;
  }
  const char* v = xcb_get_property_value(cls);
  int n = xcb_get_property_value_length(cls);
  const char* instance = v;
  const char* klass = memchr(v, 0, n) ? v + strlen(v) + 1 : "";
  if (klass >= v + n) {
    klass = "";
  }
  xcb_get_property_reply_t* name = Property(c, w, XCB_ATOM_WM_NAME);
  xcb_get_property_reply_t* pid = Property(c, w, pid_atom);
  xcb_get_window_attributes_reply_t* attr = xcb_get_window_attributes_reply(
      c, xcb_get_window_attributes(c, w), NULL);
  printf("0x%08x %s pid=%u class=%s/%s title=%.*s\n", w,
         attr && attr->map_state == XCB_MAP_STATE_VIEWABLE ? "mapped"
                                                           : "unmapped",
         pid && xcb_get_property_value_length(pid) == 4
             ? *(uint32_t*)xcb_get_property_value(pid)
             : 0,
         instance, klass, name ? xcb_get_property_value_length(name) : 0,
         name ? (const char*)xcb_get_property_value(name) : "");
  free(cls);
  free(name);
  free(pid);
  free(attr);
}

static void Walk(xcb_connection_t* c, xcb_window_t w, xcb_atom_t pid_atom,
                 int depth) {
  xcb_query_tree_reply_t* tree =
      xcb_query_tree_reply(c, xcb_query_tree(c, w), NULL);
  if (!tree) {
    return;
  }
  xcb_window_t* kids = xcb_query_tree_children(tree);
  for (int i = 0; i < xcb_query_tree_children_length(tree); i++) {
    Show(c, kids[i], pid_atom);
    if (depth > 1) {
      Walk(c, kids[i], pid_atom, depth - 1);
    }
  }
  free(tree);
}

int main(void) {
  xcb_connection_t* c = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(c)) {
    fprintf(stderr, "cannot connect to the X server\n");
    return 1;
  }
  xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
  Walk(c, screen->root, Atom(c, "_NET_WM_PID"), 2);
  xcb_disconnect(c);
  return 0;
}
