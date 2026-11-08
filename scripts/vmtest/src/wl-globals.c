/* NickelOS: which Wayland protocols a client is shown, and what happens when
 * it binds one it is not shown. Prints each global's interface and version,
 * one per line, on the display in WAYLAND_DISPLAY (or argv[1], a socket name
 * or path). With "--bind NAME [NUMBER]", it then binds global NUMBER (a
 * number from a listing, on any socket) as NAME and prints "bound" or the
 * protocol error the server answers with ("error ...").
 * Build: see vmtest/exo-protocols-test.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version) {
  printf("%u %s %u\n", name, interface, version);
}

static void global_remove(void *data, struct wl_registry *registry,
                          uint32_t name) {}

static const struct wl_registry_listener listener = {global, global_remove};

int main(int argc, char **argv) {
  const char *display_name = NULL;
  const char *bind_interface = NULL;
  uint32_t bind_name = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--bind") == 0 && i + 2 < argc) {
      bind_interface = argv[++i];
      bind_name = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else {
      display_name = argv[i];
    }
  }
  struct wl_display *display = wl_display_connect(display_name);
  if (!display) {
    perror("wl_display_connect");
    return 1;
  }
  struct wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &listener, NULL);
  wl_display_roundtrip(display);
  if (bind_interface) {
    /* The interface's name and version are what go on the wire; any
     * interface struct serves for the request itself. */
    struct wl_interface fake = {bind_interface, 1, 0, NULL, 0, NULL};
    wl_registry_bind(registry, bind_name, &fake, 1);
    if (wl_display_roundtrip(display) < 0) {
      const struct wl_interface *iface = NULL;
      uint32_t id = 0;
      int code = wl_display_get_protocol_error(display, &iface, &id);
      printf("error %d on %s\n", code, iface ? iface->name : "?");
    } else {
      printf("bound\n");
    }
  }
  wl_display_disconnect(display);
  return 0;
}
