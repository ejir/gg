/* gg — a cosmopoli-tan multitool: one APE binary, Lua powered, TUI on top.
 *
 * Copyright (c) 2026 gg authors. ISC licensed (see LICENSE).
 *
 * This header is shared by every translation unit of gg.  Keep it boring:
 * gg targets C11, builds with cosmocc (APE) and with any regular cc.
 */
#ifndef GG_H_
#define GG_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifdef _WIN32
#define GG_WINDOWS 1
#else
#define GG_WINDOWS 0
#endif

#if defined(__APPLE__)
#define GG_MACOS 1
#else
#define GG_MACOS 0
#endif

#if defined(__linux__) && !defined(__COSMOPOLITAN__)
#define GG_LINUX 1
#else
#define GG_LINUX 0
#endif

#define GG_VERSION "0.1.0"
#define GG_URL "https://github.com/ejir/gg"
#define GG_DIR_NAME ".gg"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* a little headroom so the compiler stops worrying about snprintf of
 * path-prefixes into path-sized buffers */
#define GG_PATH (PATH_MAX + 64)

#define GG_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------------------ */
/* growable byte buffer                                                */
/* ------------------------------------------------------------------ */

typedef struct {
  char *p;
  size_t n;
  size_t cap;
} sbuf;

void sb_init(sbuf *b, size_t cap);
void sb_free(sbuf *b);
void sb_clear(sbuf *b);
void sb_reserve(sbuf *b, size_t extra);
void sb_addc(sbuf *b, char c);
void sb_adds(sbuf *b, const char *s);
void sb_addn(sbuf *b, const char *s, size_t n);
void sb_addf(sbuf *b, const char *fmt, ...);
void sb_reset_to(sbuf *b, size_t n);
/* printf into a fresh malloc'd string */
char *gg_asprintf(const char *fmt, ...);
char *gg_strdup(const char *s);
char *gg_strndup(const char *s, size_t n);

/* ------------------------------------------------------------------ */
/* string vectors                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
  char **v;
  int n;
  int cap;
} strvec;

void sv_init(strvec *s);
void sv_push(strvec *s, const char *item);
void sv_pushf(strvec *s, const char *fmt, ...);
void sv_free(strvec *s);
void sv_reset(strvec *s);
char **sv_argv(strvec *s); /* NULL terminated view, points into s->v */

/* ------------------------------------------------------------------ */
/* misc helpers                                                        */
/* ------------------------------------------------------------------ */

int gg_streq(const char *a, const char *b);
int gg_startswith(const char *s, const char *prefix);
int gg_endswith(const char *s, const char *suffix);
char *gg_trim(char *s);
int gg_is_space(int c);

/* Visible width of a UTF-8 string (tabs, ANSI CSI sequences are ignored). */
int gg_width(const char *s);
/* Width of one codepoint: 0 for combining, 2 for CJK/emoji, else 1. */
int gg_wcwidth(uint32_t cp);
/* Decode one UTF-8 codepoint; returns bytes consumed. */
int gg_utf8_decode(const char *s, size_t n, uint32_t *cp);
int gg_utf8_encode(uint32_t cp, char out[4]);
/* byte offset of the previous UTF-8 codepoint start */
size_t gg_utf8_prev(const char *s, size_t off);

/* ------------------------------------------------------------------ */
/* filesystem / environment                                            */
/* ------------------------------------------------------------------ */

const char *gg_home(void);       /* $GG_HOME_HOME -> $HOME -> %USERPROFILE% */
const char *gg_dir(void);        /* $GG_DIR or ~/.gg */
const char *gg_modules_dir(void);
const char *gg_bin_dir(void);
const char *gg_config_path(void);
void gg_paths_init(void);        /* computes the cached dirs above */

int gg_exists(const char *path);
int gg_is_dir(const char *path);
int gg_is_file(const char *path);
int gg_mkdir_p(const char *path);
char *gg_read_file(const char *path, size_t *len);
int gg_write_file(const char *path, const char *data, size_t len);
int gg_copy_file(const char *src, const char *dst);
int gg_chmod_x(const char *path);
char *gg_join(const char *a, const char *b);
char *gg_path_abs(const char *path);
const char *gg_basename(const char *path); /* returns pointer into path */
char *gg_dirname_of(const char *path);
char *gg_ext_of(const char *path);

const char *gg_exe_path(void);         /* path to the running binary */
const char *gg_which(const char *name); /* cached, returns "" when missing */
int gg_have(const char *name);          /* gg_which != NULL */

/* ------------------------------------------------------------------ */
/* terminal / TUI                                                      */
/* ------------------------------------------------------------------ */

enum {
  KEY_NONE = -1,
  KEY_ESC = 27,
  KEY_ENTER = '\r',
  KEY_TAB = '\t',
  KEY_BACKSPACE = 127,
  KEY_UP = 0x100,
  KEY_DOWN,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_HOME,
  KEY_END,
  KEY_PGUP,
  KEY_PGDN,
  KEY_DEL,
  KEY_CTRL_UP,
  KEY_CTRL_DOWN,
  KEY_CTRL_LEFT,
  KEY_CTRL_RIGHT,
  KEY_F1 = 0x120,
  KEY_CTRL_SHIFT_HOME,
  KEY_SHIFT_TAB,
};

typedef struct {
  int tty_out;   /* stdout is a terminal            */
  int tty_in;    /* stdin is a terminal             */
  int interactive;
  int in_alt;    /* alternate screen currently used */
  int raw;       /* raw mode currently on           */
  int w, h;
  int utf8;
  int no_color;
  struct termios saved;
  int saved_ok;
} tui_state;

extern tui_state T;
extern sbuf G_FRAME; /* scratch render buffer */

void tui_probe(void);      /* fill T.*, decide interactivity  */
int tui_init(void);        /* probe + install atexit restore  */
void tui_restore(void);
void tui_enter(void);      /* alt screen + raw mode + cursor  */
void tui_leave(void);
void tui_size(void);       /* refresh T.w/T.h                 */
void tui_flush(sbuf *b);   /* write buffer to stdout          */
void tui_clear(sbuf *b);
void tui_at(sbuf *b, int row, int col);
void tui_repeat(sbuf *b, const char *s, int times);
void tui_hline(sbuf *b, int row, int col, int len, const char *style);
int tui_key(int timeout_ms);
int tui_keys_pending(void);
void tui_bell(void);

/* styled text helpers (respect $NO_COLOR and dumb terminals) */
const char *tui_style(const char *style);
#define ST_RESET "\033[0m"
#define ST_BOLD "\033[1m"
#define ST_DIM "\033[2m"
#define ST_REV "\033[7m"
#define ST_ACCENT "\033[38;5;45m"
#define ST_OK "\033[38;5;42m"
#define ST_WARN "\033[38;5;214m"
#define ST_ERR "\033[38;5;203m"
#define ST_MUTED "\033[38;5;245m"
#define ST_TITLE "\033[1;38;5;51m"
#define ST_SEL "\033[7;38;5;51m"

typedef struct {
  int kind;   /* 0 = text, 1 = bool, 2 = choice */
  const char *name;
  const char *label;
  const char *help;
  char *value;
  const char **choices;
  int nchoices;
} tui_field;

typedef struct {
  const char *title;
  const char *subtitle;
  const char *footer;
  const char **items;
  int n;
  int sel;
  int scroll;
  char filter[192];
  int filter_on;
  const char *status;
} tui_menu;

void tui_menu_init(tui_menu *m, const char *title, const char **items, int n);
void tui_menu_render(tui_menu *m);
int tui_menu_key(tui_menu *m, int key);
/* Convenience: interactive loop over a menu. Return selected index or -1.
 * Other keys are reported back through *other_key (0 when unused). */
int tui_menu_run(tui_menu *m, int *other_key);

int tui_prompt(const char *title, const char *label, char *buf, size_t cap,
               const char *help);
int tui_confirm(const char *title, const char *msg, int def);
int tui_select(const char *title, const char *msg, const char **items, int n);
void tui_message(const char *title, const char *body);
char *tui_textbox(const char *title, const char *filename, const char *text);
int tui_form(const char *title, const char *subtitle, tui_field *fields, int n);

void tui_progress_begin(const char *title, const char *subtitle);
void tui_progress_set(double pct, const char *msg);
void tui_progress_log(const char *line);
void tui_progress_end(int ok, const char *msg);

/* ------------------------------------------------------------------ */
/* processes                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
  int capture;                  /* collect stdout+stderr            */
  int shell;                    /* run through the system shell     */
  int sudo;                     /* prefix with sudo (if not root)   */
  int quiet;                    /* do not echo the command          */
  int echo;                     /* print "+ cmd" before running     */
  const char *cwd;
  const char *input;            /* text piped into stdin            */
  char **env_extra;             /* NULL terminated KEY=VALUE list   */
  void (*on_line)(const char *line, void *ud); /* streaming callback */
  void *ud;
} spawn_opts;

void spawn_opts_init(spawn_opts *o);
/* Returns exit status (0 ok, -1 spawn failure). *out is malloc'd when
 * o->capture is set. */
int proc_run(char **argv, spawn_opts *o, char **out);
int proc_run_interactive(char **argv, int sudo);
int proc_exec(char **argv, int sudo); /* never returns on success */
char *proc_shell_capture(const char *cmd, int *status);
int proc_shell_run(const char *cmd); /* inherits stdio */
char **proc_sudo_prefix(char **argv, strvec *storage);
int gg_is_root(void);
/* Quote for a shell: "a b" -> 'a b' */
char *gg_shell_quote(const char *s);
char *gg_join_argv(char **argv);
void proc_echo_cmd(char **argv);
void gg_echo_line(const char *line); /* prints "  $ <line>" */

/* ------------------------------------------------------------------ */
/* registry of user commands (gg ls / gg <name>)                       */
/* ------------------------------------------------------------------ */

typedef struct {
  char *name;
  char *cmd;
  char *desc;
  char *source; /* "user" | "builtin" */
} reg_entry;

void reg_load(void);
void reg_reload(void);
int reg_count(void);
reg_entry *reg_at(int i);
reg_entry *reg_find(const char *name);
int reg_add(const char *name, const char *cmd, const char *desc);
int reg_remove(const char *name);
const char *reg_path(void);
void reg_log(const char *name, const char *cmdline);
int reg_log_recent(int n, char **lines);

/* ------------------------------------------------------------------ */
/* modules (lua)                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
  char *name;
  char *title;
  char *desc;
  char *path;    /* NULL when built into the binary */
  int builtin;
  int is_tui;
} module_info;

void modules_scan(void);
void modules_rescan(void); /* forget the cache, re-read the module dir */
int modules_count(void);
module_info *modules_at(int i);
module_info *modules_find(const char *name);
char *module_path_of(const char *name);

#define GG_CUR_SHOW "\033[?25h"
#define GG_CUR_HIDE "\033[?25l"
#define GG_AT(row, col) "\033[" #row ";" #col "H"
void gg_highlight_lua(sbuf *out, const char *line, int col);
void gg_pause_key(const char *msg);
int run_shell_paused(const char *cmd);

/* ------------------------------------------------------------------ */
/* lua bridge                                                          */
/* ------------------------------------------------------------------ */

struct lua_State;
extern struct lua_State *L;

int lua_open_runtime(void);
int lua_close_runtime(void);
int lua_run_file(const char *path, int argc, char **argv);
int lua_run_string(const char *code, const char *chunkname);
int lua_run_module(const char *name, int argc, char **argv, int force_tui);
int lua_check_syntax(const char *path, char **err);
int lua_eval_expr_string(const char *expr, char **out);

/* embedded examples / builtin modules (generated by tools/embed.sh) */
typedef struct {
  const char *name;
  const char *data;
  unsigned int len;
} embedded_file;
extern const embedded_file GG_EMBEDDED[];
extern const int GG_EMBEDDED_COUNT;
const char *gg_embedded_lookup(const char *name, unsigned int *len);

/* ------------------------------------------------------------------ */
/* builtin commands                                                    */
/* ------------------------------------------------------------------ */

int cmd_active(int argc, char **argv);
int cmd_ls(int argc, char **argv);
int cmd_add(int argc, char **argv);
int cmd_rm(int argc, char **argv);
int cmd_show(int argc, char **argv);
int cmd_edit(int argc, char **argv);
int cmd_run(int argc, char **argv);
int cmd_init(int argc, char **argv);
int cmd_doctor(int argc, char **argv);
int cmd_help(int argc, char **argv);
int cmd_tui(int argc, char **argv);
int cmd_config(int argc, char **argv);
int cmd_upgrade(int argc, char **argv);
int cmd_modules(int argc, char **argv);
int cmd_link(int argc, char **argv);
int cmd_unlink(int argc, char **argv);
int cmd_exec(int argc, char **argv);
int cmd_dashboard(void);
int cmd_version(void);

/* localization: pick zh when the user asks for it */
int gg_lang_zh(void);
const char *gg_tr(const char *en, const char *zh);
void gg_lang_set(int zh);

/* generic arg utilities for builtins */
int arg_is_flag(const char *s, const char *shrt, const char *lng);
void print_kv(const char *key, const char *fmt, ...);
void gg_error(const char *fmt, ...);
void gg_warn(const char *fmt, ...);
void gg_info(const char *fmt, ...);
void gg_ok(const char *fmt, ...);
void gg_die(const char *fmt, ...);

/* colors for plain (non-TUI) output */
const char *c_ok(void);
const char *c_err(void);
const char *c_warn(void);
const char *c_dim(void);
const char *c_bold(void);
const char *c_accent(void);
const char *c_reset(void);

#endif /* GG_H_ */
