/* gg — utilities: buffers, strings, utf8 widths, files, env, logging. */
#include "gg.h"

#if defined(__GNUC__) && !defined(__clang__)
/* path buffers are GG_PATH sized on both sides; gcc cannot see that */
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

#include <stdarg.h>

#if defined(__COSMOPOLITAN__)
#include <cosmo.h>
#endif

/* ------------------------------------------------------------------ */
/* sbuf                                                                */
/* ------------------------------------------------------------------ */

void sb_init(sbuf *b, size_t cap) {
  if (cap < 32) cap = 32;
  b->p = malloc(cap);
  b->p[0] = 0;
  b->n = 0;
  b->cap = cap;
}

void sb_free(sbuf *b) {
  free(b->p);
  b->p = 0;
  b->n = b->cap = 0;
}

void sb_clear(sbuf *b) {
  b->n = 0;
  if (b->p) b->p[0] = 0;
}

void sb_reserve(sbuf *b, size_t extra) {
  if (b->n + extra + 1 <= b->cap) return;
  size_t cap = b->cap ? b->cap : 32;
  while (cap < b->n + extra + 1) cap *= 2;
  b->p = realloc(b->p, cap);
  b->cap = cap;
}

void sb_addc(sbuf *b, char c) {
  sb_reserve(b, 1);
  b->p[b->n++] = c;
  b->p[b->n] = 0;
}

void sb_addn(sbuf *b, const char *s, size_t n) {
  if (!n) return;
  sb_reserve(b, n);
  memcpy(b->p + b->n, s, n);
  b->n += n;
  b->p[b->n] = 0;
}

void sb_adds(sbuf *b, const char *s) {
  if (s) sb_addn(b, s, strlen(s));
}

void sb_addf(sbuf *b, const char *fmt, ...) {
  va_list ap;
  char tmp[1024];
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if ((size_t)n < sizeof(tmp)) {
    sb_addn(b, tmp, (size_t)n);
  } else {
    char *big = malloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sb_addn(b, big, (size_t)n);
    free(big);
  }
}

void sb_reset_to(sbuf *b, size_t n) {
  if (n <= b->n) {
    b->n = n;
    if (b->p) b->p[n] = 0;
  }
}

char *gg_asprintf(const char *fmt, ...) {
  va_list ap;
  char tmp[1024];
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n < 0) return gg_strdup("");
  char *out = malloc((size_t)n + 1);
  if ((size_t)n < sizeof(tmp)) {
    memcpy(out, tmp, (size_t)n + 1);
  } else {
    va_start(ap, fmt);
    vsnprintf(out, (size_t)n + 1, fmt, ap);
    va_end(ap);
  }
  return out;
}

char *gg_strdup(const char *s) {
  if (!s) return 0;
  size_t n = strlen(s);
  char *p = malloc(n + 1);
  memcpy(p, s, n + 1);
  return p;
}

char *gg_strndup(const char *s, size_t n) {
  char *p = malloc(n + 1);
  memcpy(p, s, n);
  p[n] = 0;
  return p;
}

/* ------------------------------------------------------------------ */
/* strvec                                                              */
/* ------------------------------------------------------------------ */

void sv_init(strvec *s) {
  s->v = 0;
  s->n = s->cap = 0;
}

void sv_push(strvec *s, const char *item) {
  if (s->n + 2 > s->cap) {
    s->cap = s->cap ? s->cap * 2 : 8;
    s->v = realloc(s->v, (size_t)s->cap * sizeof(char *));
  }
  s->v[s->n++] = gg_strdup(item ? item : "");
  s->v[s->n] = 0;
}

void sv_pushf(strvec *s, const char *fmt, ...) {
  va_list ap;
  char tmp[2048];
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  sv_push(s, tmp);
}

void sv_reset(strvec *s) {
  for (int i = 0; i < s->n; i++) free(s->v[i]);
  s->n = 0;
  if (s->v) s->v[0] = 0;
}

void sv_free(strvec *s) {
  sv_reset(s);
  free(s->v);
  s->v = 0;
  s->cap = 0;
}

char **sv_argv(strvec *s) {
  if (!s->v) sv_push(s, "");
  return s->v;
}

/* ------------------------------------------------------------------ */
/* strings                                                             */
/* ------------------------------------------------------------------ */

int gg_streq(const char *a, const char *b) {
  if (!a || !b) return a == b;
  return strcmp(a, b) == 0;
}

int gg_startswith(const char *s, const char *prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

int gg_endswith(const char *s, const char *suffix) {
  size_t ls = strlen(s), lf = strlen(suffix);
  if (lf > ls) return 0;
  return memcmp(s + ls - lf, suffix, lf) == 0;
}

int gg_is_space(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

char *gg_trim(char *s) {
  while (*s && gg_is_space((unsigned char)*s)) s++;
  size_t n = strlen(s);
  while (n && gg_is_space((unsigned char)s[n - 1])) s[--n] = 0;
  return s;
}

int gg_utf8_decode(const char *s, size_t n, uint32_t *cp) {
  const unsigned char *u = (const unsigned char *)s;
  if (!n) {
    *cp = 0;
    return 0;
  }
  if (u[0] < 0x80) {
    *cp = u[0];
    return 1;
  }
  if ((u[0] & 0xE0) == 0xC0 && n >= 2 && (u[1] & 0xC0) == 0x80) {
    *cp = ((uint32_t)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
    return 2;
  }
  if ((u[0] & 0xF0) == 0xE0 && n >= 3 && (u[1] & 0xC0) == 0x80 &&
      (u[2] & 0xC0) == 0x80) {
    *cp = ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) |
          (u[2] & 0x3F);
    return 3;
  }
  if ((u[0] & 0xF8) == 0xF0 && n >= 4 && (u[1] & 0xC0) == 0x80 &&
      (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) {
    *cp = ((uint32_t)(u[0] & 0x07) << 18) | ((uint32_t)(u[1] & 0x3F) << 12) |
          ((uint32_t)(u[2] & 0x3F) << 6) | (u[3] & 0x3F);
    return 4;
  }
  *cp = u[0];
  return 1;
}

int gg_utf8_encode(uint32_t cp, char out[4]) {
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  } else if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  } else if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

size_t gg_utf8_prev(const char *s, size_t off) {
  if (off == 0) return 0;
  size_t i = off - 1;
  while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
  return i;
}

/* A compact approximation of wcwidth(3): good enough for CJK, emoji,
 * box drawing and latin. */
int gg_wcwidth(uint32_t cp) {
  if (cp == 0) return 0;
  if (cp < 32) return 0;
  if (cp < 0x7F) return 1;
  if (cp >= 0x0300 && cp <= 0x036F) return 0;   /* combining diacritics */
  if (cp >= 0x200B && cp <= 0x200F) return 0;   /* zero width */
  if (cp >= 0xFE00 && cp <= 0xFE0F) return 0;   /* variation selectors */
  if (cp >= 0xFE20 && cp <= 0xFE2F) return 0;
  if (cp >= 0x1AB0 && cp <= 0x1AFF) return 0;
  if (cp >= 0x1100 && cp <= 0x115F) return 2;   /* hangul jamo */
  if (cp == 0x2329 || cp == 0x232A) return 2;
  if (cp >= 0x2E80 && cp <= 0x303E) return 2;
  if (cp >= 0x3041 && cp <= 0x33FF) return 2;
  if (cp >= 0x3400 && cp <= 0x4DBF) return 2;
  if (cp >= 0x4E00 && cp <= 0x9FFF) return 2;   /* CJK unified */
  if (cp >= 0xA000 && cp <= 0xA4CF) return 2;
  if (cp >= 0xAC00 && cp <= 0xD7A3) return 2;   /* hangul syllables */
  if (cp >= 0xF900 && cp <= 0xFAFF) return 2;
  if (cp >= 0xFE30 && cp <= 0xFE6F) return 2;
  if (cp >= 0xFF00 && cp <= 0xFF60) return 2;   /* fullwidth forms */
  if (cp >= 0xFFE0 && cp <= 0xFFE6) return 2;
  if (cp >= 0x1F300 && cp <= 0x1F64F) return 2; /* emoji */
  if (cp >= 0x1F900 && cp <= 0x1F9FF) return 2;
  if (cp >= 0x20000 && cp <= 0x3FFFD) return 2;
  if (cp >= 0x2500 && cp <= 0x257F) return 1;   /* box drawing */
  return 1;
}

int gg_width(const char *s) {
  int w = 0;
  size_t i = 0, n = s ? strlen(s) : 0;
  while (i < n) {
    unsigned char c = (unsigned char)s[i];
    if (c == 0x1B) { /* skip CSI / other escapes */
      i++;
      if (i < n && (s[i] == '[' || s[i] == ']' || s[i] == '(')) {
        char kind = s[i++];
        if (kind == '[') {
          while (i < n && !((unsigned char)s[i] >= 0x40 && (unsigned char)s[i] <= 0x7E))
            i++;
          if (i < n) i++;
        } else {
          while (i < n && s[i] != 7) i++;
          if (i < n) i++;
        }
      }
      continue;
    }
    if (c == '\t') {
      w += 4;
      i++;
      continue;
    }
    if (c < 0x20) {
      i++;
      continue;
    }
    uint32_t cp;
    int k = gg_utf8_decode(s + i, n - i, &cp);
    w += gg_wcwidth(cp);
    i += (size_t)k;
  }
  return w;
}

/* ------------------------------------------------------------------ */
/* filesystem                                                          */
/* ------------------------------------------------------------------ */

static char g_home[GG_PATH];
static char g_dir[GG_PATH];
static char g_modules[GG_PATH];
static char g_bindir[GG_PATH];
static char g_config[GG_PATH];
static int g_paths_ready;

void gg_paths_init(void) {
  if (g_paths_ready) return;
  g_paths_ready = 1;

  const char *h = getenv("GG_HOME_HOME");
  if (!h || !*h) h = getenv("HOME");
  if (!h || !*h) h = getenv("USERPROFILE");
  if (!h || !*h) h = "/";
  snprintf(g_home, sizeof(g_home), "%s", h);

  const char *d = getenv("GG_DIR");
  if (d && *d) {
    snprintf(g_dir, sizeof(g_dir), "%s", d);
  } else {
    snprintf(g_dir, sizeof(g_dir), "%s/%s", g_home, GG_DIR_NAME);
  }
  snprintf(g_modules, sizeof(g_modules), "%s/modules", g_dir);
  snprintf(g_bindir, sizeof(g_bindir), "%s/bin", g_dir);
  snprintf(g_config, sizeof(g_config), "%s/config.lua", g_dir);
}

const char *gg_home(void) {
  gg_paths_init();
  return g_home;
}
const char *gg_dir(void) {
  gg_paths_init();
  return g_dir;
}
const char *gg_modules_dir(void) {
  gg_paths_init();
  return g_modules;
}
const char *gg_bin_dir(void) {
  gg_paths_init();
  return g_bindir;
}
const char *gg_config_path(void) {
  gg_paths_init();
  return g_config;
}

int gg_exists(const char *path) {
  struct stat st;
  return path && *path && stat(path, &st) == 0;
}

int gg_is_dir(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int gg_is_file(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int gg_mkdir_p(const char *path) {
  char tmp[PATH_MAX];
  size_t n = strlen(path);
  if (n >= sizeof(tmp)) return -1;
  memcpy(tmp, path, n + 1);
  for (size_t i = 1; i <= n; i++) {
    if (tmp[i] == '/' || tmp[i] == '\\' || tmp[i] == 0) {
      char save = tmp[i];
      tmp[i] = 0;
      if (*tmp && !gg_is_dir(tmp)) {
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
          /* keep going anyway, the final check reports failure */
        }
      }
      tmp[i] = save;
    }
  }
  return gg_is_dir(path) ? 0 : -1;
}

char *gg_read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  sbuf b;
  sb_init(&b, 4096);
  char tmp[8192];
  size_t n;
  while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) sb_addn(&b, tmp, n);
  fclose(f);
  if (len) *len = b.n;
  return b.p;
}

int gg_write_file(const char *path, const char *data, size_t len) {
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  if (len && fwrite(data, 1, len, f) != len) {
    fclose(f);
    return -1;
  }
  fclose(f);
  return 0;
}

int gg_copy_file(const char *src, const char *dst) {
  size_t n = 0;
  char *data = gg_read_file(src, &n);
  if (!data) return -1;
  int rc = gg_write_file(dst, data, n);
  free(data);
  return rc;
}

int gg_chmod_x(const char *path) {
#if GG_WINDOWS
  (void)path;
  return 0;
#else
  struct stat st;
  if (stat(path, &st) != 0) return -1;
  return chmod(path, st.st_mode | 0755);
#endif
}

char *gg_join(const char *a, const char *b) {
  if (!a || !*a) return gg_strdup(b ? b : "");
  if (!b || !*b) return gg_strdup(a);
  if (b[0] == '/' || (b[0] && b[1] == ':')) return gg_strdup(b);
  size_t la = strlen(a);
  while (la > 1 && (a[la - 1] == '/' || a[la - 1] == '\\')) la--;
  return gg_asprintf("%.*s/%s", (int)la, a, b);
}

char *gg_path_abs(const char *path) {
  if (!path || !*path) return gg_strdup(gg_dir());
  if (path[0] == '~') {
    return gg_asprintf("%s%s", gg_home(), path + 1);
  }
  if (path[0] == '/') return gg_strdup(path);
#if GG_WINDOWS
  if (path[1] == ':') return gg_strdup(path);
#endif
  char cwd[PATH_MAX];
  if (!getcwd(cwd, sizeof(cwd))) snprintf(cwd, sizeof(cwd), ".");
  return gg_join(cwd, path);
}

const char *gg_basename(const char *path) {
  const char *p = path + strlen(path);
  while (p > path && p[-1] != '/' && p[-1] != '\\') p--;
  return p;
}

char *gg_dirname_of(const char *path) {
  size_t n = strlen(path);
  while (n > 1 && path[n - 1] != '/' && path[n - 1] != '\\') n--;
  while (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\')) n--;
  if (n == 0) return gg_strdup(".");
  return gg_strndup(path, n);
}

char *gg_ext_of(const char *path) {
  const char *b = gg_basename(path);
  const char *dot = strrchr(b, '.');
  return gg_strdup(dot ? dot + 1 : "");
}

/* tiny shell-style glob: * and ? only, no character classes */
int gg_glob_match(const char *pat, const char *str) {
  if (!pat || !str) return 0;
  if (!*pat) return !*str;
  if (*pat == '*') {
    for (const char *t = str;; t++) {
      if (gg_glob_match(pat + 1, t)) return 1;
      if (!*t) return 0;
    }
  }
  if (*pat == '?') return *str && gg_glob_match(pat + 1, str + 1);
  if (*pat != *str) return 0;
  return gg_glob_match(pat + 1, str + 1);
}

const char *gg_exe_path(void) {
  static char buf[PATH_MAX];
  if (buf[0]) return buf;
#if defined(__COSMOPOLITAN__)
  snprintf(buf, sizeof(buf), "%s", GetProgramExecutableName());
#elif defined(__APPLE__)
  {
    uint32_t size = sizeof(buf);
    extern int _NSGetExecutablePath(char *, uint32_t *);
    if (_NSGetExecutablePath(buf, &size) != 0) snprintf(buf, sizeof(buf), "gg");
  }
#elif GG_WINDOWS
  snprintf(buf, sizeof(buf), "gg");
#else
  {
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) buf[n] = 0;
    else snprintf(buf, sizeof(buf), "gg");
  }
#endif
  return buf;
}

/* ------------------------------------------------------------------ */
/* PATH search (cached)                                                */
/* ------------------------------------------------------------------ */

typedef struct {
  char *name;
  char *path;
} which_entry;

static which_entry *g_which;
static int g_which_n, g_which_cap;

const char *gg_which(const char *name) {
  if (!name || !*name) return 0;
  for (int i = 0; i < g_which_n; i++)
    if (gg_streq(g_which[i].name, name)) return g_which[i].path;
  char *found = 0;
  const char *path = getenv("PATH");
  if (path && *path) {
#if GG_WINDOWS
    /* Windows PATH entries are ';' separated, but drive letters contain
     * ':' — only fall back to ':' when no ';' is present at all. */
    const char *seps = strchr(path, ';') ? ";" : ":";
#else
    const char *seps = ":";
#endif
    const char *p = path;
    while (*p && !found) {
      const char *sep = strpbrk(p, seps);
      size_t n = sep ? (size_t)(sep - p) : strlen(p);
      if (n) {
        char *dir = gg_strndup(p, n);
        char *cand = gg_asprintf("%s/%s", dir[0] ? dir : ".", name);
        if (gg_is_file(cand) && access(cand, X_OK) == 0) {
          found = cand;
        } else {
          free(cand);
#if GG_WINDOWS
          /* try windows executable extensions */
          const char *exts[] = {".exe", ".com", ".bat", ".cmd", ".ps1", 0};
          for (int e = 0; exts[e] && !found; e++) {
            char *c2 = gg_asprintf("%s/%s%s", dir, name, exts[e]);
            if (gg_is_file(c2)) found = c2;
            else free(c2);
          }
#endif
        }
        free(dir);
      }
      p = sep ? sep + 1 : p + strlen(p);
    }
  }
  /* remember the result (including misses) */
  if (g_which_n + 1 > g_which_cap) {
    g_which_cap = g_which_cap ? g_which_cap * 2 : 32;
    g_which = realloc(g_which, (size_t)g_which_cap * sizeof(which_entry));
  }
  g_which[g_which_n].name = gg_strdup(name);
  g_which[g_which_n].path = found;
  g_which_n++;
  return found;
}

int gg_have(const char *name) { return gg_which(name) != 0; }

/* ------------------------------------------------------------------ */
/* logging                                                             */
/* ------------------------------------------------------------------ */

static int g_no_color = -1;

static int use_color(void) {
  if (g_no_color < 0) {
    const char *nc = getenv("NO_COLOR");
    const char *term = getenv("TERM");
    g_no_color = (nc && *nc) || (term && gg_streq(term, "dumb")) || !isatty(1);
    if (T.interactive) g_no_color = 0;
  }
  return !g_no_color;
}

const char *c_reset(void) { return use_color() ? "\033[0m" : ""; }
const char *c_bold(void) { return use_color() ? "\033[1m" : ""; }
const char *c_dim(void) { return use_color() ? "\033[38;5;245m" : ""; }
const char *c_ok(void) { return use_color() ? "\033[38;5;42m" : ""; }
const char *c_err(void) { return use_color() ? "\033[38;5;203m" : ""; }
const char *c_warn(void) { return use_color() ? "\033[38;5;214m" : ""; }
const char *c_accent(void) { return use_color() ? "\033[38;5;45m" : ""; }

static void vlogf(const char *prefix, const char *color, const char *fmt,
                  va_list ap) {
  fflush(stdout);
  fprintf(stderr, "%s%s%s", color, prefix, c_reset());
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

void gg_info(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlogf("", "", fmt, ap);
  va_end(ap);
}

void gg_warn(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlogf("warning: ", c_warn(), fmt, ap);
  va_end(ap);
}

void gg_error(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlogf("error: ", c_err(), fmt, ap);
  va_end(ap);
}

void gg_ok(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlogf("", c_ok(), fmt, ap);
  va_end(ap);
}

void gg_die(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlogf("error: ", c_err(), fmt, ap);
  va_end(ap);
  exit(1);
}

void print_kv(const char *key, const char *fmt, ...) {
  va_list ap;
  printf("%s%-14s%s ", c_dim(), key, c_reset());
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  putchar('\n');
}

int arg_is_flag(const char *s, const char *shrt, const char *lng) {
  return (shrt && gg_streq(s, shrt)) || (lng && gg_streq(s, lng));
}

void gg_echo_line(const char *line) {
  fflush(stdout);
  fprintf(stderr, "%s$%s %s\n", c_accent(), c_reset(), line ? line : "");
  fflush(stderr);
}

/* ------------------------------------------------------------------ */
/* language                                                            */
/* ------------------------------------------------------------------ */

static int g_zh = -1;

int gg_lang_zh(void) { return g_zh == 1; }

void gg_lang_set(int zh) { g_zh = zh ? 1 : 0; }

static int lang_from_env(void) {
  const char *l = getenv("GG_LANG");
  if (l && *l) return gg_startswith(l, "zh") || gg_startswith(l, "cn");
  const char *lc = getenv("LC_ALL");
  if (!lc || !*lc) lc = getenv("LC_MESSAGES");
  if (!lc || !*lc) lc = getenv("LANG");
  if (lc && (gg_startswith(lc, "zh") || strstr(lc, "zh_CN") || strstr(lc, "zh_TW")))
    return 1;
  return 0;
}

const char *gg_tr(const char *en, const char *zh) {
  if (g_zh < 0) g_zh = lang_from_env();
  return g_zh ? zh : en;
}
