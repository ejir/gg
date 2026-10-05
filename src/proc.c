/* gg — process helpers: spawn, capture, stream, sudo, exec. */
#include "gg.h"

#include <spawn.h>
#include <termios.h>

extern char **environ;

void spawn_opts_init(spawn_opts *o) { memset(o, 0, sizeof(*o)); }

int gg_is_root(void) {
#if GG_WINDOWS
  return 0;
#else
  return geteuid() == 0;
#endif
}

void proc_echo_cmd(char **argv) {
  fflush(stdout);
  sbuf b;
  sb_init(&b, 128);
  sb_adds(&b, c_accent());
  sb_adds(&b, "$ ");
  sb_adds(&b, c_reset());
  for (int i = 0; argv[i]; i++) {
    int need = 0;
    for (const char *p = argv[i]; *p; p++)
      if (gg_is_space((unsigned char)*p) || strchr("\"'\\$`!*?()[]{}&|<>;", *p))
        need = 1;
    char *q = need ? gg_shell_quote(argv[i]) : gg_strdup(argv[i]);
    if (i) sb_addc(&b, ' ');
    sb_adds(&b, q);
    free(q);
  }
  sb_addc(&b, '\n');
  fwrite(b.p, 1, b.n, stdout);
  fflush(stdout);
  sb_free(&b);
}

char *gg_shell_quote(const char *s) {
#if GG_WINDOWS
  /* the cosmopolitan command interpreter understands posix quotes */
  sbuf b;
  sb_init(&b, strlen(s) + 8);
  sb_addc(&b, '\'');
  for (const char *p = s; *p; p++) {
    if (*p == '\'') sb_adds(&b, "'\"'\"'");
    else sb_addc(&b, *p);
  }
  sb_addc(&b, '\'');
  return b.p;
#else
  sbuf b;
  sb_init(&b, strlen(s) + 8);
  sb_addc(&b, '\'');
  for (const char *p = s; *p; p++) {
    if (*p == '\'') sb_adds(&b, "'\\''");
    else sb_addc(&b, *p);
  }
  sb_addc(&b, '\'');
  return b.p;
#endif
}

char *gg_join_argv(char **argv) {
  sbuf b;
  sb_init(&b, 128);
  for (int i = 0; argv[i]; i++) {
    if (i) sb_addc(&b, ' ');
    int need = 0;
    for (const char *p = argv[i]; *p; p++)
      if (gg_is_space((unsigned char)*p) || strchr("\"'\\$`!*?()[]{}&|<>;", *p))
        need = 1;
    if (need) {
      char *q = gg_shell_quote(argv[i]);
      sb_adds(&b, q);
      free(q);
    } else {
      sb_adds(&b, argv[i]);
    }
  }
  return b.p;
}

char **proc_sudo_prefix(char **argv, strvec *storage) {
  if (gg_is_root() || !gg_streq(argv[0], "sudo")) return argv;
  return argv;
}

/* resolve "sudo xxx" into a spawnable argv (uses the sudo binary when
 * available, otherwise leaves the command alone) */
static char **maybe_sudo(char **argv, strvec *tmp, int want_sudo) {
  if (!want_sudo || gg_is_root() || gg_streq(argv[0], "sudo")) {
    return argv;
  }
  if (!gg_have("sudo")) return argv;
  sv_reset(tmp);
  sv_push(tmp, "sudo");
  for (int i = 0; argv[i]; i++) sv_push(tmp, argv[i]);
  return sv_argv(tmp);
}

static char **wrap_shell(char **argv, strvec *tmp) {
  /* one argument means "this whole string is the command line" */
  char *line = (argv[0] && !argv[1]) ? gg_strdup(argv[0]) : gg_join_argv(argv);
  sv_reset(tmp);
#if GG_WINDOWS
  /* cosmopolitan ships a bourne-ish command interpreter (cocmd) and
   * popen()/system() already use it — but for -c style execution we
   * ask the shell to interpret the line. */
  sv_push(tmp, "sh");
  sv_push(tmp, "-c");
  sv_push(tmp, line);
#else
  const char *sh = getenv("SHELL");
  if (!sh || !*sh) sh = "/bin/sh";
  sv_push(tmp, sh);
  sv_push(tmp, "-c");
  sv_push(tmp, line);
#endif
  free(line);
  return sv_argv(tmp);
}

static int decode_status(int st) {
  if (st == -1) return 127;
  if (WIFEXITED(st)) return WEXITSTATUS(st);
  if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
  return st;
}

int proc_run(char **argv, spawn_opts *o, char **out) {
  spawn_opts def;
  if (!o) {
    spawn_opts_init(&def);
    o = &def;
  }
  if (!argv || !argv[0]) return 127;
  if (o->cwd && !gg_is_dir(o->cwd)) o->cwd = 0;
  strvec tmp, tmp2;
  sv_init(&tmp);
  sv_init(&tmp2);
  char **real = maybe_sudo(argv, &tmp, o->sudo);
  if (o->shell) real = wrap_shell(real, &tmp2);
  if (out) *out = 0;

  if (o->echo && !o->quiet) proc_echo_cmd(real);

  int pipefd[2] = {-1, -1};
  if (o->capture || o->on_line) {
    if (pipe(pipefd) != 0) {
      sv_free(&tmp);
      sv_free(&tmp2);
      return 127;
    }
  }
  int inpipe[2] = {-1, -1};
  if (o->input) {
    if (pipe(inpipe) != 0) {
      if (pipefd[0] >= 0) {
        close(pipefd[0]);
        close(pipefd[1]);
      }
      sv_free(&tmp);
      sv_free(&tmp2);
      return 127;
    }
  }

  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  if (o->capture || o->on_line) {
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 1);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 2);
    posix_spawn_file_actions_addclose(&fa, pipefd[0]);
    if (pipefd[1] != 1 && pipefd[1] != 2)
      posix_spawn_file_actions_addclose(&fa, pipefd[1]);
  }
  if (o->cwd) posix_spawn_file_actions_addchdir_np(&fa, o->cwd);
  if (o->input) {
    posix_spawn_file_actions_adddup2(&fa, inpipe[0], 0);
    posix_spawn_file_actions_addclose(&fa, inpipe[1]);
    posix_spawn_file_actions_addclose(&fa, inpipe[0]);
  } else if (o->capture || o->on_line) {
    /* give children an empty stdin by default: they must not steal the
     * terminal while we are capturing their output */
    posix_spawn_file_actions_addopen(&fa, 0, GG_WINDOWS ? "NUL" : "/dev/null",
                                     O_RDONLY, 0);
  }

  pid_t pid = -1;
  int rc = posix_spawnp(&pid, real[0], &fa, 0, real, environ);
  posix_spawn_file_actions_destroy(&fa);
  if (pipefd[1] >= 0) close(pipefd[1]);
  if (inpipe[0] >= 0) close(inpipe[0]);

  if (rc != 0) {
    if (pipefd[0] >= 0) close(pipefd[0]);
    if (inpipe[1] >= 0) close(inpipe[1]);
    sv_free(&tmp);
    sv_free(&tmp2);
    gg_error("%s: %s", real[0], strerror(rc));
    return 127;
  }

  if (inpipe[1] >= 0) {
    size_t len = strlen(o->input);
    size_t off = 0;
    while (off < len) {
      ssize_t n = write(inpipe[1], o->input + off, len - off);
      if (n <= 0) break;
      off += (size_t)n;
    }
    close(inpipe[1]);
  }

  sbuf cap;
  sb_init(&cap, 4096);
  int have_cap = o->capture || o->on_line;
  if (have_cap) {
    char buf[8192];
    sbuf line;
    sb_init(&line, 256);
    struct pollfd p;
    p.fd = pipefd[0];
    p.events = POLLIN;
    for (;;) {
      p.revents = 0;
      int pr = poll(&p, 1, 200);
      if (pr > 0 && (p.revents & (POLLIN | POLLHUP))) {
        ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n > 0) {
          if (o->capture) sb_addn(&cap, buf, (size_t)n);
          if (o->on_line) {
            for (ssize_t i = 0; i < n; i++) {
              char ch = buf[i];
              if (ch == '\n') {
                if (o->on_line) o->on_line(line.p, o->ud);
                sb_clear(&line);
              } else {
                sb_addc(&line, ch);
              }
            }
          }
        } else if (n == 0) {
          break;
        }
      } else if (pr < 0 && errno != EINTR) {
        break;
      }
    }
    if (o->on_line && line.n) o->on_line(line.p, o->ud);
    sb_free(&line);
    close(pipefd[0]);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (o->capture) {
    if (!cap.p) sb_adds(&cap, "");
    *out = cap.p;
  } else {
    sb_free(&cap);
  }
  sv_free(&tmp);
  sv_free(&tmp2);
  return decode_status(status);
}

int proc_run_interactive(char **argv, int sudo) {
  spawn_opts o;
  spawn_opts_init(&o);
  o.sudo = sudo;
  o.echo = 1;
  return proc_run(argv, &o, 0);
}

int proc_exec(char **argv, int sudo) {
  strvec tmp;
  sv_init(&tmp);
  char **real = maybe_sudo(argv, &tmp, sudo);
  execvp(real[0], real);
  int e = errno;
  sv_free(&tmp);
  gg_error("%s: %s", argv[0], strerror(e));
  return 127;
}

char *proc_shell_capture(const char *cmd, int *status) {
  spawn_opts o;
  spawn_opts_init(&o);
  o.shell = 1;
  o.capture = 1;
  char *argv[2];
  argv[0] = (char *)cmd;
  argv[1] = 0;
  char *out = 0;
  int rc = proc_run(argv, &o, &out);
  if (status) *status = rc;
  return out ? out : gg_strdup("");
}

int proc_shell_run(const char *cmd) {
  spawn_opts o;
  spawn_opts_init(&o);
  o.shell = 1;
  o.echo = 0; /* callers print the line themselves */
  char *argv[2];
  argv[0] = (char *)cmd;
  argv[1] = 0;
  return proc_run(argv, &o, 0);
}
