/* gg — the little command registry (~/.gg/commands.tsv) and run history. */
#include "gg.h"

static reg_entry *g_reg;
static int g_reg_n, g_reg_cap;
static int g_loaded;

static char *g_reg_path;
static char *g_hist_path;

const char *reg_path(void) {
  if (!g_reg_path) g_reg_path = gg_join(gg_dir(), "commands.tsv");
  return g_reg_path;
}

static const char *hist_path(void) {
  if (!g_hist_path) g_hist_path = gg_join(gg_dir(), "history.tsv");
  return g_hist_path;
}

static void reg_push(const char *name, const char *cmd, const char *desc,
                     const char *src) {
  if (g_reg_n + 1 > g_reg_cap) {
    g_reg_cap = g_reg_cap ? g_reg_cap * 2 : 32;
    g_reg = realloc(g_reg, (size_t)g_reg_cap * sizeof(reg_entry));
  }
  reg_entry *e = &g_reg[g_reg_n++];
  e->name = gg_strdup(name);
  e->cmd = gg_strdup(cmd);
  e->desc = gg_strdup(desc ? desc : "");
  e->source = gg_strdup(src ? src : "user");
}

void reg_reload(void) {
  for (int i = 0; i < g_reg_n; i++) {
    free(g_reg[i].name);
    free(g_reg[i].cmd);
    free(g_reg[i].desc);
    free(g_reg[i].source);
  }
  g_reg_n = 0;
  g_loaded = 1;
  size_t len = 0;
  char *data = gg_read_file(reg_path(), &len);
  if (!data) return;
  char *p = data;
  while (p && *p) {
    char *nl = strchr(p, '\n');
    if (nl) *nl = 0;
    char *line = gg_trim(p);
    if (*line && *line != '#') {
      char *t1 = strchr(line, '\t');
      if (t1) {
        *t1 = 0;
        char *t2 = strchr(t1 + 1, '\t');
        char *desc = (char *)"";
        if (t2) {
          *t2 = 0;
          desc = t2 + 1;
        }
        reg_push(gg_trim(line), gg_trim(t1 + 1), gg_trim(desc), "user");
      } else {
        /* legacy "name = command" form */
        char *eq = strchr(line, '=');
        if (eq) {
          *eq = 0;
          reg_push(gg_trim(line), gg_trim(eq + 1), "", "user");
        }
      }
    }
    p = nl ? nl + 1 : 0;
  }
  free(data);
}

void reg_load(void) {
  if (!g_loaded) reg_reload();
}

static int reg_save(void) {
  gg_mkdir_p(gg_dir());
  sbuf b;
  sb_init(&b, 512);
  sb_adds(&b, "# gg commands — name<TAB>command<TAB>description\n");
  sb_adds(&b, "# managed by `gg add`, `gg rm` and the interactive dashboard.\n");
  for (int i = 0; i < g_reg_n; i++) {
    sb_addf(&b, "%s\t%s\t%s\n", g_reg[i].name, g_reg[i].cmd, g_reg[i].desc);
  }
  int rc = gg_write_file(reg_path(), b.p, b.n);
  sb_free(&b);
  return rc;
}

int reg_count(void) {
  reg_load();
  return g_reg_n;
}

reg_entry *reg_at(int i) {
  reg_load();
  if (i < 0 || i >= g_reg_n) return 0;
  return &g_reg[i];
}

reg_entry *reg_find(const char *name) {
  reg_load();
  for (int i = 0; i < g_reg_n; i++)
    if (gg_streq(g_reg[i].name, name)) return &g_reg[i];
  return 0;
}

int reg_add(const char *name, const char *cmd, const char *desc) {
  reg_load();
  reg_entry *e = reg_find(name);
  if (e) {
    free(e->cmd);
    free(e->desc);
    e->cmd = gg_strdup(cmd);
    e->desc = gg_strdup(desc ? desc : "");
  } else {
    reg_push(name, cmd, desc, "user");
  }
  return reg_save();
}

int reg_remove(const char *name) {
  reg_load();
  for (int i = 0; i < g_reg_n; i++) {
    if (gg_streq(g_reg[i].name, name)) {
      free(g_reg[i].name);
      free(g_reg[i].cmd);
      free(g_reg[i].desc);
      free(g_reg[i].source);
      memmove(&g_reg[i], &g_reg[i + 1],
              (size_t)(g_reg_n - i - 1) * sizeof(reg_entry));
      g_reg_n--;
      return reg_save();
    }
  }
  return -1;
}

/* ------------------------------------------------------------------ */
/* history                                                             */
/* ------------------------------------------------------------------ */

void reg_log(const char *name, const char *cmdline) {
  gg_mkdir_p(gg_dir());
  FILE *f = fopen(hist_path(), "a");
  if (!f) return;
  time_t now = time(0);
  struct tm tmv;
  struct tm *t = localtime(&now);
  if (t) tmv = *t;
  else memset(&tmv, 0, sizeof(tmv));
  fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d\t%s\t%s\n", tmv.tm_year + 1900,
          tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
          name ? name : "", cmdline ? cmdline : "");
  fclose(f);
}

int reg_log_recent(int n, char **lines) {
  size_t len = 0;
  char *data = gg_read_file(hist_path(), &len);
  if (!data) return 0;
  /* collect line starts */
  strvec v;
  sv_init(&v);
  char *p = data;
  while (p && *p) {
    char *nl = strchr(p, '\n');
    if (nl) *nl = 0;
    if (*p) sv_push(&v, p);
    p = nl ? nl + 1 : 0;
  }
  int count = 0;
  for (int i = v.n - 1; i >= 0 && count < n; i--) {
    lines[count++] = gg_strdup(v.v[i]);
  }
  sv_free(&v);
  free(data);
  return count;
}
