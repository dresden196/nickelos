// Distributed under the terms of the GNU General Public License v2
//
// nickel-logind-session USER: the NickelOS session of USER, as elogind sees
// it. ChromeOS's session_manager starts sessions without PAM, so elogind
// would not know about them; this helper opens a PAM session (service
// nickel-session, pam_elogind) and stays as the session's leader. The
// session is on seat0 with no VT, type wayland, class user, so elogind makes
// it the active session: polkit then treats the user's processes as the
// active user's (password prompts through the session's agent), and
// elogind's uaccess gives the user the seat's devices.
//
// It writes the session id and its cgroup to /run/nickel/session/logind;
// nickel-session-exec moves the session's programs into that cgroup, so they
// belong to the session. On SIGTERM (the nickel-logind-session job stopping
// with the user session) it ends the session.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <security/pam_appl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#define STATE_FILE "/run/nickel/session/logind"

// No prompts: the user signed in through Chrome already.
static int no_conversation(int n, const struct pam_message** msg,
                           struct pam_response** resp, void* data) {
  (void)n;
  (void)msg;
  (void)resp;
  (void)data;
  return PAM_CONV_ERR;
}

// The name=elogind line of /proc/self/cgroup, without its prefix.
static int session_cgroup(char* out, size_t len) {
  FILE* f = fopen("/proc/self/cgroup", "re");
  char line[PATH_MAX];
  int found = 0;
  if (!f) {
    return 0;
  }
  while (fgets(line, sizeof(line), f)) {
    char* p = strstr(line, ":name=elogind:");
    if (p) {
      p += strlen(":name=elogind:");
      p[strcspn(p, "\n")] = '\0';
      snprintf(out, len, "%s", p);
      found = 1;
      break;
    }
  }
  fclose(f);
  return found;
}

static void write_file(const char* path, const char* text) {
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0 || write(fd, text, strlen(text)) < 0) {
    syslog(LOG_WARNING, "cannot write %s: %m", path);
  }
  if (fd >= 0) {
    close(fd);
  }
}

static void run(const char* const argv[]) {
  pid_t pid = fork();
  if (pid == 0) {
    execv(argv[0], (char* const*)argv);
    _exit(127);
  }
  if (pid > 0) {
    waitpid(pid, NULL, 0);
  }
}

int main(int argc, char** argv) {
  static const struct pam_conv conv = {no_conversation, NULL};
  pam_handle_t* pamh = NULL;
  char cgroup[PATH_MAX] = "";
  char text[PATH_MAX + 64];
  sigset_t set;
  int sig;
  int r;

  openlog("nickel-logind-session", LOG_PID, LOG_DAEMON);
  if (argc != 2) {
    fprintf(stderr, "usage: %s USER\n", argv[0]);
    return 2;
  }

  // SIGTERM ends the session; block it until we wait for it.
  sigemptyset(&set);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGINT);
  sigprocmask(SIG_BLOCK, &set, NULL);

  r = pam_start("nickel-session", argv[1], &conv, &pamh);
  if (r == PAM_SUCCESS) {
    pam_putenv(pamh, "XDG_SESSION_TYPE=wayland");
    pam_putenv(pamh, "XDG_SESSION_CLASS=user");
    pam_putenv(pamh, "XDG_SEAT=seat0");
    pam_putenv(pamh, "XDG_SESSION_DESKTOP=NickelOS");
    r = pam_open_session(pamh, 0);
  }
  if (r != PAM_SUCCESS) {
    syslog(LOG_ERR, "cannot open a session for %s: %s", argv[1],
           pamh ? pam_strerror(pamh, r) : "pam_start failed");
    if (pamh) {
      pam_end(pamh, r);
    }
    return 1;
  }

  const char* id = pam_getenv(pamh, "XDG_SESSION_ID");
  if (!id || !session_cgroup(cgroup, sizeof(cgroup))) {
    syslog(LOG_ERR, "elogind gave no session for %s", argv[1]);
    pam_close_session(pamh, 0);
    pam_end(pamh, PAM_SUCCESS);
    return 1;
  }
  char session_id[64];
  snprintf(session_id, sizeof(session_id), "%s", id);
  snprintf(text, sizeof(text), "%s\n%s\n", session_id, cgroup);
  int fd = open(STATE_FILE ".new", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                0644);
  if (fd < 0 || write(fd, text, strlen(text)) < 0 ||
      rename(STATE_FILE ".new", STATE_FILE) < 0) {
    syslog(LOG_ERR, "cannot write " STATE_FILE ": %m");
  }
  if (fd >= 0) {
    close(fd);
  }
  syslog(LOG_INFO, "session %s of %s in %s", session_id, argv[1], cgroup);

  do {
    if (sigwait(&set, &sig) != 0) {
      sig = SIGTERM;
    }
  } while (sig != SIGTERM && sig != SIGINT);

  // Leave the session's cgroup, so ending the session does not end us
  // before we closed it, then end everything left in it.
  snprintf(text, sizeof(text), "%d\n", (int)getpid());
  write_file("/sys/fs/cgroup/elogind/cgroup.procs", text);
  unlink(STATE_FILE);
  const char* terminate[] = {"/usr/bin/loginctl", "terminate-session",
                             session_id, NULL};
  run(terminate);
  pam_close_session(pamh, 0);
  pam_end(pamh, PAM_SUCCESS);

  // Wait for elogind to finish removing the session.
  snprintf(text, sizeof(text), "/run/systemd/sessions/%s", session_id);
  for (int i = 0; i < 50 && access(text, F_OK) == 0; ++i) {
    usleep(100 * 1000);
  }
  syslog(LOG_INFO, "session %s ended", session_id);
  return 0;
}
