/* gg — module discovery: lua files in ~/.gg/modules plus the modules
 * compiled into the executable (see examples/, embedded by tools/embed.sh). */
#include "gg.h"

static module_info *g_mods;
static int g_n, g_cap;
static int g_scanned;

/* `--[[gg ...]]`-style headers: pull M.title / M.desc / M.tui out of the
 * source text without running it. */
static void mod_sniff(module_info *m, const char *text) {
  if (!text) return;
  m->is_tui = strstr(text, "function M.tui") != 0 || strstr(text, "M.tui =") != 0;
  const char *keys[2] = {"M.title", "M.desc"};
  char **slots[2] = {&m->title, &m->desc};
  for (int k = 0; k < 2; k++) {
    const char *hit = strstr(text, keys[k]);
    if (!hit) continue;
    const char *q = strchr(hit, '"');
    const char *q2 = strchr(hit, '\'');
    if (!q || (q2 && q2 < q)) q = q2;
    if (!q) continue;
    const char *e = strchr(q + 1, *q);
    if (e) *slots[k] = gg_strndup(q + 1, (size_t)(e - q - 1));
  }
}

static void mod_push(const char *name, const char *path, int builtin,
                     const char *source, size_t srclen) {
  if (g_n + 1 > g_cap) {
    g_cap = g_cap ? g_cap * 2 : 32;
    g_mods = realloc(g_mods, (size_t)g_cap * sizeof(module_info));
  }
  module_info *m = &g_mods[g_n++];
  memset(m, 0, sizeof(*m));
  m->name = gg_strdup(name);
  m->path = path ? gg_strdup(path) : 0;
  m->builtin = builtin;
  m->title = 0;
  m->desc = 0;
  /* cheap metadata sniffing: read `--[[gg title: ...]]` style headers */
  char *text = 0;
  if (source) {
    text = gg_strndup(source, srclen);
  } else if (path) {
    size_t len = 0;
    text = gg_read_file(path, &len);
  }
  if (text) {
    mod_sniff(m, text);
    free(text);
  }
  if (!m->title) m->title = gg_strdup(m->name);
  if (!m->desc) m->desc = gg_strdup("");
}

static int mod_less_raw(const void *a, const void *b) {
  const module_info *x = a, *y = b;
  if (x->builtin != y->builtin) return x->builtin ? 1 : -1;
  return strcmp(x->name, y->name);
}

void modules_scan(void) {
  if (g_scanned) return;
  g_scanned = 1;
  /* 1. built-in modules */
  for (int i = 0; i < GG_EMBEDDED_COUNT; i++) {
    const char *nm = GG_EMBEDDED[i].name;
    if (gg_startswith(nm, "mod_") && gg_endswith(nm, ".lua")) {
      char *shortname = gg_strndup(nm + 4, strlen(nm) - 8);
      mod_push(shortname, 0, 1, GG_EMBEDDED[i].data, GG_EMBEDDED[i].len);
      free(shortname);
    }
  }
  /* 2. user modules (shadow built-ins with the same name) */
  const char *dir = gg_modules_dir();
  DIR *d = opendir(dir);
  if (d) {
    struct dirent *de;
    while ((de = readdir(d))) {
      if (!gg_endswith(de->d_name, ".lua")) continue;
      char *full = gg_join(dir, de->d_name);
      if (!gg_is_file(full)) {
        free(full);
        continue;
      }
      char *name = gg_strndup(de->d_name, strlen(de->d_name) - 4);
      int replaced = 0;
      for (int i = 0; i < g_n; i++) {
        if (gg_streq(g_mods[i].name, name) && g_mods[i].builtin) {
          free(g_mods[i].title);
          free(g_mods[i].desc);
          free(g_mods[i].path);
          g_mods[i].path = gg_strdup(full); /* `full` is freed below */
          g_mods[i].builtin = 0;
          g_mods[i].title = 0;
          g_mods[i].desc = 0;
          /* re-sniff metadata from the user copy */
          size_t len = 0;
          char *text = gg_read_file(full, &len);
          if (text) {
            mod_sniff(&g_mods[i], text);
            free(text);
          }
          if (!g_mods[i].title) g_mods[i].title = gg_strdup(name);
          if (!g_mods[i].desc) g_mods[i].desc = gg_strdup("");
          replaced = 1;
          break;
        }
      }
      if (!replaced) {
        mod_push(name, full, 0, 0, 0);
        free(name);
      }
      free(full);
    }
    closedir(d);
  }
  qsort(g_mods, (size_t)g_n, sizeof(module_info), mod_less_raw);
}

/* drop the cached list and read the module directory again */
void modules_rescan(void) {
  for (int i = 0; i < g_n; i++) {
    free(g_mods[i].name);
    free(g_mods[i].path);
    free(g_mods[i].title);
    free(g_mods[i].desc);
  }
  g_n = 0;
  g_scanned = 0;
  modules_scan();
}

int modules_count(void) {
  modules_scan();
  return g_n;
}

module_info *modules_at(int i) {
  modules_scan();
  if (i < 0 || i >= g_n) return 0;
  return &g_mods[i];
}

module_info *modules_find(const char *name) {
  modules_scan();
  for (int i = 0; i < g_n; i++)
    if (gg_streq(g_mods[i].name, name)) return &g_mods[i];
  return 0;
}

const char *gg_embedded_lookup(const char *name, unsigned int *len) {
  for (int i = 0; i < GG_EMBEDDED_COUNT; i++) {
    if (gg_streq(GG_EMBEDDED[i].name, name)) {
      if (len) *len = GG_EMBEDDED[i].len;
      return GG_EMBEDDED[i].data;
    }
  }
  return 0;
}

char *module_path_of(const char *name) {
  module_info *m = modules_find(name);
  if (!m) return 0;
  if (m->path) return gg_strdup(m->path);
  /* built-in module: expose it on disk so `gg edit` works */
  char key[256];
  snprintf(key, sizeof(key), "mod_%s.lua", name);
  unsigned int len = 0;
  const char *src = gg_embedded_lookup(key, &len);
  if (!src) return 0;
  gg_mkdir_p(gg_modules_dir());
  char *leaf = gg_asprintf("%s.lua", name);
  char *path = gg_join(gg_modules_dir(), leaf);
  free(leaf);
  gg_write_file(path, src, len);
  return path;
}
