// Distributed under the terms of the GNU General Public License v2
//
// ScreenCast (org.freedesktop.impl.portal.ScreenCast) for
// xdg-desktop-portal-nickel: apps (OBS, browsers, Discord, AnyDesk) get the
// screen or a window as a PipeWire video stream.
//
// CreateSession and SelectSources are remembered (one stream per session).
// Start asks Chrome (VmApplicationsService.NickelScreenCastChoose, Chrome
// patch 0022), which shows ChromeOS's own "share your screen" picker with the
// app's name; then Chrome copies the chosen screen or window, at most 30
// times a second, into shared memory this backend hands it
// (NickelScreenCastStart: a memfd of kSlots frames, BGRx, and a pipe on which
// it writes the slot of each new frame), and shows its "sharing your screen"
// notification. Each frame goes out as a buffer of a PipeWire stream this
// backend publishes (Video/Source, BGRx, the source's size); Start answers
// with the stream's node id, which the app opens through the portal
// (OpenPipeWireRemote gives it access to that node only).
//
// The session's PipeWire daemon runs without a session manager
// (nickel-pipewire), so this backend links the app's stream to its own when
// it appears: portal apps name the node as their target (node.target or
// target.object). The session ends when the app closes it or when the user
// stops sharing in ChromeOS's notification (Chrome closes the pipe); the
// Session's Closed signal then tells the app.
//
// RemoteDesktop (org.freedesktop.impl.portal.RemoteDesktop) shares the
// sessions: remote desktop apps (AnyDesk, RustDesk) select devices and,
// through ScreenCast's SelectSources on the same session, a screen. Start
// then shares the screen as above and asks the user whether the app may
// control the keyboard and pointer (VmApplicationsService
// .NickelRemoteDesktopAsk, Chrome patch 0023), which answers with a token;
// the Notify* calls go to Chrome as input events with it
// (NickelRemoteDesktopInput). Keysyms become evdev keycodes through the
// keyboard map (xkbcommon), with Shift when the symbol needs it. When the
// user stops the control in ChromeOS's notification, Chrome refuses the
// next event and the session closes.

#include "screencast.h"

#include <chromeos/dbus/vm_applications/dbus-constants.h>
#include <fcntl.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <glib-unix.h>
#include <pipewire/pipewire.h>
#include <spa/param/buffers.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include <atomic>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr char kSessionInterface[] = "org.freedesktop.impl.portal.Session";
// Frames in the shared memory Chrome writes to.
constexpr guint32 kSlots = 3;
// Portal source types and cursor modes.
constexpr guint32 kMonitor = 1;
constexpr guint32 kWindow = 2;
constexpr guint32 kCursorHidden = 1;
constexpr guint32 kCursorEmbedded = 2;
// RemoteDesktop device types.
constexpr guint32 kKeyboard = 1;
constexpr guint32 kPointer = 2;
// evdev KEY_LEFTSHIFT.
constexpr guint32 kLeftShift = 42;
// Portal responses.
constexpr guint32 kSuccess = 0;
constexpr guint32 kCanceled = 1;
constexpr guint32 kFailed = 2;

}  // namespace

struct CastSession;
struct BoundNode;

struct ScreenCast {
  GDBusConnection* session = nullptr;
  GDBusConnection* system = nullptr;
  GDBusInterfaceInfo* session_info = nullptr;
  std::map<std::string, CastSession*> sessions;

  // PipeWire, connected on first use (the daemon may start after us).
  pw_thread_loop* loop = nullptr;
  pw_context* context = nullptr;
  pw_core* core = nullptr;
  pw_registry* registry = nullptr;
  spa_hook registry_listener = {};
  // Every node, bound to read its properties: the registry shows only a
  // few, and an app's target is not among them.
  std::map<uint32_t, BoundNode*> nodes;
};

struct BoundNode {
  ScreenCast* cast = nullptr;
  uint32_t id = 0;
  pw_proxy* proxy = nullptr;
  spa_hook listener = {};
};

struct CastSession {
  ScreenCast* cast = nullptr;
  std::string handle;
  std::string app_id;
  guint32 types = kMonitor;
  guint32 cursor_mode = kCursorEmbedded;
  guint object = 0;
  bool started = false;
  // RemoteDesktop: the devices the app wants, whether it also selected a
  // screen (ScreenCast sessions always do), and the token Chrome gave when
  // the user allowed control.
  bool remote = false;
  bool wants_screen = true;
  guint32 devices = kKeyboard | kPointer;
  std::string control_token;
  // Clipboard (asked for before Start): this computer's clipboard and the
  // other one's, through Chrome (NickelClipboard*): watched once a second,
  // and one transfer from the app at a time.
  bool clipboard = false;
  guint clipboard_watch = 0;
  bool clipboard_asking = false;
  std::string clipboard_state;
  bool own_change = false;
  guint32 next_serial = 1;
  struct ClipboardTransfer* transfer = nullptr;

  // While sharing.
  GDBusMethodInvocation* start = nullptr;
  std::string source_id;
  guint32 source_type = kMonitor;
  int width = 0;
  int height = 0;
  guint8* ring = nullptr;
  size_t ring_size = 0;
  int notify_fd = -1;
  guint notify_watch = 0;
  std::atomic<int> latest_slot{-1};

  // PipeWire objects, used with the thread loop locked.
  pw_stream* stream = nullptr;
  spa_hook stream_listener = {};
  uint32_t node_id = SPA_ID_INVALID;
  std::string node_serial;
  std::vector<pw_proxy*> links;
  std::set<uint32_t> linked;
};

namespace {

GVariant* EmptyResults() {
  return g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0);
}

void Reply(GDBusMethodInvocation* invocation, guint32 response,
           GVariant* results) {
  g_dbus_method_invocation_return_value(
      invocation,
      g_variant_new("(u@a{sv})", response, results ? results : EmptyResults()));
}

// The name the user knows the app by, for Chrome's prompt.
std::string AppName(const std::string& app_id) {
  if (!app_id.empty()) {
    std::string desktop_id = app_id + ".desktop";
    g_autoptr(GDesktopAppInfo) info =
        g_desktop_app_info_new(desktop_id.c_str());
    if (info) {
      return g_app_info_get_display_name(G_APP_INFO(info));
    }
    return app_id;
  }
  return "";
}

// --- PipeWire -------------------------------------------------------------

void LinkIfTarget(CastSession* s, uint32_t id, const spa_dict* props) {
  if (s->node_id == SPA_ID_INVALID || id == s->node_id) {
    return;
  }
  const char* target = spa_dict_lookup(props, PW_KEY_TARGET_OBJECT);
  if (!target) {
    // Deprecated, but what the portal's apps set (pw_stream_connect).
    target = spa_dict_lookup(props, "node.target");
  }
  if (!target) {
    return;
  }
  std::string own_id = std::to_string(s->node_id);
  if (target != own_id && (s->node_serial.empty() || target != s->node_serial)) {
    return;
  }
  if (!s->linked.insert(id).second) {
    return;
  }
  pw_properties* link_props = pw_properties_new(nullptr, nullptr);
  pw_properties_setf(link_props, PW_KEY_LINK_OUTPUT_NODE, "%u", s->node_id);
  pw_properties_setf(link_props, PW_KEY_LINK_INPUT_NODE, "%u", id);
  pw_proxy* link = static_cast<pw_proxy*>(pw_core_create_object(
      s->cast->core, "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK,
      &link_props->dict, 0));
  pw_properties_free(link_props);
  if (link) {
    s->links.push_back(link);
    syslog(LOG_INFO, "screencast: linked node %u to %u", s->node_id, id);
  }
}

// A node's full properties: link it if it is an app's stream that names
// one of ours as its target.
void OnNodeInfo(void* data, const pw_node_info* info) {
  BoundNode* node = static_cast<BoundNode*>(data);
  if (!info->props || !(info->change_mask & PW_NODE_CHANGE_MASK_PROPS)) {
    return;
  }
  for (auto& [handle, s] : node->cast->sessions) {
    if (node->id == s->node_id) {
      if (const char* serial =
              spa_dict_lookup(info->props, PW_KEY_OBJECT_SERIAL)) {
        s->node_serial = serial;
      }
    } else {
      LinkIfTarget(s, node->id, info->props);
    }
  }
}

const pw_node_events kNodeEvents = {
    .version = PW_VERSION_NODE_EVENTS,
    .info = OnNodeInfo,
};

void OnRegistryGlobal(void* data,
                      uint32_t id,
                      uint32_t,
                      const char* type,
                      uint32_t,
                      const spa_dict*) {
  ScreenCast* cast = static_cast<ScreenCast*>(data);
  if (strcmp(type, PW_TYPE_INTERFACE_Node) != 0 || cast->nodes.count(id)) {
    return;
  }
  BoundNode* node = new BoundNode;
  node->cast = cast;
  node->id = id;
  node->proxy = static_cast<pw_proxy*>(pw_registry_bind(
      cast->registry, id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
  if (!node->proxy) {
    delete node;
    return;
  }
  pw_proxy_add_object_listener(node->proxy, &node->listener, &kNodeEvents,
                               node);
  cast->nodes[id] = node;
}

void OnRegistryGlobalRemove(void* data, uint32_t id) {
  ScreenCast* cast = static_cast<ScreenCast*>(data);
  auto it = cast->nodes.find(id);
  if (it == cast->nodes.end()) {
    return;
  }
  spa_hook_remove(&it->second->listener);
  pw_proxy_destroy(it->second->proxy);
  delete it->second;
  cast->nodes.erase(it);
}

const pw_registry_events kRegistryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = OnRegistryGlobal,
    .global_remove = OnRegistryGlobalRemove,
};

bool ConnectPipeWire(ScreenCast* cast) {
  if (cast->core) {
    return true;
  }
  if (!cast->loop) {
    pw_init(nullptr, nullptr);
    cast->loop = pw_thread_loop_new("nickel-screencast", nullptr);
    cast->context = pw_context_new(pw_thread_loop_get_loop(cast->loop),
                                   nullptr, 0);
    pw_thread_loop_start(cast->loop);
  }
  pw_thread_loop_lock(cast->loop);
  cast->core = pw_context_connect(cast->context, nullptr, 0);
  if (cast->core) {
    cast->registry =
        pw_core_get_registry(cast->core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(cast->registry, &cast->registry_listener,
                             &kRegistryEvents, cast);
  }
  pw_thread_loop_unlock(cast->loop);
  if (!cast->core) {
    syslog(LOG_ERR, "screencast: cannot connect to PipeWire");
    return false;
  }
  return true;
}

// Called on the main context once the stream has a node (or failed).
struct StartDone {
  ScreenCast* cast;
  std::string handle;
  bool ok;
};

void FinishStart(CastSession* s, bool ok);

gboolean OnStartDone(gpointer data) {
  StartDone* done = static_cast<StartDone*>(data);
  auto it = done->cast->sessions.find(done->handle);
  if (it != done->cast->sessions.end()) {
    FinishStart(it->second, done->ok);
  }
  delete done;
  return G_SOURCE_REMOVE;
}

void OnStreamStateChanged(void* data,
                          pw_stream_state,
                          pw_stream_state state,
                          const char* error) {
  CastSession* s = static_cast<CastSession*>(data);
  if (state == PW_STREAM_STATE_ERROR) {
    syslog(LOG_ERR, "screencast: stream error: %s", error ? error : "");
  }
  if ((state == PW_STREAM_STATE_PAUSED || state == PW_STREAM_STATE_ERROR) &&
      s->start && s->node_id == SPA_ID_INVALID) {
    if (state == PW_STREAM_STATE_PAUSED) {
      s->node_id = pw_stream_get_node_id(s->stream);
    }
    g_main_context_invoke(
        nullptr, OnStartDone,
        new StartDone{s->cast, s->handle, state == PW_STREAM_STATE_PAUSED});
  }
}

void OnStreamParamChanged(void* data, uint32_t id, const spa_pod* param) {
  CastSession* s = static_cast<CastSession*>(data);
  if (!param || id != SPA_PARAM_Format) {
    return;
  }
  uint8_t buffer[1024];
  spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  const int stride = s->width * 4;
  const spa_pod* params[1];
  params[0] = static_cast<const spa_pod*>(spa_pod_builder_add_object(
      &b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
      SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(4, 2, 8),
      SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1), SPA_PARAM_BUFFERS_size,
      SPA_POD_Int(stride * s->height), SPA_PARAM_BUFFERS_stride,
      SPA_POD_Int(stride), SPA_PARAM_BUFFERS_dataType,
      SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemFd) |
                               (1 << SPA_DATA_MemPtr))));
  pw_stream_update_params(s->stream, params, 1);
}

// A graph cycle: send the newest frame Chrome wrote.
void OnStreamProcess(void* data) {
  CastSession* s = static_cast<CastSession*>(data);
  const int slot = s->latest_slot.load();
  if (slot < 0) {
    return;
  }
  pw_buffer* buffer = pw_stream_dequeue_buffer(s->stream);
  if (!buffer) {
    return;
  }
  spa_data* d = &buffer->buffer->datas[0];
  const size_t frame = static_cast<size_t>(s->width) * s->height * 4;

  if (d->data && d->maxsize >= frame) {
    memcpy(d->data, s->ring + slot * frame, frame);
    d->chunk->offset = 0;
    d->chunk->size = frame;
    d->chunk->stride = s->width * 4;
  } else if (d->chunk) {
    d->chunk->size = 0;
  }
  pw_stream_queue_buffer(s->stream, buffer);
}

const pw_stream_events kStreamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = OnStreamStateChanged,
    .param_changed = OnStreamParamChanged,
    .process = OnStreamProcess,
};

bool CreateStream(CastSession* s) {
  ScreenCast* cast = s->cast;
  pw_thread_loop_lock(cast->loop);
  s->stream = pw_stream_new(
      cast->core, "nickel-screencast",
      pw_properties_new(PW_KEY_MEDIA_CLASS, "Video/Source", PW_KEY_MEDIA_ROLE,
                        "Screen", PW_KEY_NODE_DESCRIPTION,
                        "ChromeOS screen", nullptr));
  pw_stream_add_listener(s->stream, &s->stream_listener, &kStreamEvents, s);
  uint8_t buffer[1024];
  spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  spa_rectangle size = SPA_RECTANGLE(static_cast<uint32_t>(s->width),
                                     static_cast<uint32_t>(s->height));
  spa_fraction rate = SPA_FRACTION(0, 1);
  spa_fraction max_rate = SPA_FRACTION(30, 1);
  const spa_pod* params[1];
  params[0] = static_cast<const spa_pod*>(spa_pod_builder_add_object(
      &b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat, SPA_FORMAT_mediaType,
      SPA_POD_Id(SPA_MEDIA_TYPE_video), SPA_FORMAT_mediaSubtype,
      SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), SPA_FORMAT_VIDEO_format,
      SPA_POD_CHOICE_ENUM_Id(3, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx,
                             SPA_VIDEO_FORMAT_BGRA),
      SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&size),
      SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&rate),
      SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_Fraction(&max_rate)));
  int r = pw_stream_connect(
      s->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
      // PipeWire allocates the buffers (shared memory) and maps them.
      static_cast<pw_stream_flags>(PW_STREAM_FLAG_DRIVER |
                                   PW_STREAM_FLAG_MAP_BUFFERS),
      params, 1);
  pw_thread_loop_unlock(cast->loop);
  if (r < 0) {
    syslog(LOG_ERR, "screencast: cannot connect the stream: %s",
           strerror(-r));
    return false;
  }
  return true;
}

// --- Session lifetime ------------------------------------------------------

void CloseSession(CastSession* s, bool tell_app);
void StartClipboardWatch(CastSession* s);
void StopClipboard(CastSession* s);

// Chrome wrote frames (the slot of each), or closed the pipe: the user
// stopped sharing.
gboolean OnFrames(gint fd, GIOCondition condition, gpointer data) {
  CastSession* s = static_cast<CastSession*>(data);
  guint32 slots[16];
  ssize_t n = read(fd, slots, sizeof(slots));
  if (n > 0) {
    guint32 slot = slots[n / sizeof(guint32) - 1];
    if (s->latest_slot.load() < 0) {
      syslog(LOG_INFO, "screencast: first frame from Chrome");
    }
    if (slot < kSlots) {
      s->latest_slot = static_cast<int>(slot);
      pw_thread_loop_lock(s->cast->loop);
      if (s->stream) {
        pw_stream_trigger_process(s->stream);
      }
      pw_thread_loop_unlock(s->cast->loop);
    }
    return G_SOURCE_CONTINUE;
  }
  if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
    return G_SOURCE_CONTINUE;
  }
  // End of file: Chrome stopped.
  s->notify_watch = 0;
  CloseSession(s, true);
  return G_SOURCE_REMOVE;
}

void CloseSession(CastSession* s, bool tell_app) {
  ScreenCast* cast = s->cast;
  StopClipboard(s);
  if (s->start) {
    Reply(s->start, kFailed, nullptr);
    s->start = nullptr;
  }
  if (s->notify_watch) {
    g_source_remove(s->notify_watch);
    s->notify_watch = 0;
  }
  if (s->notify_fd >= 0) {
    close(s->notify_fd);
    s->notify_fd = -1;
  }
  if (cast->loop && (s->stream || !s->links.empty())) {
    pw_thread_loop_lock(cast->loop);
    for (pw_proxy* link : s->links) {
      pw_proxy_destroy(link);
    }
    s->links.clear();
    if (s->stream) {
      pw_stream_destroy(s->stream);
      s->stream = nullptr;
    }
    pw_thread_loop_unlock(cast->loop);
  }
  if (s->ring) {
    munmap(s->ring, s->ring_size);
    s->ring = nullptr;
  }
  if (!s->control_token.empty() && cast->system) {
    g_dbus_connection_call(
        cast->system, vm_tools::apps::kVmApplicationsServiceName,
        vm_tools::apps::kVmApplicationsServicePath,
        vm_tools::apps::kVmApplicationsServiceInterface,
        "NickelRemoteDesktopInput",
        g_variant_new("(sssddub)", s->control_token.c_str(), "end", "", 0.0,
                      0.0, 0u, FALSE),
        nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
  }
  if (!s->source_id.empty() && cast->system) {
    // Chrome stops on its own when the pipe closes; this also takes its
    // notification down right away.
    g_dbus_connection_call(
        cast->system, vm_tools::apps::kVmApplicationsServiceName,
        vm_tools::apps::kVmApplicationsServicePath,
        vm_tools::apps::kVmApplicationsServiceInterface,
        "NickelScreenCastStop", g_variant_new("(s)", s->source_id.c_str()),
        nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
  }
  if (tell_app) {
    g_dbus_connection_emit_signal(cast->session, nullptr, s->handle.c_str(),
                                  kSessionInterface, "Closed", nullptr,
                                  nullptr);
  }
  if (s->object) {
    g_dbus_connection_unregister_object(cast->session, s->object);
  }
  // PipeWire's thread reads the sessions (registry events).
  if (cast->loop) {
    pw_thread_loop_lock(cast->loop);
  }
  cast->sessions.erase(s->handle);
  if (cast->loop) {
    pw_thread_loop_unlock(cast->loop);
  }
  delete s;
}

void OnSessionMethodCall(GDBusConnection*,
                         const gchar*,
                         const gchar* object_path,
                         const gchar*,
                         const gchar* method_name,
                         GVariant*,
                         GDBusMethodInvocation* invocation,
                         gpointer user_data) {
  ScreenCast* cast = static_cast<ScreenCast*>(user_data);
  auto it = cast->sessions.find(object_path);
  g_dbus_method_invocation_return_value(invocation, nullptr);
  if (it != cast->sessions.end() && g_strcmp0(method_name, "Close") == 0) {
    CloseSession(it->second, false);
  }
}

GVariant* OnSessionGetProperty(GDBusConnection*,
                               const gchar*,
                               const gchar*,
                               const gchar*,
                               const gchar* property_name,
                               GError** error,
                               gpointer) {
  if (g_strcmp0(property_name, "version") == 0) {
    return g_variant_new_uint32(1);
  }
  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
              "unknown property %s", property_name);
  return nullptr;
}

void AskControl(CastSession* s, GVariant* streams);

void FinishStart(CastSession* s, bool ok) {
  if (!s->start) {
    return;
  }
  if (!ok || s->node_id == SPA_ID_INVALID) {
    GDBusMethodInvocation* invocation = s->start;
    s->start = nullptr;
    Reply(invocation, kFailed, nullptr);
    CloseSession(s, true);
    return;
  }
  GVariantBuilder props;
  g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&props, "{sv}", "source_type",
                        g_variant_new_uint32(s->source_type));
  g_variant_builder_add(&props, "{sv}", "size",
                        g_variant_new("(ii)", s->width, s->height));
  g_variant_builder_add(&props, "{sv}", "position", g_variant_new("(ii)", 0, 0));
  GVariantBuilder streams;
  g_variant_builder_init(&streams, G_VARIANT_TYPE("a(ua{sv})"));
  g_variant_builder_add(&streams, "(ua{sv})", s->node_id, &props);
  syslog(LOG_INFO, "screencast: %s shares %s as node %u (%dx%d)",
         s->app_id.c_str(), s->source_id.c_str(), s->node_id, s->width,
         s->height);
  GVariant* stream_list = g_variant_builder_end(&streams);
  if (s->remote) {
    AskControl(s, stream_list);
    return;
  }
  GDBusMethodInvocation* invocation = s->start;
  s->start = nullptr;
  GVariantBuilder results;
  g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&results, "{sv}", "streams", stream_list);
  Reply(invocation, kSuccess, g_variant_builder_end(&results));
}

// --- RemoteDesktop ---------------------------------------------------------

struct AskCall {
  ScreenCast* cast;
  std::string handle;
  GVariant* streams;  // Owned, or nullptr without a shared screen.
};

void OnControlAnswered(GObject* source, GAsyncResult* result, gpointer data) {
  AskCall* call = static_cast<AskCall*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  auto it = call->cast->sessions.find(call->handle);
  CastSession* s = it == call->cast->sessions.end() ? nullptr : it->second;
  const gchar* token = "";
  if (reply) {
    g_variant_get(reply, "(&s)", &token);
  } else {
    syslog(LOG_WARNING, "NickelRemoteDesktopAsk: %s", error->message);
  }
  if (s && s->start) {
    GDBusMethodInvocation* invocation = s->start;
    s->start = nullptr;
    if (!*token) {
      Reply(invocation, reply ? kCanceled : kFailed, nullptr);
      CloseSession(s, true);
    } else {
      s->control_token = token;
      GVariantBuilder results;
      g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&results, "{sv}", "devices",
                            g_variant_new_uint32(s->devices));
      if (s->clipboard) {
        g_variant_builder_add(&results, "{sv}", "clipboard_enabled",
                              g_variant_new_boolean(TRUE));
        StartClipboardWatch(s);
      }
      if (call->streams) {
        g_variant_builder_add(&results, "{sv}", "streams", call->streams);
        call->streams = nullptr;
      }
      syslog(LOG_INFO, "remote desktop: %s may control the device",
             s->app_id.c_str());
      Reply(invocation, kSuccess, g_variant_builder_end(&results));
    }
  }
  if (call->streams) {
    g_variant_unref(g_variant_ref_sink(call->streams));
  }
  delete call;
}

// Asks the user whether the app may control the keyboard and pointer;
// answers Start then.
void AskControl(CastSession* s, GVariant* streams) {
  std::string name = AppName(s->app_id);
  g_dbus_connection_call(
      s->cast->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface,
      "NickelRemoteDesktopAsk", g_variant_new("(s)", name.c_str()),
      G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, G_MAXINT, nullptr,
      OnControlAnswered, new AskCall{s->cast, s->handle, streams});
}

struct InputCall {
  ScreenCast* cast;
  std::string handle;
};

void OnInputSent(GObject* source, GAsyncResult* result, gpointer data) {
  InputCall* call = static_cast<InputCall*>(data);
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, nullptr);
  gboolean ok = FALSE;
  if (reply) {
    g_variant_get(reply, "(b)", &ok);
  }
  auto it = call->cast->sessions.find(call->handle);
  if (!ok && it != call->cast->sessions.end() &&
      !it->second->control_token.empty()) {
    // The user stopped the control.
    it->second->control_token.clear();
    CloseSession(it->second, true);
  }
  delete call;
}

// One input event to Chrome. Calls on one connection arrive in order.
void SendInput(CastSession* s,
               const char* kind,
               double x,
               double y,
               guint32 code,
               bool down) {
  if (s->control_token.empty()) {
    return;
  }
  g_dbus_connection_call(
      s->cast->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface,
      "NickelRemoteDesktopInput",
      g_variant_new("(sssddub)", s->control_token.c_str(), kind,
                    s->source_id.c_str(), x, y, code, down),
      G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, OnInputSent,
      new InputCall{s->cast, s->handle});
}

// The evdev keycode that types |keysym| on the default keyboard map, and
// whether it needs Shift.
bool KeysymToKeycode(guint32 keysym, guint32* keycode, bool* shift) {
  static xkb_keymap* keymap = [] {
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_rule_names names = {};
    return context ? xkb_keymap_new_from_names(context, &names,
                                               XKB_KEYMAP_COMPILE_NO_FLAGS)
                   : nullptr;
  }();
  if (!keymap) {
    return false;
  }
  for (xkb_keycode_t key = xkb_keymap_min_keycode(keymap);
       key <= xkb_keymap_max_keycode(keymap); ++key) {
    const xkb_level_index_t levels =
        xkb_keymap_num_levels_for_key(keymap, key, 0);
    for (xkb_level_index_t level = 0; level < levels && level < 2; ++level) {
      const xkb_keysym_t* syms = nullptr;
      int n = xkb_keymap_key_get_syms_by_level(keymap, key, 0, level, &syms);
      for (int i = 0; i < n; ++i) {
        if (syms[i] == keysym) {
          *keycode = key - 8;
          *shift = level == 1;
          return true;
        }
      }
    }
  }
  return false;
}

CastSession* SessionFor(ScreenCast* cast, const gchar* handle) {
  auto it = cast->sessions.find(handle);
  return it == cast->sessions.end() ? nullptr : it->second;
}


// --- Clipboard -------------------------------------------------------------
//
// The Clipboard portal of remote desktop sessions (RequestClipboard before
// Start): what the other computer copies reaches this one's clipboard, and
// the other way round, through Chrome (VmApplicationsService.
// NickelClipboardFormats, NickelClipboardRead and NickelClipboardWrite,
// Chrome patch 0068), with the session's control token: only while the user
// lets the app control the device. ChromeOS's clipboard can't be filled
// lazily, so when the app announces a selection (SetSelection) its data is
// asked for at once (SelectionTransfer) and put on the clipboard once the
// app has written it; this computer's clipboard is watched once a second
// and the app told when it changes (SelectionOwnerChanged). Text and HTML go
// both ways, pictures (PNG) only to the app.

constexpr char kPortalObjectPath[] = "/org/freedesktop/portal/desktop";
constexpr char kClipboardInterface[] = "org.freedesktop.impl.portal.Clipboard";
// Larger selections are not put on the clipboard.
constexpr gsize kClipboardMax = 32 * 1024 * 1024;
// What an app's selection is asked for in, best first.
constexpr const char* kSelectionTypes[] = {"text/plain;charset=utf-8",
                                           "UTF8_STRING", "text/plain",
                                           "STRING", "TEXT", "text/html"};

}  // namespace

struct ClipboardTransfer {
  ScreenCast* cast = nullptr;
  std::string handle;
  guint32 serial = 0;
  std::string mime_type;
  GOutputStream* buffer = nullptr;  // A GMemoryOutputStream, once writing.
  bool read_done = false;
  bool read_ok = false;
  bool write_done = false;
  bool success = false;
};

namespace {

struct SessionRef {
  ScreenCast* cast;
  std::string handle;
};

void EmitClipboard(CastSession* s, const char* signal, GVariant* args) {
  g_dbus_connection_emit_signal(s->cast->session, nullptr, kPortalObjectPath,
                                kClipboardInterface, signal, args, nullptr);
}

void OnClipboardWritten(GObject* source, GAsyncResult* result, gpointer data) {
  SessionRef* ref = static_cast<SessionRef*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  gboolean done = FALSE;
  if (reply) {
    g_variant_get(reply, "(b)", &done);
  } else {
    syslog(LOG_WARNING, "clipboard: NickelClipboardWrite: %s", error->message);
  }
  CastSession* s = SessionFor(ref->cast, ref->handle.c_str());
  if (s && done) {
    // The change the watch sees next is the app's own.
    s->own_change = true;
  } else if (reply && !done) {
    syslog(LOG_WARNING, "clipboard: Chrome did not take the selection");
  }
  delete ref;
}

// Once the app has written the selection and said it is done: onto the
// clipboard.
void FinishTransfer(ClipboardTransfer* t) {
  if (!t->read_done || !t->write_done) {
    return;
  }
  CastSession* s = SessionFor(t->cast, t->handle.c_str());
  if (s && s->transfer == t) {
    s->transfer = nullptr;
  }
  if (s && t->success && t->read_ok && !s->control_token.empty()) {
    g_autoptr(GBytes) bytes = g_memory_output_stream_steal_as_bytes(
        G_MEMORY_OUTPUT_STREAM(t->buffer));
    gsize size = 0;
    const void* data = g_bytes_get_data(bytes, &size);
    if (size > 0 && size <= kClipboardMax) {
      g_dbus_connection_call(
          t->cast->system, vm_tools::apps::kVmApplicationsServiceName,
          vm_tools::apps::kVmApplicationsServicePath,
          vm_tools::apps::kVmApplicationsServiceInterface,
          "NickelClipboardWrite",
          g_variant_new("(ss@ay)", s->control_token.c_str(),
                        t->mime_type.c_str(),
                        g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, data,
                                                  size, 1)),
          G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
          OnClipboardWritten, new SessionRef{t->cast, t->handle});
    }
  }
  g_clear_object(&t->buffer);
  delete t;
}

void OnSelectionSpliced(GObject* source, GAsyncResult* result, gpointer data) {
  ClipboardTransfer* t = static_cast<ClipboardTransfer*>(data);
  g_autoptr(GError) error = nullptr;
  const gssize length =
      g_output_stream_splice_finish(G_OUTPUT_STREAM(source), result, &error);
  t->read_ok = length >= 0;
  t->read_done = true;
  FinishTransfer(t);
}

struct ReadCall {
  int fd;
  GBytes* data = nullptr;
  GOutputStream* out = nullptr;
};

void OnSelectionSent(GObject* source, GAsyncResult* result, gpointer data) {
  ReadCall* call = static_cast<ReadCall*>(data);
  g_output_stream_write_all_finish(G_OUTPUT_STREAM(source), result, nullptr,
                                   nullptr);
  g_object_unref(call->out);  // Closes the pipe: the app reads to the end.
  g_bytes_unref(call->data);
  delete call;
}

void OnClipboardRead(GObject* source, GAsyncResult* result, gpointer data) {
  ReadCall* call = static_cast<ReadCall*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  gboolean ok = FALSE;
  g_autoptr(GVariant) bytes = nullptr;
  if (reply) {
    g_variant_get(reply, "(b@ay)", &ok, &bytes);
  } else {
    syslog(LOG_WARNING, "clipboard: NickelClipboardRead: %s", error->message);
  }
  gsize size = 0;
  const void* contents =
      ok ? g_variant_get_fixed_array(bytes, &size, 1) : nullptr;
  if (!ok || size == 0) {
    close(call->fd);
    delete call;
    return;
  }
  call->data = g_bytes_new(contents, size);
  call->out = g_unix_output_stream_new(call->fd, TRUE);
  g_output_stream_write_all_async(call->out, g_bytes_get_data(call->data, nullptr),
                                  size, G_PRIORITY_DEFAULT, nullptr,
                                  OnSelectionSent, call);
}

void OnClipboardFormats(GObject* source, GAsyncResult* result, gpointer data) {
  SessionRef* ref = static_cast<SessionRef*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  CastSession* s = SessionFor(ref->cast, ref->handle.c_str());
  delete ref;
  if (!s) {
    return;
  }
  s->clipboard_asking = false;
  if (!reply) {
    syslog(LOG_WARNING, "clipboard: NickelClipboardFormats: %s",
           error->message);
    return;
  }
  const gchar* state = nullptr;
  g_autoptr(GVariant) types = nullptr;
  g_variant_get(reply, "(&s@as)", &state, &types);
  if (!*state || s->clipboard_state == state) {
    return;
  }
  const bool first = s->clipboard_state.empty();
  s->clipboard_state = state;
  if (first && !s->own_change && g_variant_n_children(types) == 0) {
    return;
  }
  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&options, "{sv}", "mime_types", types);
  g_variant_builder_add(&options, "{sv}", "session_is_owner",
                        g_variant_new_boolean(s->own_change));
  s->own_change = false;
  EmitClipboard(s, "SelectionOwnerChanged",
                g_variant_new("(oa{sv})", s->handle.c_str(), &options));
}

gboolean OnClipboardWatch(gpointer data) {
  SessionRef* ref = static_cast<SessionRef*>(data);
  CastSession* s = SessionFor(ref->cast, ref->handle.c_str());
  if (!s) {
    return G_SOURCE_REMOVE;
  }
  if (s->clipboard_asking || s->control_token.empty()) {
    return G_SOURCE_CONTINUE;
  }
  s->clipboard_asking = true;
  g_dbus_connection_call(
      s->cast->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, "NickelClipboardFormats",
      g_variant_new("(s)", s->control_token.c_str()), G_VARIANT_TYPE("(sas)"),
      G_DBUS_CALL_FLAGS_NONE, -1, nullptr, OnClipboardFormats,
      new SessionRef{ref->cast, ref->handle});
  return G_SOURCE_CONTINUE;
}

void StartClipboardWatch(CastSession* s) {
  if (s->clipboard_watch) {
    return;
  }
  s->clipboard_watch = g_timeout_add_seconds_full(
      G_PRIORITY_DEFAULT, 1, OnClipboardWatch,
      new SessionRef{s->cast, s->handle},
      [](gpointer data) { delete static_cast<SessionRef*>(data); });
}

void StopClipboard(CastSession* s) {
  if (s->clipboard_watch) {
    g_source_remove(s->clipboard_watch);
    s->clipboard_watch = 0;
  }
  if (s->transfer && !s->transfer->buffer) {
    // Never written: nothing else holds it. One being written finishes
    // without the session.
    delete s->transfer;
  }
  s->transfer = nullptr;
}

// Returns |fd| to the caller as the method's (h) answer; it is ours to give.
void ReplyFd(GDBusMethodInvocation* invocation, int fd) {
  g_autoptr(GUnixFDList) list = g_unix_fd_list_new_from_array(&fd, 1);
  g_dbus_method_invocation_return_value_with_unix_fd_list(
      invocation, g_variant_new("(h)", 0), list);
}

void Notify(ScreenCast* cast,
            const gchar* method_name,
            GVariant* parameters) {
  const gchar* handle = nullptr;
  g_variant_get_child(parameters, 0, "&o", &handle);
  CastSession* s = SessionFor(cast, handle);
  if (!s || !s->remote) {
    return;
  }
  if (g_strcmp0(method_name, "NotifyPointerMotion") == 0) {
    double dx = 0, dy = 0;
    g_variant_get(parameters, "(&o@a{sv}dd)", nullptr, nullptr, &dx, &dy);
    SendInput(s, "move-by", dx, dy, 0, false);
  } else if (g_strcmp0(method_name, "NotifyPointerMotionAbsolute") == 0) {
    guint32 stream = 0;
    double x = 0, y = 0;
    g_variant_get(parameters, "(&o@a{sv}udd)", nullptr, nullptr, &stream, &x,
                  &y);
    SendInput(s, "move-to", x, y, 0, false);
  } else if (g_strcmp0(method_name, "NotifyPointerButton") == 0) {
    gint32 button = 0;
    guint32 state = 0;
    g_variant_get(parameters, "(&o@a{sv}iu)", nullptr, nullptr, &button,
                  &state);
    SendInput(s, "button", 0, 0, button, state == 1);
  } else if (g_strcmp0(method_name, "NotifyPointerAxis") == 0) {
    double dx = 0, dy = 0;
    g_variant_get(parameters, "(&o@a{sv}dd)", nullptr, nullptr, &dx, &dy);
    SendInput(s, "scroll", dx, dy, 0, false);
  } else if (g_strcmp0(method_name, "NotifyPointerAxisDiscrete") == 0) {
    guint32 axis = 0;
    gint32 steps = 0;
    g_variant_get(parameters, "(&o@a{sv}ui)", nullptr, nullptr, &axis, &steps);
    // Axis 0 is vertical, 1 horizontal.
    SendInput(s, "scroll-steps", axis == 1 ? steps : 0, axis == 1 ? 0 : steps,
              0, false);
  } else if (g_strcmp0(method_name, "NotifyKeyboardKeycode") == 0) {
    gint32 keycode = 0;
    guint32 state = 0;
    g_variant_get(parameters, "(&o@a{sv}iu)", nullptr, nullptr, &keycode,
                  &state);
    SendInput(s, "key", 0, 0, keycode, state == 1);
  } else if (g_strcmp0(method_name, "NotifyKeyboardKeysym") == 0) {
    gint32 keysym = 0;
    guint32 state = 0;
    g_variant_get(parameters, "(&o@a{sv}iu)", nullptr, nullptr, &keysym,
                  &state);
    guint32 keycode = 0;
    bool shift = false;
    if (!KeysymToKeycode(keysym, &keycode, &shift)) {
      return;
    }
    const bool down = state == 1;
    if (shift && down) {
      SendInput(s, "key", 0, 0, kLeftShift, true);
    }
    SendInput(s, "key", 0, 0, keycode, down);
    if (shift && !down) {
      SendInput(s, "key", 0, 0, kLeftShift, false);
    }
  }
  // Touch: not offered (AvailableDeviceTypes).
}

// --- Start -----------------------------------------------------------------

struct StartCall {
  ScreenCast* cast;
  std::string handle;
};

CastSession* Lookup(StartCall* call) {
  auto it = call->cast->sessions.find(call->handle);
  return it == call->cast->sessions.end() ? nullptr : it->second;
}

void OnCaptureStarted(GObject* source, GAsyncResult* result, gpointer data) {
  StartCall* call = static_cast<StartCall*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_with_unix_fd_list_finish(
      G_DBUS_CONNECTION(source), nullptr, result, &error);
  CastSession* s = Lookup(call);
  delete call;
  if (!s) {
    return;
  }
  gboolean ok = FALSE;
  if (reply) {
    g_variant_get(reply, "(b)", &ok);
  } else {
    syslog(LOG_WARNING, "NickelScreenCastStart: %s", error->message);
  }
  if (!ok || !CreateStream(s)) {
    FinishStart(s, false);
  }
}

void OnSourceChosen(GObject* source, GAsyncResult* result, gpointer data) {
  StartCall* call = static_cast<StartCall*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  CastSession* s = Lookup(call);
  if (!s) {
    delete call;
    return;
  }
  const gchar* source_id = "";
  gint32 width = 0;
  gint32 height = 0;
  if (reply) {
    g_variant_get(reply, "(&sii)", &source_id, &width, &height);
  } else {
    syslog(LOG_WARNING, "NickelScreenCastChoose: %s", error->message);
  }
  if (!*source_id || width <= 0 || height <= 0) {
    // The user canceled (or Chrome could not ask).
    delete call;
    GDBusMethodInvocation* invocation = s->start;
    s->start = nullptr;
    Reply(invocation, reply ? kCanceled : kFailed, nullptr);
    CloseSession(s, false);
    return;
  }
  s->source_id = source_id;
  s->source_type = g_str_has_prefix(source_id, "window:") ? kWindow : kMonitor;
  s->width = width;
  s->height = height;

  // The frames Chrome writes, and the pipe it tells us about them on.
  s->ring_size = static_cast<size_t>(kSlots) * width * height * 4;
  int ring_fd = memfd_create("nickel-screencast", MFD_CLOEXEC);
  int pipe_fds[2] = {-1, -1};
  if (ring_fd < 0 || ftruncate(ring_fd, s->ring_size) < 0 ||
      pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK) < 0) {
    syslog(LOG_ERR, "screencast: cannot make the frame buffer: %m");
    if (ring_fd >= 0) {
      close(ring_fd);
    }
    delete call;
    FinishStart(s, false);
    return;
  }
  s->ring = static_cast<guint8*>(
      mmap(nullptr, s->ring_size, PROT_READ, MAP_SHARED, ring_fd, 0));
  if (s->ring == MAP_FAILED) {
    s->ring = nullptr;
  }
  s->notify_fd = pipe_fds[0];
  s->notify_watch = g_unix_fd_add(
      s->notify_fd,
      static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), OnFrames, s);

  g_autoptr(GUnixFDList) fds = g_unix_fd_list_new();
  gint ring_index = g_unix_fd_list_append(fds, ring_fd, nullptr);
  gint pipe_index = g_unix_fd_list_append(fds, pipe_fds[1], nullptr);
  close(ring_fd);
  close(pipe_fds[1]);
  if (!s->ring) {
    delete call;
    FinishStart(s, false);
    return;
  }
  g_dbus_connection_call_with_unix_fd_list(
      s->cast->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, "NickelScreenCastStart",
      g_variant_new("(shhuuubs)", s->source_id.c_str(), ring_index,
                    pipe_index, static_cast<guint32>(width),
                    static_cast<guint32>(height), kSlots,
                    s->cursor_mode != kCursorHidden,
                    AppName(s->app_id).c_str()),
      G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, -1, fds, nullptr,
      OnCaptureStarted, call);
}

void Start(ScreenCast* cast,
           GDBusMethodInvocation* invocation,
           GVariant* parameters) {
  const gchar* handle = nullptr;
  const gchar* session_handle = nullptr;
  const gchar* app_id = nullptr;
  const gchar* parent_window = nullptr;
  g_autoptr(GVariant) options = nullptr;
  g_variant_get(parameters, "(&o&o&s&s@a{sv})", &handle, &session_handle,
                &app_id, &parent_window, &options);
  auto it = cast->sessions.find(session_handle);
  if (it == cast->sessions.end() || it->second->started || !cast->system ||
      ((!it->second->remote || it->second->wants_screen) &&
       !ConnectPipeWire(cast))) {
    Reply(invocation, kFailed, nullptr);
    return;
  }
  CastSession* s = it->second;
  s->started = true;
  s->start = invocation;
  if (s->remote && !s->wants_screen) {
    AskControl(s, nullptr);
    return;
  }
  std::string name = AppName(s->app_id);
  // Chrome's picker waits for the user: no timeout.
  g_dbus_connection_call(
      cast->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface,
      "NickelScreenCastChoose",
      g_variant_new("(su)", name.c_str(), s->types),
      G_VARIANT_TYPE("(sii)"), G_DBUS_CALL_FLAGS_NONE, G_MAXINT, nullptr,
      OnSourceChosen, new StartCall{cast, session_handle});
}

void CreateSession(ScreenCast* cast,
                   GDBusMethodInvocation* invocation,
                   GVariant* parameters,
                   bool remote) {
  const gchar* handle = nullptr;
  const gchar* session_handle = nullptr;
  const gchar* app_id = nullptr;
  g_autoptr(GVariant) options = nullptr;
  g_variant_get(parameters, "(&o&o&s@a{sv})", &handle, &session_handle,
                &app_id, &options);
  if (cast->sessions.count(session_handle)) {
    Reply(invocation, kFailed, nullptr);
    return;
  }
  CastSession* s = new CastSession;
  s->cast = cast;
  s->handle = session_handle;
  s->app_id = app_id;
  s->remote = remote;
  // A remote desktop session shares a screen only if the app selects one.
  s->wants_screen = !remote;
  static const GDBusInterfaceVTable kSessionVTable = {
      OnSessionMethodCall, OnSessionGetProperty, nullptr};
  s->object = g_dbus_connection_register_object(
      cast->session, session_handle, cast->session_info, &kSessionVTable,
      cast, nullptr, nullptr);
  if (cast->loop) {
    pw_thread_loop_lock(cast->loop);
  }
  cast->sessions[session_handle] = s;
  if (cast->loop) {
    pw_thread_loop_unlock(cast->loop);
  }
  GVariantBuilder results;
  g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&results, "{sv}", "session_id",
                        g_variant_new_string(session_handle));
  Reply(invocation, kSuccess, g_variant_builder_end(&results));
}

void SelectSources(ScreenCast* cast,
                   GDBusMethodInvocation* invocation,
                   GVariant* parameters) {
  const gchar* handle = nullptr;
  const gchar* session_handle = nullptr;
  const gchar* app_id = nullptr;
  g_autoptr(GVariant) options = nullptr;
  g_variant_get(parameters, "(&o&o&s@a{sv})", &handle, &session_handle,
                &app_id, &options);
  auto it = cast->sessions.find(session_handle);
  if (it == cast->sessions.end()) {
    Reply(invocation, kFailed, nullptr);
    return;
  }
  guint32 types = 0;
  guint32 cursor_mode = 0;
  it->second->wants_screen = true;
  if (g_variant_lookup(options, "types", "u", &types) && types) {
    it->second->types = types & (kMonitor | kWindow);
  }
  if (!it->second->types) {
    it->second->types = kMonitor;
  }
  if (g_variant_lookup(options, "cursor_mode", "u", &cursor_mode) &&
      cursor_mode) {
    it->second->cursor_mode = cursor_mode;
  }
  Reply(invocation, kSuccess, nullptr);
}

}  // namespace

ScreenCast* ScreenCastNew(GDBusConnection* session,
                          GDBusConnection* system,
                          GDBusInterfaceInfo* session_info) {
  ScreenCast* cast = new ScreenCast;
  cast->session = session;
  cast->system = system;
  cast->session_info = session_info;
  return cast;
}

void ScreenCastMethodCall(ScreenCast* cast,
                          const gchar* method_name,
                          GVariant* parameters,
                          GDBusMethodInvocation* invocation) {
  if (g_strcmp0(method_name, "CreateSession") == 0) {
    CreateSession(cast, invocation, parameters, /*remote=*/false);
  } else if (g_strcmp0(method_name, "SelectSources") == 0) {
    SelectSources(cast, invocation, parameters);
  } else if (g_strcmp0(method_name, "Start") == 0) {
    Start(cast, invocation, parameters);
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
  }
}

GVariant* ScreenCastGetProperty(ScreenCast*, const gchar* property_name) {
  if (g_strcmp0(property_name, "AvailableSourceTypes") == 0) {
    return g_variant_new_uint32(kMonitor | kWindow);
  }
  if (g_strcmp0(property_name, "AvailableCursorModes") == 0) {
    return g_variant_new_uint32(kCursorHidden | kCursorEmbedded);
  }
  if (g_strcmp0(property_name, "version") == 0) {
    // Cursor modes; no restore tokens (version 4).
    return g_variant_new_uint32(2);
  }
  return nullptr;
}

void RemoteDesktopMethodCall(ScreenCast* cast,
                             const gchar* method_name,
                             GVariant* parameters,
                             GDBusMethodInvocation* invocation) {
  if (g_strcmp0(method_name, "CreateSession") == 0) {
    CreateSession(cast, invocation, parameters, /*remote=*/true);
  } else if (g_strcmp0(method_name, "SelectDevices") == 0) {
    const gchar* session_handle = nullptr;
    g_autoptr(GVariant) options = nullptr;
    g_variant_get(parameters, "(&o&o&s@a{sv})", nullptr, &session_handle,
                  nullptr, &options);
    CastSession* s = SessionFor(cast, session_handle);
    guint32 types = 0;
    if (s && g_variant_lookup(options, "types", "u", &types) && types) {
      s->devices = types & (kKeyboard | kPointer);
    }
    Reply(invocation, s ? kSuccess : kFailed, nullptr);
  } else if (g_strcmp0(method_name, "Start") == 0) {
    Start(cast, invocation, parameters);
  } else if (g_str_has_prefix(method_name, "Notify")) {
    Notify(cast, method_name, parameters);
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (g_strcmp0(method_name, "ConnectToEIS") == 0) {
    // Apps then send input through the Notify methods.
    g_dbus_method_invocation_return_error_literal(
        invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
        "libei is not offered: use the Notify methods");
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
  }
}

GVariant* RemoteDesktopGetProperty(ScreenCast*, const gchar* property_name) {
  if (g_strcmp0(property_name, "AvailableDeviceTypes") == 0) {
    return g_variant_new_uint32(kKeyboard | kPointer);
  }
  if (g_strcmp0(property_name, "version") == 0) {
    // 2: the clipboard (xdg-desktop-portal offers it from version 2 on).
    // Its persistence options are not kept, and input goes through the
    // Notify methods, not libei (ConnectToEIS).
    return g_variant_new_uint32(2);
  }
  return nullptr;
}

// RequestClipboard(o session, a{sv}), SetSelection(o session, a{sv}
// {mime_types}), SelectionWrite(o session, u serial) -> (h fd),
// SelectionWriteDone(o session, u serial, b success), SelectionRead(o
// session, s mime_type) -> (h fd).
void ClipboardMethodCall(ScreenCast* cast,
                         const gchar* method_name,
                         GVariant* parameters,
                         GDBusMethodInvocation* invocation) {
  const gchar* handle = nullptr;
  g_variant_get_child(parameters, 0, "&o", &handle);
  CastSession* s = SessionFor(cast, handle);
  if (!s || !s->remote) {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_INVALID_ARGS,
                                          "no remote desktop session %s",
                                          handle);
    return;
  }
  if (g_strcmp0(method_name, "RequestClipboard") == 0) {
    s->clipboard = true;
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (g_strcmp0(method_name, "SetSelection") == 0) {
    g_autoptr(GVariant) options = nullptr;
    g_variant_get(parameters, "(&o@a{sv})", nullptr, &options);
    g_autofree const gchar** offered = nullptr;
    g_variant_lookup(options, "mime_types", "^a&s", &offered);
    const char* chosen = nullptr;
    for (const char* type : kSelectionTypes) {
      for (const gchar** o = offered; o && *o && !chosen; ++o) {
        if (g_strcmp0(*o, type) == 0) {
          chosen = type;
        }
      }
    }
    g_dbus_method_invocation_return_value(invocation, nullptr);
    if (!chosen || !s->clipboard || s->control_token.empty()) {
      return;
    }
    if (s->transfer && !s->transfer->buffer) {
      delete s->transfer;
    }
    ClipboardTransfer* t = new ClipboardTransfer;
    t->cast = cast;
    t->handle = s->handle;
    t->serial = s->next_serial++;
    t->mime_type = chosen;
    s->transfer = t;
    EmitClipboard(s, "SelectionTransfer",
                  g_variant_new("(osu)", s->handle.c_str(), chosen, t->serial));
  } else if (g_strcmp0(method_name, "SelectionWrite") == 0) {
    guint32 serial = 0;
    g_variant_get(parameters, "(&ou)", nullptr, &serial);
    ClipboardTransfer* t = s->transfer;
    int fds[2];
    if (!t || t->serial != serial || t->buffer ||
        pipe2(fds, O_CLOEXEC) < 0) {
      g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                            G_DBUS_ERROR_FAILED,
                                            "no transfer %u", serial);
      return;
    }
    GInputStream* in = g_unix_input_stream_new(fds[0], TRUE);
    t->buffer = g_memory_output_stream_new_resizable();
    g_output_stream_splice_async(
        t->buffer, in,
        static_cast<GOutputStreamSpliceFlags>(
            G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
            G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET),
        G_PRIORITY_DEFAULT, nullptr, OnSelectionSpliced, t);
    g_object_unref(in);
    ReplyFd(invocation, fds[1]);
  } else if (g_strcmp0(method_name, "SelectionWriteDone") == 0) {
    guint32 serial = 0;
    gboolean success = FALSE;
    g_variant_get(parameters, "(&oub)", nullptr, &serial, &success);
    ClipboardTransfer* t = s->transfer;
    g_dbus_method_invocation_return_value(invocation, nullptr);
    if (t && t->serial == serial && t->buffer) {
      t->write_done = true;
      t->success = success;
      FinishTransfer(t);
    }
  } else if (g_strcmp0(method_name, "SelectionRead") == 0) {
    const gchar* mime_type = nullptr;
    g_variant_get(parameters, "(&o&s)", nullptr, &mime_type);
    int fds[2];
    if (!s->clipboard || s->control_token.empty() ||
        pipe2(fds, O_CLOEXEC) < 0) {
      g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                            G_DBUS_ERROR_FAILED,
                                            "no clipboard");
      return;
    }
    g_dbus_connection_call(
        cast->system, vm_tools::apps::kVmApplicationsServiceName,
        vm_tools::apps::kVmApplicationsServicePath,
        vm_tools::apps::kVmApplicationsServiceInterface,
        "NickelClipboardRead",
        g_variant_new("(ss)", s->control_token.c_str(), mime_type),
        G_VARIANT_TYPE("(bay)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
        OnClipboardRead, new ReadCall{fds[1]});
    ReplyFd(invocation, fds[0]);
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
  }
}

GVariant* ClipboardGetProperty(ScreenCast*, const gchar* property_name) {
  if (g_strcmp0(property_name, "version") == 0) {
    return g_variant_new_uint32(1);
  }
  return nullptr;
}
