/* NickelOS: asks for the screen the way a screen sharing app does (the
 * ScreenCast portal: CreateSession, SelectSources, Start, then
 * OpenPipeWireRemote) and receives frames over PipeWire. Prints each step's
 * answer, then "frames=N size=WxH lit=P%" (P: share of non-black pixels in
 * the last frame) after N frames or 20 s. Start waits for the user to pick a
 * screen in ChromeOS's picker. Build: see vmtest/screencast-test.sh. */
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <stdio.h>
#include <string.h>

#define PORTAL "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define SCREENCAST "org.freedesktop.portal.ScreenCast"

static GDBusConnection *bus;
static GMainLoop *gloop;
static guint32 response_code;
static GVariant *response_results;

static void on_response(GDBusConnection *c, const gchar *s, const gchar *p,
                        const gchar *i, const gchar *sig, GVariant *params,
                        gpointer data) {
  g_variant_get(params, "(u@a{sv})", &response_code, &response_results);
  g_main_loop_quit(gloop);
}

/* Calls a portal method whose answer comes as the Response of a Request. */
static gboolean call(const char *method, GVariant *args, const char *token) {
  char *sender = g_strdup(g_dbus_connection_get_unique_name(bus) + 1);
  for (char *c = sender; *c; ++c)
    if (*c == '.') *c = '_';
  char *handle = g_strdup_printf(PORTAL_PATH "/request/%s/%s", sender, token);
  guint sub = g_dbus_connection_signal_subscribe(
      bus, PORTAL, "org.freedesktop.portal.Request", "Response", handle, NULL,
      G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, on_response, NULL, NULL);
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_sync(
      bus, PORTAL, PORTAL_PATH, SCREENCAST, method, args, NULL,
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (!reply) {
    printf("%s: %s\n", method, error->message);
    return FALSE;
  }
  g_variant_unref(reply);
  g_main_loop_run(gloop);
  g_dbus_connection_signal_unsubscribe(bus, sub);
  printf("%s: response %u\n", method, response_code);
  g_free(handle);
  g_free(sender);
  return response_code == 0;
}

static struct pw_main_loop *ploop;
static struct pw_stream *stream;
static struct spa_video_info_raw format;
static int frames;
static double lit;

static void on_param_changed(void *data, uint32_t id, const struct spa_pod *p) {
  if (p && id == SPA_PARAM_Format) {
    spa_format_video_raw_parse(p, &format);
    printf("format: %ux%u\n", format.size.width, format.size.height);
  }
}

static void on_process(void *data) {
  struct pw_buffer *b = pw_stream_dequeue_buffer(stream);
  if (!b) return;
  struct spa_data *d = &b->buffer->datas[0];
  if (d->data && d->chunk->size > 0) {
    const uint32_t *px = d->data;
    size_t n = d->chunk->size / 4, on = 0;
    for (size_t i = 0; i < n; i += 97)
      if (px[i] & 0x00ffffff) ++on;
    lit = 100.0 * on / (n / 97 + 1);
    if (++frames >= 10) {
      /* The last frame, as /tmp/screencast-frame.ppm, to look at. */
      FILE *f = fopen("/tmp/screencast-frame.ppm", "wb");
      if (f && format.size.width) {
        uint32_t w = format.size.width, h = format.size.height;
        fprintf(f, "P6 %u %u 255\n", w, h);
        for (uint32_t i = 0; i < w * h && i < n; ++i) {
          uint32_t p = px[i];
          unsigned char rgb[3] = {(p >> 16) & 0xff, (p >> 8) & 0xff, p & 0xff};
          fwrite(rgb, 1, 3, f);
        }
      }
      if (f) fclose(f);
      pw_main_loop_quit(ploop);
    }
  }
  pw_stream_queue_buffer(stream, b);
}

static const struct pw_stream_events events = {
    PW_VERSION_STREAM_EVENTS,
    .param_changed = on_param_changed,
    .process = on_process,
};

static void on_timeout(void *data, uint64_t expirations) {
  pw_main_loop_quit(ploop);
}

int main(int argc, char **argv) {
  gloop = g_main_loop_new(NULL, FALSE);
  bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);

  GVariantBuilder o;
  g_variant_builder_init(&o, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&o, "{sv}", "handle_token", g_variant_new_string("t1"));
  g_variant_builder_add(&o, "{sv}", "session_handle_token",
                        g_variant_new_string("s1"));
  if (!call("CreateSession", g_variant_new("(a{sv})", &o), "t1")) return 1;
  const char *session = NULL;
  g_variant_lookup(response_results, "session_handle", "&s", &session);
  printf("session: %s\n", session);

  g_variant_builder_init(&o, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&o, "{sv}", "handle_token", g_variant_new_string("t2"));
  g_variant_builder_add(&o, "{sv}", "types", g_variant_new_uint32(1));
  g_variant_builder_add(&o, "{sv}", "cursor_mode", g_variant_new_uint32(2));
  if (!call("SelectSources", g_variant_new("(oa{sv})", session, &o), "t2"))
    return 1;

  g_variant_builder_init(&o, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&o, "{sv}", "handle_token", g_variant_new_string("t3"));
  if (!call("Start", g_variant_new("(osa{sv})", session, "", &o), "t3"))
    return 1;
  GVariant *streams =
      g_variant_lookup_value(response_results, "streams", G_VARIANT_TYPE("a(ua{sv})"));
  guint32 node = 0;
  GVariant *props = NULL;
  g_variant_get_child(streams, 0, "(u@a{sv})", &node, &props);
  gint32 w = 0, h = 0;
  g_variant_lookup(props, "size", "(ii)", &w, &h);
  printf("stream: node %u, %dx%d\n", node, w, h);

  GUnixFDList *fds = NULL;
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_with_unix_fd_list_sync(
      bus, PORTAL, PORTAL_PATH, SCREENCAST, "OpenPipeWireRemote",
      g_variant_new("(oa{sv})", session, NULL), G_VARIANT_TYPE("(h)"),
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &fds, NULL, &error);
  if (!reply) {
    printf("OpenPipeWireRemote: %s\n", error->message);
    return 1;
  }
  gint32 index = 0;
  g_variant_get(reply, "(h)", &index);
  int fd = g_unix_fd_list_get(fds, index, NULL);
  printf("pipewire remote: fd %d\n", fd);

  pw_init(&argc, &argv);
  ploop = pw_main_loop_new(NULL);
  struct pw_context *context =
      pw_context_new(pw_main_loop_get_loop(ploop), NULL, 0);
  struct pw_core *core = pw_context_connect_fd(context, fd, NULL, 0);
  if (!core) {
    printf("cannot connect to PipeWire\n");
    return 1;
  }
  stream = pw_stream_new(core, "screencast-test",
                         pw_properties_new(PW_KEY_MEDIA_TYPE, "Video",
                                           PW_KEY_MEDIA_CATEGORY, "Capture",
                                           PW_KEY_MEDIA_ROLE, "Screen", NULL));
  struct spa_hook listener;
  pw_stream_add_listener(stream, &listener, &events, NULL);
  uint8_t buffer[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  struct spa_rectangle def = SPA_RECTANGLE(320, 240), min = SPA_RECTANGLE(1, 1),
                       max = SPA_RECTANGLE(8192, 8192);
  const struct spa_pod *params[1] = {spa_pod_builder_add_object(
      &b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat, SPA_FORMAT_mediaType,
      SPA_POD_Id(SPA_MEDIA_TYPE_video), SPA_FORMAT_mediaSubtype,
      SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), SPA_FORMAT_VIDEO_format,
      SPA_POD_CHOICE_ENUM_Id(3, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx,
                             SPA_VIDEO_FORMAT_BGRA),
      SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&def, &min, &max))};
  pw_stream_connect(stream, PW_DIRECTION_INPUT, node,
                    PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS,
                    params, 1);
  struct spa_source *timer =
      pw_loop_add_timer(pw_main_loop_get_loop(ploop), on_timeout, NULL);
  struct timespec twenty = {20, 0};
  pw_loop_update_timer(pw_main_loop_get_loop(ploop), timer, &twenty, NULL,
                       false);
  pw_main_loop_run(ploop);
  printf("frames=%d size=%ux%u lit=%.0f%%\n", frames, format.size.width,
         format.size.height, lit);
  return frames > 0 ? 0 : 1;
}
