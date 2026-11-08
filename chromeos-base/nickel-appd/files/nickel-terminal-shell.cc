// Distributed under the terms of the GNU General Public License v2
//
// nickel-terminal-shell [command...]: the Terminal app's shell.
//
// Chrome runs the Terminal app's command as its own child, on a terminal
// Chrome holds the other end of. session_manager starts Chrome with
// no_new_privs and a Landlock sandbox, which its children inherit: sudo and
// mount would not work in a shell Chrome started. So this command hands its
// terminal to nickel-appd (org.nickelos.Apps.SpawnShell), which starts the
// session user's login shell on it, or the command, outside that sandbox, and
// waits for the exit status nickel-appd writes to a pipe when the shell ends.
// It exits with that status, so Chrome sees the shell end as before.
//
// Without nickel-appd (no session plumbing) it runs the shell itself.

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <vector>

namespace {

[[noreturn]] void RunShellHere(int argc, char** argv) {
  const char* home = getenv("HOME");
  if (home && chdir(home) != 0) {
    perror("nickel-terminal-shell: chdir");
  }
  std::vector<char*> shell = {const_cast<char*>("/bin/bash"),
                              const_cast<char*>("-l")};
  if (argc > 1) {
    shell.push_back(const_cast<char*>("-c"));
    shell.push_back(const_cast<char*>("exec \"$@\""));
    shell.push_back(const_cast<char*>("bash"));
    for (int i = 1; i < argc; ++i) {
      shell.push_back(argv[i]);
    }
  }
  shell.push_back(nullptr);
  execv(shell[0], shell.data());
  perror("nickel-terminal-shell: /bin/bash");
  _exit(127);
}

}  // namespace

int main(int argc, char** argv) {
  if (!isatty(STDIN_FILENO)) {
    RunShellHere(argc, argv);
  }

  int status_pipe[2];
  if (pipe2(status_pipe, O_CLOEXEC) != 0) {
    RunShellHere(argc, argv);
  }

  GError* error = nullptr;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
  if (!bus) {
    fprintf(stderr, "nickel-terminal-shell: %s\n", error->message);
    RunShellHere(argc, argv);
  }

  // Give up the terminal (Chrome made it ours), so that the shell can make it
  // its controlling terminal. Giving it up sends us SIGHUP.
  signal(SIGHUP, SIG_IGN);
  ioctl(STDIN_FILENO, TIOCNOTTY);

  GUnixFDList* fds = g_unix_fd_list_new();
  gint terminal = g_unix_fd_list_append(fds, STDIN_FILENO, nullptr);
  gint status = g_unix_fd_list_append(fds, status_pipe[1], nullptr);
  close(status_pipe[1]);

  GVariantBuilder command;
  g_variant_builder_init(&command, G_VARIANT_TYPE("as"));
  for (int i = 1; i < argc; ++i) {
    g_variant_builder_add(&command, "s", argv[i]);
  }
  GVariantBuilder env;
  g_variant_builder_init(&env, G_VARIANT_TYPE("as"));
  for (const char* name : {"TERM", "COLORTERM", "TERM_PROGRAM"}) {
    if (const char* value = getenv(name)) {
      gchar* assignment = g_strdup_printf("%s=%s", name, value);
      g_variant_builder_add(&env, "s", assignment);
      g_free(assignment);
    }
  }

  GVariant* reply = g_dbus_connection_call_with_unix_fd_list_sync(
      bus, "org.nickelos.Apps", "/org/nickelos/Apps", "org.nickelos.Apps",
      "SpawnShell",
      g_variant_new("(hhasas)", terminal, status, &command, &env), nullptr,
      G_DBUS_CALL_FLAGS_NONE, 10000, fds, nullptr, nullptr, &error);
  g_object_unref(fds);
  if (!reply) {
    fprintf(stderr, "nickel-terminal-shell: %s\n", error->message);
    // Take the terminal back for the shell started here.
    ioctl(STDIN_FILENO, TIOCSCTTY, 0);
    signal(SIGHUP, SIG_DFL);
    close(status_pipe[0]);
    RunShellHere(argc, argv);
  }
  g_variant_unref(reply);

  gint32 exit_status = 1;
  ssize_t n;
  do {
    n = read(status_pipe[0], &exit_status, sizeof(exit_status));
  } while (n < 0 && errno == EINTR);
  // Without a status nickel-appd went away; the shell got its hangup.
  return n == sizeof(exit_status) ? exit_status : 1;
}
