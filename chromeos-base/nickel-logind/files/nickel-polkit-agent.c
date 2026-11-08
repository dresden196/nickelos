// Distributed under the terms of the GNU General Public License v2
//
// nickel-polkit-agent: NickelOS's polkit authentication agent. When a program
// asks polkit for an administrator's permission (pkexec, a system-wide
// Flatpak, a disk tool), polkit hands the request to the session's agent.
// This one has Chrome show ChromeOS's own password prompt
// (org.chromium.VmApplicationsService's NickelAdminAuthAsk) and checks the
// password through polkit's helper (PAM), as every agent does: the agent
// can't grant anything itself, polkitd takes the helper's word.
//
// It runs as root in the session's elogind session (nickel-polkit-agent.conf):
// polkit lets root register an agent for another user's session, and only
// root may ask Chrome for a password (org.nickelos.PolkitAgent.conf), so no
// app can raise a real prompt and read its answer. polkitd keeps the agent
// as the session user's, though (polkit#17), and takes the helper's word
// only from a helper whose real uid is that user's, so the helper is started
// with it (initiate_as_session_user), as under a desktop's own agent. A password goes from
// Chrome's reply to the helper's stdin and is wiped after; it is never
// logged. Requests are shown one at a time; a wrong password keeps the
// prompt open with PAM's message, until the right one or Cancel.
//
//   NickelAdminAuthAsk(s request_id, s message, s icon_name,
//                      a{ss} details, as administrators, s error)
//       -> (s user, s password)      ("", "") when the user cancelled
//   NickelAdminAuthClose(s request_id)

#define _GNU_SOURCE  // setresuid
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE

#include <errno.h>
#include <gio/gio.h>
#include <glib.h>
#include <locale.h>
#include <polkit/polkit.h>
#include <polkitagent/polkitagent.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define CHROME_SERVICE "org.chromium.VmApplicationsService"
#define CHROME_PATH "/org/chromium/VmApplicationsService"
#define CHROME_INTERFACE "org.chromium.VmApplicationsService"
#define AGENT_PATH "/org/nickelos/PolkitAgent"
#define LOGIND_FILE "/run/nickel/session/logind"

static GDBusConnection *bus;
static uid_t session_uid;  // the session's user

typedef struct {
  GTask *task;  // initiate_authentication's
  char *request_id;
  char *action_id;
  char *message;
  char *icon_name;
  char *cookie;
  GVariant *details;    // a{ss}
  GPtrArray *admins;    // user names offered
  GPtrArray *identities;  // PolkitIdentity*, parallel to admins
  GCancellable *cancellable;
  gulong cancel_id;
  PolkitAgentSession *session;
  char *password;
  GString *pam_error;
} Request;

static GQueue queue = G_QUEUE_INIT;
static Request *active;

static void start_next(void);
static void ask(Request *req, const char *error);

static void wipe(char **s) {
  if (*s) {
    explicit_bzero(*s, strlen(*s));
    g_free(*s);
    *s = NULL;
  }
}

static void request_free(Request *req) {
  if (req->cancel_id)
    g_cancellable_disconnect(req->cancellable, req->cancel_id);
  g_clear_object(&req->cancellable);
  g_clear_object(&req->session);
  wipe(&req->password);
  g_free(req->request_id);
  g_free(req->action_id);
  g_free(req->message);
  g_free(req->icon_name);
  g_free(req->cookie);
  g_variant_unref(req->details);
  g_ptr_array_unref(req->admins);
  g_ptr_array_unref(req->identities);
  g_string_free(req->pam_error, TRUE);
  g_free(req);
}

static void on_closed(GObject *source, GAsyncResult *res, gpointer data) {
  GVariant *reply = g_dbus_connection_call_finish(bus, res, NULL);
  if (reply)
    g_variant_unref(reply);
}

// With a callback: Chrome's reply is expected (without one GDBus says none
// is, and dbus-daemon logs Chrome's as rejected).
static void close_prompt(Request *req) {
  g_dbus_connection_call(bus, CHROME_SERVICE, CHROME_PATH, CHROME_INTERFACE,
                         "NickelAdminAuthClose",
                         g_variant_new("(s)", req->request_id), NULL,
                         G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_closed, NULL);
}

// Ends the request (polkit gets |error|, or success when NULL) and shows the
// next one.
static void finish(Request *req, GError *error) {
  if (error) {
    syslog(LOG_INFO, "%s: %s", req->action_id, error->message);
    g_task_return_error(req->task, error);
  } else {
    syslog(LOG_INFO, "%s: authorized", req->action_id);
    g_task_return_boolean(req->task, TRUE);
  }
  g_object_unref(req->task);
  if (active == req)
    active = NULL;
  request_free(req);
  start_next();
}

static void finish_cancelled(Request *req) {
  close_prompt(req);
  finish(req, g_error_new(POLKIT_ERROR, POLKIT_ERROR_CANCELLED,
                          "Authentication dialog was dismissed by the user"));
}

static void on_pam_request(PolkitAgentSession *session, const char *prompt,
                           gboolean echo_on, gpointer data) {
  Request *req = data;
  polkit_agent_session_response(session, req->password ? req->password : "");
}

static void on_pam_text(PolkitAgentSession *session, const char *text,
                        gpointer data) {
  Request *req = data;
  if (!text || !*text)
    return;
  if (req->pam_error->len)
    g_string_append_c(req->pam_error, ' ');
  g_string_append(req->pam_error, text);
}

static void on_pam_completed(PolkitAgentSession *session, gboolean gained,
                             gpointer data) {
  Request *req = data;
  wipe(&req->password);
  g_clear_object(&req->session);
  if (gained) {
    close_prompt(req);
    finish(req, NULL);
  } else if (g_cancellable_is_cancelled(req->cancellable)) {
    finish_cancelled(req);
  } else {
    // A wrong password: the same prompt again, with PAM's word if it gave
    // one (a locked account says so), Chrome's own "incorrect" otherwise.
    syslog(LOG_INFO, "%s: wrong password, asking again", req->action_id);
    char *error = g_string_free_and_steal(req->pam_error);
    req->pam_error = g_string_new(NULL);
    ask(req, error);
    g_free(error);
  }
}

// polkit's helper (setuid root) reports its real uid to polkitd with the
// result, and polkitd matches it to the agent's user: the session's. It is
// spawned inside polkit_agent_session_initiate, so for that call this
// process's real uid is the session user's; the effective and saved uids
// stay root's, and so do the capabilities, which put it back.
static void initiate_as_session_user(PolkitAgentSession *session) {
  if (setresuid(session_uid, 0, 0) != 0)
    syslog(LOG_ERR, "real uid %u: %m", (unsigned)session_uid);
  polkit_agent_session_initiate(session);
  if (setresuid(0, 0, 0) != 0) {
    syslog(LOG_ERR, "back to root: %m");
    abort();
  }
}

static PolkitIdentity *identity_for(Request *req, const char *user) {
  for (guint i = 0; i < req->admins->len; i++) {
    if (g_strcmp0(g_ptr_array_index(req->admins, i), user) == 0)
      return g_ptr_array_index(req->identities, i);
  }
  return NULL;
}

static void on_answer(GObject *source, GAsyncResult *res, gpointer data) {
  Request *req = data;
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_finish(bus, res, &error);
  if (!reply) {
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_error_free(error);
      finish_cancelled(req);
      return;
    }
    // Chrome gone, or no answer for an hour (the bus's limit): no prompt.
    char *text = g_strdup_printf("no prompt: %s", error->message);
    g_error_free(error);
    close_prompt(req);
    finish(req, g_error_new_literal(POLKIT_ERROR, POLKIT_ERROR_FAILED, text));
    g_free(text);
    return;
  }
  const char *user = NULL;
  char *password = NULL;
  g_variant_get(reply, "(&ss)", &user, &password);
  PolkitIdentity *identity = (user && *user) ? identity_for(req, user) : NULL;
  if (!user || !*user || !identity) {
    // Cancelled (or an answer for someone polkit didn't offer).
    if (password)
      explicit_bzero(password, strlen(password));
    g_free(password);
    g_variant_unref(reply);
    finish_cancelled(req);
    return;
  }
  req->password = password;
  g_variant_unref(reply);
  req->session = polkit_agent_session_new(identity, req->cookie);
  g_signal_connect(req->session, "request", G_CALLBACK(on_pam_request), req);
  g_signal_connect(req->session, "show-error", G_CALLBACK(on_pam_text), req);
  g_signal_connect(req->session, "show-info", G_CALLBACK(on_pam_text), req);
  g_signal_connect(req->session, "completed", G_CALLBACK(on_pam_completed),
                   req);
  initiate_as_session_user(req->session);
}

static void ask(Request *req, const char *error) {
  GVariantBuilder admins;
  g_variant_builder_init(&admins, G_VARIANT_TYPE("as"));
  for (guint i = 0; i < req->admins->len; i++)
    g_variant_builder_add(&admins, "s", g_ptr_array_index(req->admins, i));
  g_dbus_connection_call(
      bus, CHROME_SERVICE, CHROME_PATH, CHROME_INTERFACE, "NickelAdminAuthAsk",
      g_variant_new("(sss@a{ss}ass)", req->request_id, req->message,
                    req->icon_name, req->details, &admins,
                    error ? error : ""),
      G_VARIANT_TYPE("(ss)"), G_DBUS_CALL_FLAGS_NONE, G_MAXINT,
      req->cancellable, on_answer, req);
}

static void on_cancelled(GCancellable *cancellable, gpointer data) {
  Request *req = data;
  // polkit (or the program asking) gave up: the Chrome call is cancelled
  // with it (on_answer), or a running check ends as not gained.
  if (req->session)
    polkit_agent_session_cancel(req->session);
}

static void start_next(void) {
  if (active)
    return;
  active = g_queue_pop_head(&queue);
  if (!active)
    return;
  if (g_cancellable_is_cancelled(active->cancellable)) {
    finish_cancelled(active);
    return;
  }
  syslog(LOG_INFO, "%s: asking", active->action_id);
  ask(active, NULL);
}

// The listener.

typedef struct {
  PolkitAgentListener parent;
} NickelListener;

typedef struct {
  PolkitAgentListenerClass parent_class;
} NickelListenerClass;

G_DEFINE_TYPE(NickelListener, nickel_listener, POLKIT_AGENT_TYPE_LISTENER)

static void initiate_authentication(PolkitAgentListener *listener,
                                    const char *action_id,
                                    const char *message,
                                    const char *icon_name,
                                    PolkitDetails *details,
                                    const char *cookie, GList *identities,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data) {
  Request *req = g_new0(Request, 1);
  req->task = g_task_new(listener, cancellable, callback, user_data);
  req->request_id = g_uuid_string_random();
  req->action_id = g_strdup(action_id);
  req->message = g_strdup(message ? message : "");
  req->icon_name = g_strdup(icon_name ? icon_name : "");
  req->cookie = g_strdup(cookie);
  req->pam_error = g_string_new(NULL);
  req->cancellable = cancellable ? g_object_ref(cancellable)
                                 : g_cancellable_new();

  GVariantBuilder b;
  g_variant_builder_init(&b, G_VARIANT_TYPE("a{ss}"));
  if (details) {
    gchar **keys = polkit_details_get_keys(details);
    for (gchar **k = keys; k && *k; k++) {
      const char *v = polkit_details_lookup(details, *k);
      g_variant_builder_add(&b, "{ss}", *k, v ? v : "");
    }
    g_strfreev(keys);
  }
  req->details = g_variant_ref_sink(g_variant_builder_end(&b));

  // The administrators polkit offers (wheel's members), root left out unless
  // it is the only one.
  req->admins = g_ptr_array_new_with_free_func(g_free);
  req->identities = g_ptr_array_new_with_free_func(g_object_unref);
  PolkitIdentity *root = NULL;
  for (GList *l = identities; l; l = l->next) {
    if (!POLKIT_IS_UNIX_USER(l->data))
      continue;
    gint uid = polkit_unix_user_get_uid(POLKIT_UNIX_USER(l->data));
    struct passwd *pw = getpwuid(uid);
    if (!pw)
      continue;
    if (uid == 0) {
      root = l->data;
      continue;
    }
    g_ptr_array_add(req->admins, g_strdup(pw->pw_name));
    g_ptr_array_add(req->identities, g_object_ref(l->data));
  }
  if (req->admins->len == 0 && root) {
    g_ptr_array_add(req->admins, g_strdup("root"));
    g_ptr_array_add(req->identities, g_object_ref(root));
  }
  if (req->admins->len == 0) {
    finish(req, g_error_new_literal(POLKIT_ERROR, POLKIT_ERROR_FAILED,
                                    "no administrator to ask"));
    return;
  }
  req->cancel_id = g_cancellable_connect(req->cancellable,
                                         G_CALLBACK(on_cancelled), req, NULL);
  g_queue_push_tail(&queue, req);
  start_next();
}

static gboolean initiate_authentication_finish(PolkitAgentListener *listener,
                                               GAsyncResult *res,
                                               GError **error) {
  return g_task_propagate_boolean(G_TASK(res), error);
}

static void nickel_listener_init(NickelListener *self) {}

static void nickel_listener_class_init(NickelListenerClass *klass) {
  PolkitAgentListenerClass *lc = POLKIT_AGENT_LISTENER_CLASS(klass);
  lc->initiate_authentication = initiate_authentication;
  lc->initiate_authentication_finish = initiate_authentication_finish;
}

// The session's user, from elogind.
static gboolean get_session_uid(const char *session_id, uid_t *uid) {
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.login1", "/org/freedesktop/login1",
      "org.freedesktop.login1.Manager", "GetSession",
      g_variant_new("(s)", session_id), G_VARIANT_TYPE("(o)"),
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (!reply) {
    syslog(LOG_ERR, "session %s: %s", session_id, error->message);
    g_error_free(error);
    return FALSE;
  }
  const char *path = NULL;
  g_variant_get(reply, "(&o)", &path);
  GVariant *user = g_dbus_connection_call_sync(
      bus, "org.freedesktop.login1", path, "org.freedesktop.DBus.Properties",
      "Get", g_variant_new("(ss)", "org.freedesktop.login1.Session", "User"),
      G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_variant_unref(reply);
  if (!user) {
    syslog(LOG_ERR, "session %s's user: %s", session_id, error->message);
    g_error_free(error);
    return FALSE;
  }
  GVariant *value = NULL;
  g_variant_get(user, "(v)", &value);
  guint32 u = 0;
  const char *user_path = NULL;
  g_variant_get(value, "(u&o)", &u, &user_path);
  g_variant_unref(value);
  g_variant_unref(user);
  *uid = u;
  return TRUE;
}

int main(void) {
  setlocale(LC_ALL, "");
  openlog("nickel-polkit-agent", LOG_PID, LOG_AUTH);

  char *contents = NULL;
  if (!g_file_get_contents(LOGIND_FILE, &contents, NULL, NULL)) {
    syslog(LOG_ERR, "no session (%s)", LOGIND_FILE);
    return 1;
  }
  char *session_id = g_strstrip(g_strndup(contents, strcspn(contents, "\n")));
  g_free(contents);

  GError *error = NULL;
  bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus) {
    syslog(LOG_ERR, "system bus: %s", error->message);
    return 1;
  }
  if (!get_session_uid(session_id, &session_uid) || session_uid == 0) {
    syslog(LOG_ERR, "no user for session %s", session_id);
    return 1;
  }
  PolkitSubject *subject = polkit_unix_session_new(session_id);
  PolkitAgentListener *listener = g_object_new(nickel_listener_get_type(),
                                               NULL);
  gpointer handle = polkit_agent_listener_register(
      listener, POLKIT_AGENT_REGISTER_FLAGS_NONE, subject, AGENT_PATH, NULL,
      &error);
  if (!handle) {
    syslog(LOG_ERR, "registering for session %s: %s", session_id,
           error->message);
    return 1;
  }
  syslog(LOG_INFO, "the agent of session %s", session_id);
  g_main_loop_run(g_main_loop_new(NULL, FALSE));
  return 0;
}
