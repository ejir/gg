/* gg — Lua runtime: the `gg` table exposed to scripts and modules. */
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "gg.h"

lua_State *L;

#define GG_LIBNAME "gg"

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static void push_str_or_nil(lua_State *l, const char *s) {
  if (s) lua_pushstring(l, s);
  else lua_pushnil(l);
}

static char *lua_getcstr(lua_State *l, int idx) {
  const char *s = luaL_tolstring(l, idx, 0);
  char *out = gg_strdup(s ? s : "");
  lua_pop(l, 1);
  return out;
}

/* push an argv array from a lua value: accepts a string (single command),
 * a table of strings, or a table with {"cmd","a","b"} */
static int lua_to_argv(lua_State *l, int idx, strvec *store) {
  sv_reset(store);
  if (lua_isstring(l, idx)) {
    sv_push(store, lua_tostring(l, idx));
  } else if (lua_istable(l, idx)) {
    int n = (int)lua_rawlen(l, idx);
    for (int i = 1; i <= n; i++) {
      lua_rawgeti(l, idx, i);
      if (lua_isstring(l, -1) || lua_isnumber(l, -1)) {
        sv_push(store, lua_tostring(l, -1));
      } else {
        char *s = lua_getcstr(l, -1);
        sv_push(store, s);
        free(s);
      }
      lua_pop(l, 1);
    }
  } else {
    return 0;
  }
  return store->n;
}

static const char *opt_string(lua_State *l, int idx, const char *key,
                              const char *def) {
  lua_getfield(l, idx, key);
  const char *v = lua_isstring(l, -1) ? lua_tostring(l, -1) : def;
  lua_pop(l, 1);
  return v ? gg_strdup(v) : 0; /* caller frees */
}

static int opt_bool(lua_State *l, int idx, const char *key, int def) {
  lua_getfield(l, idx, key);
  int v = lua_isnil(l, -1) ? def : lua_toboolean(l, -1);
  lua_pop(l, 1);
  return v;
}

/* ------------------------------------------------------------------ */
/* logging                                                             */
/* ------------------------------------------------------------------ */

static void log_impl(lua_State *l, const char *prefix, const char *color) {
  sbuf b;
  sb_init(&b, 128);
  int n = lua_gettop(l);
  if (n >= 2 && lua_type(l, 1) == LUA_TSTRING) {
    const char *fmt = lua_tostring(l, 1);
    if (fmt && strchr(fmt, '%')) {
      /* behave like string.format so modules can write ctx.log("%s", x) */
      lua_getglobal(l, "string");
      lua_getfield(l, -1, "format");
      lua_remove(l, -2);
      lua_insert(l, 1);
      if (lua_pcall(l, n, 1, 0) != 0) {
        const char *msg = lua_tostring(l, -1);
        gg_error("log format error: %s", msg ? msg : "?");
        lua_pop(l, 1);
        sb_free(&b);
        return;
      }
      size_t len = 0;
      const char *s = lua_tolstring(l, -1, &len);
      sb_addn(&b, s ? s : "", len);
      lua_pop(l, 1);
      goto emit;
    }
  }
  for (int i = 1; i <= n; i++) {
    size_t len = 0;
    const char *s = luaL_tolstring(l, i, &len);
    if (i > 1) sb_addc(&b, '\t');
    sb_addn(&b, s ? s : "", len);
    lua_pop(l, 1);
  }
emit:
  fflush(stdout);
  if (prefix && *prefix)
    fprintf(stderr, "%s%s%s%s", color, prefix, c_reset(), "");
  fwrite(b.p, 1, b.n, stderr);
  fputc('\n', stderr);
  fflush(stderr);
  sb_free(&b);
}

static int l_log(lua_State *l) {
  log_impl(l, "", "");
  return 0;
}

static int l_print_stdout(lua_State *l) {
  int n = lua_gettop(l);
  for (int i = 1; i <= n; i++) {
    size_t len = 0;
    const char *s = luaL_tolstring(l, i, &len);
    if (i > 1) fputc('\t', stdout);
    fwrite(s ? s : "", 1, len, stdout);
    lua_pop(l, 1);
  }
  fputc('\n', stdout);
  return 0;
}

static int l_info(lua_State *l) {
  log_impl(l, "", c_dim());
  return 0;
}
static int l_warn(lua_State *l) {
  log_impl(l, "warning: ", c_warn());
  return 0;
}
static int l_err(lua_State *l) {
  log_impl(l, "error: ", c_err());
  return 0;
}
static int l_ok(lua_State *l) {
  log_impl(l, "", c_ok());
  return 0;
}

/* ------------------------------------------------------------------ */
/* environment / platform                                              */
/* ------------------------------------------------------------------ */

static int l_getenv(lua_State *l) {
  const char *k = luaL_checkstring(l, 1);
  push_str_or_nil(l, getenv(k));
  return 1;
}

static int l_setenv(lua_State *l) {
  const char *k = luaL_checkstring(l, 1);
  const char *v = luaL_optstring(l, 2, 0);
  if (v) setenv(k, v, 1);
  else unsetenv(k);
  return 0;
}

static int l_which(lua_State *l) {
  const char *name = luaL_checkstring(l, 1);
  if (gg_startswith(name, "gg ")) name += 3;
  const char *p = gg_which(name);
  push_str_or_nil(l, p);
  return 1;
}

static int l_have(lua_State *l) {
  lua_pushboolean(l, gg_have(luaL_checkstring(l, 1)));
  return 1;
}

static int l_refresh_tools(lua_State *l) {
  (void)l;
  gg_which_clear();
  return 0;
}

static int l_cwd(lua_State *l) {
  char buf[PATH_MAX];
  if (!getcwd(buf, sizeof(buf))) snprintf(buf, sizeof(buf), ".");
  lua_pushstring(l, buf);
  return 1;
}

static int l_sleep(lua_State *l) {
  double secs = luaL_optnumber(l, 1, 0);
  if (secs > 0) usleep((unsigned)(secs * 1e6));
  return 0;
}

static int l_now(lua_State *l) {
  lua_pushnumber(l, (double)time(0));
  return 1;
}

/* ------------------------------------------------------------------ */
/* filesystem                                                          */
/* ------------------------------------------------------------------ */

static int l_exists(lua_State *l) {
  lua_pushboolean(l, gg_exists(luaL_checkstring(l, 1)));
  return 1;
}
static int l_is_dir(lua_State *l) {
  lua_pushboolean(l, gg_is_dir(luaL_checkstring(l, 1)));
  return 1;
}
static int l_is_file(lua_State *l) {
  lua_pushboolean(l, gg_is_file(luaL_checkstring(l, 1)));
  return 1;
}

static int l_mkdir(lua_State *l) {
  const char *p = luaL_checkstring(l, 1);
  lua_pushboolean(l, gg_mkdir_p(p) == 0);
  return 1;
}

static int l_read(lua_State *l) {
  size_t len = 0;
  char *data = gg_read_file(luaL_checkstring(l, 1), &len);
  if (!data) {
    lua_pushnil(l);
    lua_pushstring(l, strerror(errno));
    return 2;
  }
  lua_pushlstring(l, data, len);
  free(data);
  return 1;
}

static int l_write(lua_State *l) {
  const char *path = luaL_checkstring(l, 1);
  size_t len = 0;
  const char *data = luaL_checklstring(l, 2, &len);
  int rc = gg_write_file(path, data, len);
  lua_pushboolean(l, rc == 0);
  return 1;
}

static int rm_rf(const char *path);

static int l_rm(lua_State *l) {
  const char *path = luaL_checkstring(l, 1);
  if (gg_is_dir(path)) {
    if (!opt_bool(l, 2, "recursive", 0) && !lua_toboolean(l, 2)) {
      if (rmdir(path) != 0) {
        lua_pushboolean(l, 0);
        lua_pushstring(l, strerror(errno));
        return 2;
      }
      lua_pushboolean(l, 1);
      return 1;
    }
    int rc = rm_rf(path);
    lua_pushboolean(l, rc == 0);
    if (rc != 0) lua_pushstring(l, strerror(errno));
    return rc == 0 ? 1 : 2;
  }
  if (unlink(path) != 0) {
    lua_pushboolean(l, 0);
    lua_pushstring(l, strerror(errno));
    return 2;
  }
  lua_pushboolean(l, 1);
  return 1;
}

static int rm_rf(const char *path) {
  DIR *d = opendir(path);
  if (d) {
    struct dirent *de;
    while ((de = readdir(d))) {
      if (gg_streq(de->d_name, ".") || gg_streq(de->d_name, "..")) continue;
      char *full = gg_join(path, de->d_name);
      if (gg_is_dir(full)) rm_rf(full);
      else unlink(full);
      free(full);
    }
    closedir(d);
  }
  return rmdir(path);
}

static int l_list(lua_State *l) {
  const char *path = luaL_optstring(l, 1, ".");
  DIR *d = opendir(path);
  int n = 0;
  lua_newtable(l);
  if (d) {
    struct dirent *de;
    while ((de = readdir(d))) {
      if (gg_streq(de->d_name, ".") || gg_streq(de->d_name, "..")) continue;
      if (lua_toboolean(l, 2)) {
        char *full = gg_join(path, de->d_name);
        lua_pushstring(l, full);
        free(full);
      } else {
        lua_pushstring(l, de->d_name);
      }
      lua_rawseti(l, -2, ++n);
    }
    closedir(d);
  }
  return 1;
}

static int l_stat(lua_State *l) {
  const char *path = luaL_checkstring(l, 1);
  struct stat st;
  if (stat(path, &st) != 0) {
    lua_pushnil(l);
    return 1;
  }
  lua_newtable(l);
  lua_pushnumber(l, (double)st.st_size);
  lua_setfield(l, -2, "size");
  lua_pushnumber(l, (double)st.st_mtime);
  lua_setfield(l, -2, "mtime");
  lua_pushboolean(l, S_ISDIR(st.st_mode));
  lua_setfield(l, -2, "is_dir");
  lua_pushboolean(l, S_ISREG(st.st_mode));
  lua_setfield(l, -2, "is_file");
  return 1;
}

static int l_copy(lua_State *l) {
  const char *a = luaL_checkstring(l, 1);
  const char *b = luaL_checkstring(l, 2);
  lua_pushboolean(l, gg_copy_file(a, b) == 0);
  return 1;
}

static int l_move(lua_State *l) {
  const char *a = luaL_checkstring(l, 1);
  const char *b = luaL_checkstring(l, 2);
  lua_pushboolean(l, rename(a, b) == 0);
  return 1;
}

static int l_chmod_x(lua_State *l) {
  lua_pushboolean(l, gg_chmod_x(luaL_checkstring(l, 1)) == 0);
  return 1;
}

static int l_mkstemp(lua_State *l) {
  const char *suffix = luaL_optstring(l, 1, "");
  const char *dir = getenv("TMPDIR");
  if (!dir || !*dir) dir = getenv("TEMP");
  if (!dir || !*dir) dir = "/tmp";
  if (!gg_is_dir(dir)) dir = ".";
  char *path = gg_asprintf("%s/gg-%d-%d%s", dir, (int)getpid(), (int)(time(0) % 100000),
                           suffix);
  FILE *f = fopen(path, "wb");
  if (f) fclose(f);
  lua_pushstring(l, path);
  free(path);
  return 1;
}

/* ------------------------------------------------------------------ */
/* processes                                                           */
/* ------------------------------------------------------------------ */

static void lua_on_line(const char *line, void *ud);

static int spawn_common(lua_State *l, int interactive_run) {
  luaL_checktype(l, 1, LUA_TTABLE);
  strvec argv;
  sv_init(&argv);
  int have = 0;

  lua_getfield(l, 1, "argv");
  if (lua_isnil(l, -1)) {
    lua_pop(l, 1);
    lua_getfield(l, 1, "cmd");
  }
  if (!lua_isnil(l, -1)) have = lua_to_argv(l, -1, &argv);
  lua_pop(l, 1);

  spawn_opts o;
  spawn_opts_init(&o);
  const char *cwd = opt_string(l, 1, "cwd", 0);
  const char *input = opt_string(l, 1, "input", 0);
  o.cwd = cwd;
  o.input = input;
  o.shell = opt_bool(l, 1, "shell", !have);
  o.sudo = opt_bool(l, 1, "sudo", 0);
  o.echo = opt_bool(l, 1, "echo", !interactive_run);

  if (o.shell && have == 1) {
    /* single string command line */
  } else if (o.shell && have > 1) {
    char *line = gg_join_argv(sv_argv(&argv));
    sv_reset(&argv);
    sv_push(&argv, line);
  }

  if (interactive_run) {
    int pause = opt_bool(l, 1, "pause", 1);
    int was_alt = T.interactive && T.in_alt;
    if (was_alt) tui_suspend();
    proc_echo_cmd(sv_argv(&argv));
    int rc = proc_run(sv_argv(&argv), &o, 0);
    if (was_alt) {
      if (pause && T.tty_out) {
        printf("%s%s%s ", c_dim(),
               gg_tr("-- done, press enter to go back", "-- 完成，按回车返回"),
               c_reset());
        fflush(stdout);
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {
        }
      }
      tui_resume();
    }
    lua_pushboolean(l, rc == 0);
    lua_pushinteger(l, rc);
    sv_free(&argv);
    free((void *)cwd);
    free((void *)input);
    return 2;
  }

  lua_getfield(l, 1, "on_line");
  int has_cb = lua_isfunction(l, -1);
  int has_capture = opt_bool(l, 1, "capture", 0);
  if (has_cb) {
    struct cb {
      lua_State *l;
      int ref;
    } ud;
    lua_pushvalue(l, -1);
    ud.l = l;
    ud.ref = luaL_ref(l, LUA_REGISTRYINDEX);
    o.on_line = lua_on_line;
    o.ud = &ud;
    if (T.interactive && !T.in_alt) tui_enter();
    char *out = 0;
    int rc = proc_run(sv_argv(&argv), &o, has_capture ? &out : 0);
    luaL_unref(l, LUA_REGISTRYINDEX, ud.ref);
    lua_pushboolean(l, rc == 0);
    lua_pushinteger(l, rc);
    int nres = 2;
    if (has_capture) {
      lua_pushstring(l, out ? out : "");
      free(out);
      nres = 3;
    }
    sv_free(&argv);
    free((void *)cwd);
    free((void *)input);
    lua_pop(l, 1); /* on_line */
    return nres;
  }
  lua_pop(l, 1); /* on_line */

  o.capture = has_capture;
  char *out = 0;
  int rc = proc_run(sv_argv(&argv), &o, has_capture ? &out : 0);
  lua_pushboolean(l, rc == 0);
  lua_pushinteger(l, rc);
  int nres = 2;
  if (has_capture) {
    lua_pushstring(l, out ? out : "");
    free(out);
    nres = 3;
  }
  sv_free(&argv);
  free((void *)cwd);
  free((void *)input);
  return nres;
}

static void lua_on_line(const char *line, void *ud) {
  struct cb {
    lua_State *l;
    int ref;
  } *c = ud;
  lua_State *l = c->l;
  lua_rawgeti(l, LUA_REGISTRYINDEX, c->ref);
  lua_pushstring(l, line);
  if (lua_pcall(l, 1, 0, 0) != 0) {
    gg_error("on_line: %s", lua_tostring(l, -1));
    lua_pop(l, 1);
  }
}

static int l_spawn(lua_State *l) { return spawn_common(l, 0); }
static int l_run(lua_State *l) { return spawn_common(l, 1); }

static int l_capture(lua_State *l) {
  const char *cmd = luaL_checkstring(l, 1);
  int status = 0;
  char *out = proc_shell_capture(cmd, &status);
  lua_pushstring(l, out);
  lua_pushinteger(l, status);
  free(out);
  return 2;
}

/* gg.sh("cmd") -> output            (shell capture, discards the code) */
static int l_sh(lua_State *l) {
  const char *cmd = luaL_checkstring(l, 1);
  int status = 0;
  char *out = proc_shell_capture(cmd, &status);
  lua_pushstring(l, out);
  free(out);
  return 1;
}

static int l_exec(lua_State *l) {
  luaL_checktype(l, 1, LUA_TTABLE);
  strvec argv;
  sv_init(&argv);
  lua_getfield(l, 1, "argv");
  if (lua_isnil(l, -1)) {
    lua_pop(l, 1);
    lua_getfield(l, 1, "cmd");
  }
  lua_to_argv(l, -1, &argv);
  lua_pop(l, 1);
  int shell = opt_bool(l, 1, "shell", 0);
  int sudo = opt_bool(l, 1, "sudo", 0);
  int was_alt = T.interactive && T.in_alt;
  if (was_alt) tui_suspend();
  spawn_opts o;
  spawn_opts_init(&o);
  o.sudo = sudo;
  o.shell = shell;
  int rc = proc_run(sv_argv(&argv), &o, 0);
  sv_free(&argv);
  if (was_alt) tui_resume();
  lua_pushinteger(l, rc);
  return 1;
}

/* ------------------------------------------------------------------ */
/* shell quoting / strings                                             */
/* ------------------------------------------------------------------ */

static int l_shell_quote(lua_State *l) {
  char *q = gg_shell_quote(luaL_checkstring(l, 1));
  lua_pushstring(l, q);
  free(q);
  return 1;
}

static int l_trim(lua_State *l) {
  const char *s = luaL_checkstring(l, 1);
  char *copy = gg_strdup(s);
  char *t = gg_trim(copy);
  lua_pushstring(l, t);
  free(copy);
  return 1;
}

static int l_split(lua_State *l) {
  const char *s = luaL_checkstring(l, 1);
  const char *sep = luaL_optstring(l, 2, " \t\n");
  int n = 0;
  lua_newtable(l);
  size_t sl = strlen(s);
  size_t i = 0;
  while (i < sl) {
    while (i < sl && strchr(sep, s[i])) i++;
    size_t start = i;
    while (i < sl && !strchr(sep, s[i])) i++;
    if (i == start) break;
    lua_pushlstring(l, s + start, i - start);
    lua_rawseti(l, -2, ++n);
  }
  return 1;
}

static int l_join(lua_State *l) {
  luaL_checktype(l, 1, LUA_TTABLE);
  const char *sep = luaL_optstring(l, 2, " ");
  sbuf b;
  sb_init(&b, 64);
  int n = (int)lua_rawlen(l, 1);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(l, 1, i);
    size_t len = 0;
    const char *s = luaL_tolstring(l, -1, &len);
    if (i > 1) sb_adds(&b, sep);
    if (s) sb_addn(&b, s, len);
    lua_pop(l, 2);
  }
  lua_pushstring(l, b.p);
  sb_free(&b);
  return 1;
}

static int l_basename(lua_State *l) {
  lua_pushstring(l, gg_basename(luaL_checkstring(l, 1)));
  return 1;
}

static int l_dirname(lua_State *l) {
  char *d = gg_dirname_of(luaL_checkstring(l, 1));
  lua_pushstring(l, d);
  free(d);
  return 1;
}

static int l_ext(lua_State *l) {
  char *e = gg_ext_of(luaL_checkstring(l, 1));
  lua_pushstring(l, e);
  free(e);
  return 1;
}

static int l_join_path(lua_State *l) {
  int n = lua_gettop(l);
  if (n == 1 && lua_istable(l, 1)) {
    sbuf b;
    sb_init(&b, 64);
    int m = (int)lua_rawlen(l, 1);
    for (int i = 1; i <= m; i++) {
      lua_rawgeti(l, 1, i);
      const char *s = lua_tostring(l, -1);
      if (i > 1) sb_addc(&b, '/');
      sb_adds(&b, s ? s : "");
      lua_pop(l, 1);
    }
    lua_pushstring(l, b.p);
    sb_free(&b);
    return 1;
  }
  sbuf b;
  sb_init(&b, 64);
  for (int i = 1; i <= n; i++) {
    const char *s = lua_tostring(l, i);
    if (i > 1) sb_addc(&b, '/');
    sb_adds(&b, s ? s : "");
  }
  lua_pushstring(l, b.p);
  sb_free(&b);
  return 1;
}

static int l_abs(lua_State *l) {
  char *p = gg_path_abs(luaL_checkstring(l, 1));
  lua_pushstring(l, p);
  free(p);
  return 1;
}

static int l_i18n(lua_State *l) {
  const char *en = luaL_checkstring(l, 1);
  const char *zh = luaL_optstring(l, 2, en);
  lua_pushstring(l, gg_lang_zh() ? zh : en);
  return 1;
}

/* ------------------------------------------------------------------ */
/* interactive helpers                                                 */
/* ------------------------------------------------------------------ */

static int l_confirm(lua_State *l) {
  const char *msg = luaL_checkstring(l, 1);
  int def = (int)luaL_optinteger(l, 2, 1);
  lua_pushboolean(l, tui_confirm(gg_tr("confirm", "确认"), msg, def));
  return 1;
}

static int l_ask(lua_State *l) {
  const char *title = luaL_optstring(l, 1, gg_tr("input", "输入"));
  const char *label = luaL_optstring(l, 2, "");
  const char *def = luaL_optstring(l, 3, "");
  char buf[1024];
  snprintf(buf, sizeof(buf), "%s", def ? def : "");
  if (tui_prompt(title, label, buf, sizeof(buf), 0) != 0) {
    lua_pushnil(l);
    return 1;
  }
  lua_pushstring(l, buf);
  return 1;
}

static int l_select(lua_State *l) {
  const char *title = luaL_optstring(l, 1, gg_tr("select", "选择"));
  const char *msg = luaL_optstring(l, 2, "");
  luaL_checktype(l, 3, LUA_TTABLE);
  int n = (int)lua_rawlen(l, 3);
  const char **items = malloc(sizeof(char *) * (size_t)(n + 1));
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(l, 3, i);
    items[i - 1] = gg_strdup(lua_tostring(l, -1) ? lua_tostring(l, -1) : "");
    lua_pop(l, 1);
  }
  int rc = tui_select(title, msg, items, n);
  for (int i = 0; i < n; i++) free((void *)items[i]);
  free(items);
  if (rc < 0) lua_pushnil(l);
  else lua_pushinteger(l, rc + 1);
  return 1;
}

static int l_message(lua_State *l) {
  const char *title = luaL_optstring(l, 1, "gg");
  const char *body = luaL_optstring(l, 2, "");
  tui_message(title, body);
  return 0;
}

/* ------------------------------------------------------------------ */
/* registry / modules                                                  */
/* ------------------------------------------------------------------ */

static int lreg_list(lua_State *l) {
  lua_newtable(l);
  int n = 0;
  for (int i = 0; i < reg_count(); i++) {
    reg_entry *e = reg_at(i);
    lua_newtable(l);
    lua_pushstring(l, e->name);
    lua_setfield(l, -2, "name");
    lua_pushstring(l, e->cmd);
    lua_setfield(l, -2, "cmd");
    lua_pushstring(l, e->desc);
    lua_setfield(l, -2, "desc");
    lua_pushstring(l, e->source);
    lua_setfield(l, -2, "source");
    lua_rawseti(l, -2, ++n);
  }
  return 1;
}

static int lreg_add(lua_State *l) {
  const char *name = luaL_checkstring(l, 1);
  const char *cmd = luaL_checkstring(l, 2);
  const char *desc = luaL_optstring(l, 3, "");
  lua_pushboolean(l, reg_add(name, cmd, desc) == 0);
  return 1;
}

static int lreg_remove(lua_State *l) {
  lua_pushboolean(l, reg_remove(luaL_checkstring(l, 1)) == 0);
  return 1;
}

static int lreg_get(lua_State *l) {
  reg_entry *e = reg_find(luaL_checkstring(l, 1));
  if (!e) {
    lua_pushnil(l);
    return 1;
  }
  lua_newtable(l);
  lua_pushstring(l, e->cmd);
  lua_setfield(l, -2, "cmd");
  lua_pushstring(l, e->desc);
  lua_setfield(l, -2, "desc");
  return 1;
}

static int lreg_log(lua_State *l) {
  const char *name = luaL_optstring(l, 1, "");
  const char *cmd = luaL_optstring(l, 2, "");
  reg_log(name, cmd);
  return 0;
}

static int lmod_list(lua_State *l) {
  lua_newtable(l);
  int n = 0;
  for (int i = 0; i < modules_count(); i++) {
    module_info *m = modules_at(i);
    lua_newtable(l);
    lua_pushstring(l, m->name);
    lua_setfield(l, -2, "name");
    lua_pushstring(l, m->title ? m->title : m->name);
    lua_setfield(l, -2, "title");
    lua_pushstring(l, m->desc ? m->desc : "");
    lua_setfield(l, -2, "desc");
    push_str_or_nil(l, m->path);
    lua_setfield(l, -2, "path");
    lua_pushboolean(l, m->builtin);
    lua_setfield(l, -2, "builtin");
    lua_pushboolean(l, m->is_tui);
    lua_setfield(l, -2, "tui");
    lua_rawseti(l, -2, ++n);
  }
  return 1;
}

static int lmod_path(lua_State *l) {
  char *p = module_path_of(luaL_checkstring(l, 1));
  push_str_or_nil(l, p);
  free(p);
  return 1;
}

static int lmod_install_examples(lua_State *l) {
  int force = (int)lua_toboolean(l, 1);
  const char *dir = gg_modules_dir();
  gg_mkdir_p(dir);
  int n = 0;
  for (int i = 0; i < GG_EMBEDDED_COUNT; i++) {
    const char *nm = GG_EMBEDDED[i].name;
    if (!gg_startswith(nm, "mod_") || !gg_endswith(nm, ".lua")) continue;
    char *target = gg_join(dir, nm + 4);
    if (!force && gg_exists(target)) {
      free(target);
      continue;
    }
    if (gg_write_file(target, GG_EMBEDDED[i].data, GG_EMBEDDED[i].len) == 0) n++;
    free(target);
  }
  lua_pushinteger(l, n);
  return 1;
}

/* ------------------------------------------------------------------ */
/* download helper                                                     */
/* ------------------------------------------------------------------ */

static int l_download(lua_State *l) {
  const char *url = luaL_checkstring(l, 1);
  const char *dest = luaL_optstring(l, 2, 0);
  strvec argv;
  sv_init(&argv);
  if (gg_have("curl")) {
    sv_push(&argv, "curl");
    sv_push(&argv, "-fL");
    sv_push(&argv, "--retry");
    sv_push(&argv, "3");
    if (dest && *dest) {
      sv_push(&argv, "-o");
      sv_push(&argv, dest);
    }
    sv_push(&argv, url);
  } else if (gg_have("wget")) {
    sv_push(&argv, "wget");
    sv_push(&argv, "-c");
    if (dest && *dest) {
      sv_push(&argv, "-O");
      sv_push(&argv, dest);
    }
    sv_push(&argv, url);
  } else {
    sv_free(&argv);
    lua_pushboolean(l, 0);
    lua_pushstring(l, gg_tr("neither curl nor wget was found",
                            "找不到 curl 或 wget"));
    return 2;
  }
  spawn_opts o;
  spawn_opts_init(&o);
  o.echo = 1;
  int rc = proc_run(sv_argv(&argv), &o, 0);
  sv_free(&argv);
  lua_pushboolean(l, rc == 0);
  lua_pushinteger(l, rc);
  return 2;
}

static int l_open(lua_State *l) {
  const char *target = luaL_checkstring(l, 1);
  strvec argv;
  sv_init(&argv);
#if GG_WINDOWS
  sv_push(&argv, "cmd");
  sv_push(&argv, "/c");
  sv_push(&argv, "start");
  sv_push(&argv, "");
  sv_push(&argv, target);
#elif GG_MACOS
  sv_push(&argv, "open");
  sv_push(&argv, target);
#else
  sv_push(&argv, "xdg-open");
  sv_push(&argv, target);
#endif
  spawn_opts o;
  spawn_opts_init(&o);
  o.quiet = 1;
  int rc = proc_run(sv_argv(&argv), &o, 0);
  sv_free(&argv);
  lua_pushboolean(l, rc == 0);
  return 1;
}

/* ------------------------------------------------------------------ */
/* json                                                               */
/* ------------------------------------------------------------------ */

static void json_escape(sbuf *b, const char *s, size_t n) {
  sb_addc(b, '"');
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '"': sb_adds(b, "\\\""); break;
      case '\\': sb_adds(b, "\\\\"); break;
      case '\n': sb_adds(b, "\\n"); break;
      case '\r': sb_adds(b, "\\r"); break;
      case '\t': sb_adds(b, "\\t"); break;
      case '\b': sb_adds(b, "\\b"); break;
      case '\f': sb_adds(b, "\\f"); break;
      default:
        if (c < 0x20) sb_addf(b, "\\u%04x", c);
        else sb_addc(b, (char)c);
    }
  }
  sb_addc(b, '"');
}

static void json_encode(lua_State *l, sbuf *b, int depth);

static void json_encode_table(lua_State *l, sbuf *b, int depth) {
  if (depth > 20) {
    sb_adds(b, "null");
    return;
  }
  int n = (int)lua_rawlen(l, -1);
  int count = 0;
  int is_array = n > 0;
  lua_pushnil(l);
  while (lua_next(l, -2)) {
    count++;
    if (lua_type(l, -2) != LUA_TNUMBER) {
      is_array = 0;
    } else {
      double d = lua_tonumber(l, -2);
      if (d < 1 || d != (double)(long long)d || (long long)d > n) is_array = 0;
    }
    lua_pop(l, 1);
  }
  if (n == 0 || !is_array || count != n) {
    if (count == 0) {
      sb_adds(b, "{}");
      return;
    }
    /* object */
    sb_addc(b, '{');
    int first = 1;
    lua_pushnil(l);
    while (lua_next(l, -2)) {
      if (!first) sb_addc(b, ',');
      first = 0;
      lua_pushvalue(l, -2); /* key copy */
      if (lua_type(l, -1) == LUA_TSTRING) json_encode(l, b, depth + 1);
      else {
        size_t klen = 0;
        char *k = lua_getcstr(l, -1);
        json_escape(b, k, strlen(k));
        free(k);
        (void)klen;
      }
      lua_pop(l, 1);
      sb_addc(b, ':');
      json_encode(l, b, depth + 1); /* value */
      lua_pop(l, 1);
    }
    sb_addc(b, '}');
    return;
  }
  sb_addc(b, '[');
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(l, -1, i);
    json_encode(l, b, depth + 1);
    lua_pop(l, 1);
    if (i < n) sb_addc(b, ',');
  }
  sb_addc(b, ']');
}

static void json_encode(lua_State *l, sbuf *b, int depth) {
  switch (lua_type(l, -1)) {
    case LUA_TNIL:
      sb_adds(b, "null");
      break;
    case LUA_TBOOLEAN:
      sb_adds(b, lua_toboolean(l, -1) ? "true" : "false");
      break;
    case LUA_TNUMBER: {
      double d = lua_tonumber(l, -1);
      if (d == (double)(long long)d && d < 1e15 && d > -1e15)
        sb_addf(b, "%lld", (long long)d);
      else
        sb_addf(b, "%.17g", d);
      break;
    }
    case LUA_TSTRING: {
      size_t n = 0;
      const char *s = lua_tolstring(l, -1, &n);
      json_escape(b, s, n);
      break;
    }
    case LUA_TTABLE:
      json_encode_table(l, b, depth);
      break;
    default:
      sb_adds(b, "null");
      break;
  }
}

static int l_json_encode(lua_State *l) {
  sbuf b;
  sb_init(&b, 256);
  json_encode(l, &b, 0);
  lua_pushlstring(l, b.p, b.n);
  sb_free(&b);
  return 1;
}

typedef struct {
  const char *p;
  const char *end;
  lua_State *l;
  int err;
  const char *emsg;
} json_parser;

static void json_skip_ws(json_parser *j) {
  while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' ||
                           *j->p == '\r'))
    j->p++;
}

static void json_value(json_parser *j, int depth);

static void json_string(json_parser *j) {
  if (j->p >= j->end || *j->p != '"') {
    j->err = 1;
    j->emsg = "expected string";
    return;
  }
  j->p++;
  sbuf b;
  sb_init(&b, 64);
  while (j->p < j->end && *j->p != '"') {
    if (*j->p == '\\' && j->p + 1 < j->end) {
      j->p++;
      switch (*j->p) {
        case 'n': sb_addc(&b, '\n'); break;
        case 't': sb_addc(&b, '\t'); break;
        case 'r': sb_addc(&b, '\r'); break;
        case 'b': sb_addc(&b, '\b'); break;
        case 'f': sb_addc(&b, '\f'); break;
        case '/': sb_addc(&b, '/'); break;
        case '"': sb_addc(&b, '"'); break;
        case '\\': sb_addc(&b, '\\'); break;
        case 'u': {
          unsigned cp = 0;
          for (int i = 0; i < 4 && j->p + 1 < j->end; i++) {
            j->p++;
            char c = *j->p;
            cp <<= 4;
            if (c >= '0' && c <= '9') cp |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') cp |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') cp |= (unsigned)(c - 'A' + 10);
          }
          char tmp[4];
          int n = gg_utf8_encode(cp >= 0xD800 && cp <= 0xDFFF ? '?' : cp, tmp);
          sb_addn(&b, tmp, (size_t)n);
          break;
        }
        default: sb_addc(&b, *j->p);
      }
      j->p++;
    } else {
      sb_addc(&b, *j->p++);
    }
  }
  if (j->p < j->end) j->p++; /* closing quote */
  lua_pushlstring(j->l, b.p, b.n);
  sb_free(&b);
}

static void json_value(json_parser *j, int depth) {
  if (depth > 64) {
    j->err = 1;
    j->emsg = "too deep";
    return;
  }
  json_skip_ws(j);
  if (j->p >= j->end) {
    j->err = 1;
    return;
  }
  char c = *j->p;
  if (c == '{') {
    j->p++;
    lua_newtable(j->l);
    json_skip_ws(j);
    if (j->p < j->end && *j->p == '}') {
      j->p++;
      return;
    }
    for (;;) {
      json_skip_ws(j);
      json_string(j);
      if (j->err) return;
      json_skip_ws(j);
      if (j->p < j->end && *j->p == ':') j->p++;
      else {
        j->err = 1;
        j->emsg = "expected :";
        return;
      }
      json_value(j, depth + 1);
      if (j->err) return;
      lua_settable(j->l, -3);
      json_skip_ws(j);
      if (j->p < j->end && *j->p == ',') {
        j->p++;
        continue;
      }
      if (j->p < j->end && *j->p == '}') {
        j->p++;
        return;
      }
      j->err = 1;
      j->emsg = "expected , or }";
      return;
    }
  }
  if (c == '[') {
    j->p++;
    lua_newtable(j->l);
    int n = 0;
    json_skip_ws(j);
    if (j->p < j->end && *j->p == ']') {
      j->p++;
      return;
    }
    for (;;) {
      json_value(j, depth + 1);
      if (j->err) return;
      lua_rawseti(j->l, -2, ++n);
      json_skip_ws(j);
      if (j->p < j->end && *j->p == ',') {
        j->p++;
        continue;
      }
      if (j->p < j->end && *j->p == ']') {
        j->p++;
        return;
      }
      j->err = 1;
      j->emsg = "expected , or ]";
      return;
    }
  }
  if (c == '"') {
    json_string(j);
    return;
  }
  if (!strncmp(j->p, "true", 4)) {
    j->p += 4;
    lua_pushboolean(j->l, 1);
    return;
  }
  if (!strncmp(j->p, "false", 5)) {
    j->p += 5;
    lua_pushboolean(j->l, 0);
    return;
  }
  if (!strncmp(j->p, "null", 4)) {
    j->p += 4;
    lua_pushnil(j->l);
    return;
  }
  {
    char buf[64];
    size_t n = 0;
    while (j->p < j->end && n < sizeof(buf) - 1 &&
           (isdigit((unsigned char)*j->p) || *j->p == '-' || *j->p == '+' ||
            *j->p == '.' || *j->p == 'e' || *j->p == 'E'))
      buf[n++] = *j->p++;
    buf[n] = 0;
    if (!n) {
      j->err = 1;
      j->emsg = "unexpected character";
      return;
    }
    {
      double d = strtod(buf, 0);
      long long ll = (long long)d;
      if (d == (double)ll && d >= -9.2e18 && d <= 9.2e18)
        lua_pushinteger(j->l, (lua_Integer)ll);
      else
        lua_pushnumber(j->l, d);
    }
  }
}

static int l_json_decode(lua_State *l) {
  size_t n = 0;
  const char *s = luaL_checklstring(l, 1, &n);
  json_parser j;
  j.p = s;
  j.end = s + n;
  j.l = l;
  j.err = 0;
  j.emsg = "parse error";
  json_skip_ws(&j);
  json_value(&j, 0);
  if (j.err) {
    lua_pushnil(l);
    lua_pushstring(l, j.emsg);
    return 2;
  }
  return 1;
}

/* ------------------------------------------------------------------ */
/* colors / misc                                                       */
/* ------------------------------------------------------------------ */

static const char *style_by_name(const char *name) {
  if (gg_streq(name, "accent")) return ST_ACCENT;
  if (gg_streq(name, "ok") || gg_streq(name, "success")) return ST_OK;
  if (gg_streq(name, "warn")) return ST_WARN;
  if (gg_streq(name, "err") || gg_streq(name, "error")) return ST_ERR;
  if (gg_streq(name, "muted") || gg_streq(name, "dim")) return ST_MUTED;
  if (gg_streq(name, "bold")) return ST_BOLD;
  if (gg_streq(name, "title")) return ST_TITLE;
  if (gg_streq(name, "rev") || gg_streq(name, "selected")) return ST_SEL;
  return "";
}

static int l_style(lua_State *l) {
  const char *name = luaL_optstring(l, 1, "accent");
  lua_pushstring(l, tui_style(style_by_name(name)));
  return 1;
}

static int l_colorize(lua_State *l) {
  const char *text = luaL_checkstring(l, 1);
  const char *name = luaL_optstring(l, 2, "accent");
  lua_pushfstring(l, "%s%s%s", tui_style(style_by_name(name)), text,
                  tui_style(ST_RESET));
  return 1;
}

static int l_platform(lua_State *l) {
  lua_newtable(l);
#if GG_WINDOWS
  lua_pushstring(l, "windows");
#elif GG_MACOS
  lua_pushstring(l, "macos");
#else
  lua_pushstring(l, "linux");
#endif
  lua_setfield(l, -2, "os");
#if defined(__x86_64__) || defined(_M_X64)
  lua_pushstring(l, "x86_64");
#elif defined(__aarch64__) || defined(_M_ARM64)
  lua_pushstring(l, "aarch64");
#else
  lua_pushstring(l, "unknown");
#endif
  lua_setfield(l, -2, "arch");
  lua_pushboolean(l, T.interactive);
  lua_setfield(l, -2, "interactive");
  lua_pushboolean(l, gg_is_root());
  lua_setfield(l, -2, "root");
  lua_pushstring(l, gg_lang_zh() ? "zh" : "en");
  lua_setfield(l, -2, "lang");
  long cpus = sysconf(_SC_NPROCESSORS_ONLN);
  lua_pushinteger(l, cpus > 0 ? cpus : 1);
  lua_setfield(l, -2, "cpus");
  lua_pushstring(l, gg_home());
  lua_setfield(l, -2, "home");
  lua_pushstring(l, gg_dir());
  lua_setfield(l, -2, "dir");
  lua_pushstring(l, gg_tr("yes", "是"));
  lua_setfield(l, -2, "yes");
  lua_pushstring(l, gg_tr("no", "否"));
  lua_setfield(l, -2, "no");
  return 1;
}

/* ------------------------------------------------------------------ */
/* tui sub-library                                                     */
/* ------------------------------------------------------------------ */

static int ltui_available(lua_State *l) {
  lua_pushboolean(l, T.interactive);
  return 1;
}

static int ltui_size(lua_State *l) {
  tui_size();
  lua_pushinteger(l, T.w);
  lua_pushinteger(l, T.h);
  return 2;
}

static int ltui_begin(lua_State *l) { tui_enter(); return 0; }
static int ltui_end(lua_State *l) { tui_leave(); return 0; }

static int ltui_menu(lua_State *l) {
  luaL_checktype(l, 1, LUA_TTABLE);
  lua_getfield(l, 1, "items");
  luaL_checktype(l, -1, LUA_TTABLE);
  int n = (int)lua_rawlen(l, -1);
  const char **items = malloc(sizeof(char *) * (size_t)(n + 1));
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(l, -1, i);
    items[i - 1] = gg_strdup(lua_tostring(l, -1) ? lua_tostring(l, -1) : "");
    lua_pop(l, 1);
  }
  lua_getfield(l, 1, "title");
  const char *title = lua_tostring(l, -1);
  lua_getfield(l, 1, "footer");
  const char *footer = lua_tostring(l, -1);
  tui_menu m;
  tui_menu_init(&m, title ? title : "gg", items, n);
  m.footer = footer;
  int other = 0;
  int rc = tui_menu_run(&m, &other);
  for (int i = 0; i < n; i++) free((void *)items[i]);
  free(items);
  if (rc < 0) lua_pushnil(l);   /* cancelled */
  else lua_pushinteger(l, rc + 1);
  lua_pushinteger(l, other);
  return 2;
}

/* booleans are common as `default = true`, treat them as "1"/"0" */
static const char *lua_to_str(lua_State *l, int idx) {
  if (lua_isboolean(l, idx)) return lua_toboolean(l, idx) ? "1" : "0";
  return lua_tostring(l, idx);
}

static int ltui_form(lua_State *l) {
  luaL_checktype(l, 1, LUA_TTABLE);
  lua_getfield(l, 1, "fields");
  luaL_checktype(l, -1, LUA_TTABLE);
  int n = (int)lua_rawlen(l, -1);
  tui_field *f = calloc((size_t)(n + 1), sizeof(tui_field));
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(l, -1, i); /* the field descriptor table */
    int ft = lua_gettop(l);
    if (!lua_istable(l, ft)) {
      lua_pop(l, 1);
      continue;
    }
    /* every lookup happens on `ft` and leaves the stack exactly as it was */
    lua_getfield(l, ft, "name");
    f[i - 1].name = gg_strdup(lua_tostring(l, -1) ? lua_tostring(l, -1) : "");
    lua_pop(l, 1);
    lua_getfield(l, ft, "label");
    f[i - 1].label = gg_strdup(lua_tostring(l, -1) ? lua_tostring(l, -1) : "");
    lua_pop(l, 1);
    lua_getfield(l, ft, "help");
    f[i - 1].help = gg_strdup(lua_tostring(l, -1) ? lua_tostring(l, -1) : "");
    lua_pop(l, 1);
    lua_getfield(l, ft, "default");
    f[i - 1].value = gg_strdup(lua_to_str(l, -1) ? lua_to_str(l, -1) : "");
    lua_pop(l, 1);
    lua_getfield(l, ft, "kind");
    const char *kind = lua_to_str(l, -1);
    f[i - 1].kind = kind && gg_streq(kind, "bool") ? 1
                    : (kind && gg_streq(kind, "choice")) ? 2
                                                         : 0;
    lua_pop(l, 1);
    lua_getfield(l, ft, "choices");
    if (lua_istable(l, -1)) {
      int cn = (int)lua_rawlen(l, -1);
      f[i - 1].choices = malloc(sizeof(char *) * (size_t)(cn + 1));
      for (int c = 1; c <= cn; c++) {
        lua_rawgeti(l, -1, c);
        f[i - 1].choices[c - 1] =
            gg_strdup(lua_tostring(l, -1) ? lua_tostring(l, -1) : "");
        lua_pop(l, 1);
      }
      f[i - 1].nchoices = cn;
    }
    lua_pop(l, 2); /* choices, field descriptor */
  }
  lua_getfield(l, 1, "title");
  const char *title = lua_tostring(l, -1);
  lua_getfield(l, 1, "subtitle");
  const char *subtitle = lua_tostring(l, -1);
  int rc = tui_form(title ? title : "gg", subtitle, f, n);
  lua_newtable(l);
  int res = lua_gettop(l);
  for (int i = 0; i < n; i++) {
    if (f[i].kind == 1)
      lua_pushboolean(l, f[i].value && gg_streq(f[i].value, "1"));
    else
      lua_pushstring(l, f[i].value ? f[i].value : "");
    lua_setfield(l, res, f[i].name);
  }
  lua_pushboolean(l, rc == 0);
  lua_setfield(l, res, "ok");
  for (int i = 0; i < n; i++) {
    free((void *)f[i].name);
    free((void *)f[i].label);
    free((void *)f[i].help);
    free(f[i].value);
    for (int c = 0; c < f[i].nchoices; c++) free((void *)f[i].choices[c]);
    free((void *)f[i].choices);
  }
  free(f);
  return 1;
}

static int ltui_textbox(lua_State *l) {
  luaL_checktype(l, 1, LUA_TTABLE);
  lua_getfield(l, 1, "text");
  const char *text = lua_tostring(l, -1);
  lua_getfield(l, 1, "title");
  const char *title = lua_tostring(l, -1);
  lua_getfield(l, 1, "filename");
  const char *fname = lua_tostring(l, -1);
  char *res = tui_textbox(title ? title : "gg", fname, text ? text : "");
  push_str_or_nil(l, res);
  free(res);
  return 1;
}

/* progress objects: userdata with __index metatable */
typedef struct {
  int active;
} progress_ud;

static int lprog_set(lua_State *l) {
  double pct = luaL_optnumber(l, 2, -1);
  const char *msg = luaL_optstring(l, 3, 0);
  tui_progress_set(pct, msg);
  return 0;
}
static int lprog_log(lua_State *l) {
  const char *line = luaL_checkstring(l, 2);
  tui_progress_log(line);
  return 0;
}
static int lprog_done(lua_State *l) {
  int ok = (int)lua_toboolean(l, 2);
  const char *msg = luaL_optstring(l, 3, "");
  progress_ud *p = lua_touserdata(l, 1);
  if (p && p->active) {
    p->active = 0;
    tui_progress_end(ok, msg);
  }
  return 0;
}
static int lprog_gc(lua_State *l) {
  progress_ud *p = lua_touserdata(l, 1);
  if (p && p->active) {
    p->active = 0;
    tui_progress_end(0, gg_tr("interrupted", "已中断"));
  }
  return 0;
}

static int ltui_progress(lua_State *l) {
  luaL_checktype(l, 1, LUA_TTABLE);
  lua_getfield(l, 1, "title");
  const char *title = lua_tostring(l, -1);
  lua_getfield(l, 1, "subtitle");
  const char *subtitle = lua_tostring(l, -1);
  tui_progress_begin(title ? title : "gg", subtitle);
  progress_ud *p = lua_newuserdatauv(l, sizeof(progress_ud), 0);
  p->active = 1;
  luaL_getmetatable(l, "gg.progress");
  lua_setmetatable(l, -2);
  return 1;
}

static int ltui_message(lua_State *l) {
  const char *title = luaL_optstring(l, 1, "gg");
  const char *body = luaL_optstring(l, 2, "");
  tui_message(title, body);
  return 0;
}

static int ltui_confirm(lua_State *l) {
  const char *msg = luaL_checkstring(l, 1);
  int def = (int)luaL_optinteger(l, 2, 1);
  lua_pushboolean(l, tui_confirm(gg_tr("confirm", "确认"), msg, def));
  return 1;
}

static int ltui_prompt(lua_State *l) {
  const char *title = luaL_optstring(l, 1, gg_tr("input", "输入"));
  const char *label = luaL_optstring(l, 2, "");
  const char *def = luaL_optstring(l, 3, "");
  char buf[1024];
  snprintf(buf, sizeof(buf), "%s", def ? def : "");
  if (tui_prompt(title, label, buf, sizeof(buf), 0) != 0) {
    lua_pushnil(l);
    return 1;
  }
  lua_pushstring(l, buf);
  return 1;
}

/* ------------------------------------------------------------------ */
/* assets — files carried inside the appended zip (see bundle.c)        */
/* ------------------------------------------------------------------ */

static char g_assets_dir[PATH_MAX];

static char *asset_key(const char *name) {
  if (gg_startswith(name, "assets/")) return gg_strdup(name);
  return gg_asprintf("assets/%s", name);
}

static int l_assets_read(lua_State *l) {
  const char *name = luaL_checkstring(l, 1);
  char *key = asset_key(name);
  size_t len = 0;
  char *data = bundle_read(key, &len);
  free(key);
  if (!data) {
    lua_pushnil(l);
    return 1;
  }
  lua_pushlstring(l, data, len);
  free(data);
  return 1;
}

static int l_assets_list(lua_State *l) {
  const char *prefix = luaL_optstring(l, 1, "");
  lua_newtable(l);
  int n = 0;
  int user = bundle_has_manifest() ? bundle_user_count() : bundle_count();
  for (int i = 0; i < user; i++) {
    const char *nm = bundle_has_manifest() ? bundle_user_name(i)
                                           : bundle_entry_name(i);
    if (!nm) continue;
    if (*prefix && !gg_startswith(nm, prefix)) continue;
    lua_pushstring(l, nm);
    lua_rawseti(l, -2, ++n);
  }
  return 1;
}

static void assets_dir_cleanup(void) {
  rm_rf(g_assets_dir);
  if (!g_assets_dir[0]) return;
  if (!getenv("GG_KEEP_ASSETS")) rm_rf(g_assets_dir);
}

/* extract everything to a private directory (removed at exit) */
static int l_assets_dir(lua_State *l) {
  static char cached[PATH_MAX];
  if (!bundle_present()) {
    lua_pushnil(l); /* nothing carried: no directory to hand out */
    return 1;
  }
  if (!cached[0]) {
    char tmpl[PATH_MAX];
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    snprintf(tmpl, sizeof(tmpl), "%s/gg-bundle-%d", tmp, (int)getpid());
    gg_mkdir_p(tmpl);
    for (int i = 0; i < bundle_count(); i++) {
      const char *nm = bundle_entry_name(i);
      size_t len = 0;
      char *data = bundle_read(nm, &len);
      if (!data) continue;
      char *path = gg_join(tmpl, nm);
      char *slash = strrchr(path, '/');
      if (slash) {
        *slash = 0;
        gg_mkdir_p(path);
        *slash = '/';
      }
      gg_write_file(path, data, len);
      free(path);
      free(data);
    }
    snprintf(cached, sizeof(cached), "%s", tmpl);
    snprintf(g_assets_dir, sizeof(g_assets_dir), "%s", tmpl);
    atexit(assets_dir_cleanup);
  }
  lua_pushstring(l, cached);
  return 1;
}

static int l_assets_have(lua_State *l) {
  const char *name = luaL_checkstring(l, 1);
  char *key = asset_key(name);
  int ok = bundle_has(key);
  free(key);
  lua_pushboolean(l, ok);
  return 1;
}

static int pcall_protected(lua_State *l, int nargs, int nresults);

/* run init.lua once when the lua state comes up (bundles may carry one) */
static int l_bundle_run_init(lua_State *l) {
  (void)l;
  if (!bundle_present()) return 0;
  size_t len = 0;
  char *src = bundle_read("init.lua", &len);
  if (!src) return 0;
  if (luaL_loadbuffer(l, src, len, "@init.lua(bundle)") == 0)
    pcall_protected(l, 0, 0);
  else
    lua_pop(l, 1);
  free(src);
  return 0;
}

/* ------------------------------------------------------------------ */
/* registration                                                        */
/* ------------------------------------------------------------------ */

static const luaL_Reg gg_funcs[] = {
    {"log", l_log},          {"info", l_info},
    {"warn", l_warn},        {"err", l_err},
    {"ok", l_ok},            {"print", l_print_stdout},
    {"getenv", l_getenv},    {"setenv", l_setenv},
    {"which", l_which},      {"have", l_have},
    {"refresh_tools", l_refresh_tools},
    {"cwd", l_cwd},          {"sleep", l_sleep},
    {"now", l_now},          {"exists", l_exists},
    {"is_dir", l_is_dir},    {"is_file", l_is_file},
    {"mkdir", l_mkdir},      {"read", l_read},
    {"write", l_write},      {"rm", l_rm},
    {"list", l_list},        {"stat", l_stat},
    {"copy", l_copy},        {"move", l_move},
    {"chmod_x", l_chmod_x},  {"mkstemp", l_mkstemp},
    {"spawn", l_spawn},      {"run", l_run},
    {"capture", l_capture},  {"sh", l_sh},
    {"exec", l_exec},        {"shell_quote", l_shell_quote},
    {"trim", l_trim},        {"split", l_split},
    {"join", l_join},        {"basename", l_basename},
    {"dirname", l_dirname},  {"ext", l_ext},
    {"join_path", l_join_path},{"abs", l_abs},
    {"i18n", l_i18n},        {"confirm", l_confirm},
    {"ask", l_ask},          {"select", l_select},
    {"message", l_message},  {"download", l_download},
    {"open", l_open},        {"style", l_style},
    {"colorize", l_colorize},{"json_encode", l_json_encode},
    {"json_decode", l_json_decode}, {"platform", l_platform},
    {0, 0},
};

static const luaL_Reg gg_progress_methods[] = {
    {"set", lprog_set}, {"log", lprog_log}, {"done", lprog_done}, {0, 0},
};

static void open_tui(lua_State *l) {
  lua_newtable(l);
  lua_pushcfunction(l, ltui_available);
  lua_setfield(l, -2, "available");
  lua_pushcfunction(l, ltui_size);
  lua_setfield(l, -2, "size");
  lua_pushcfunction(l, ltui_begin);
  lua_setfield(l, -2, "begin");
  lua_pushcfunction(l, ltui_end);
  lua_setfield(l, -2, "end");
  lua_pushcfunction(l, ltui_menu);
  lua_setfield(l, -2, "menu");
  lua_pushcfunction(l, ltui_form);
  lua_setfield(l, -2, "form");
  lua_pushcfunction(l, ltui_textbox);
  lua_setfield(l, -2, "textbox");
  lua_pushcfunction(l, ltui_progress);
  lua_setfield(l, -2, "progress");
  lua_pushcfunction(l, ltui_message);
  lua_setfield(l, -2, "message");
  lua_pushcfunction(l, ltui_confirm);
  lua_setfield(l, -2, "confirm");
  lua_pushcfunction(l, ltui_prompt);
  lua_setfield(l, -2, "prompt");
  lua_setfield(l, -2, "tui");
}

static void open_registry(lua_State *l) {
  lua_newtable(l);
  lua_pushcfunction(l, lreg_list);
  lua_setfield(l, -2, "list");
  lua_pushcfunction(l, lreg_add);
  lua_setfield(l, -2, "add");
  lua_pushcfunction(l, lreg_remove);
  lua_setfield(l, -2, "remove");
  lua_pushcfunction(l, lreg_get);
  lua_setfield(l, -2, "get");
  lua_pushcfunction(l, lreg_log);
  lua_setfield(l, -2, "log");
  lua_setfield(l, -2, "registry");
}

static void open_modules(lua_State *l) {
  lua_newtable(l);
  lua_pushcfunction(l, lmod_list);
  lua_setfield(l, -2, "list");
  lua_pushcfunction(l, lmod_path);
  lua_setfield(l, -2, "path");
  lua_pushcfunction(l, lmod_install_examples);
  lua_setfield(l, -2, "install_examples");
  lua_setfield(l, -2, "modules");
}

void lua_register_gg(lua_State *l) {
  luaL_newmetatable(l, "gg.progress");
  lua_pushvalue(l, -1);
  lua_setfield(l, -2, "__index");
  luaL_setfuncs(l, gg_progress_methods, 0);
  lua_pushcfunction(l, lprog_gc);
  lua_setfield(l, -2, "__gc");
  lua_pop(l, 1);
}

static void push_gg_table(lua_State *l) {
  lua_newtable(l);
  luaL_setfuncs(l, gg_funcs, 0);

  /* aliases with nicer names */
  lua_getfield(l, -1, "which");
  lua_setfield(l, -2, "which_path");
  lua_getfield(l, -1, "list");
  lua_setfield(l, -2, "ls");
  lua_getfield(l, -1, "json_encode");
  lua_setfield(l, -2, "json");
  lua_getfield(l, -1, "rm");
  lua_setfield(l, -2, "remove");

  /* json table */
  lua_newtable(l);
  lua_pushcfunction(l, l_json_encode);
  lua_setfield(l, -2, "encode");
  lua_pushcfunction(l, l_json_decode);
  lua_setfield(l, -2, "decode");
  lua_setfield(l, -2, "json");

  /* strings table */
  lua_newtable(l);
  lua_pushcfunction(l, l_trim);
  lua_setfield(l, -2, "trim");
  lua_pushcfunction(l, l_split);
  lua_setfield(l, -2, "split");
  lua_pushcfunction(l, l_join);
  lua_setfield(l, -2, "join");
  lua_pushcfunction(l, l_shell_quote);
  lua_setfield(l, -2, "quote");
  lua_setfield(l, -2, "str");

  /* fs table */
  lua_newtable(l);
  lua_pushcfunction(l, l_read);
  lua_setfield(l, -2, "read");
  lua_pushcfunction(l, l_write);
  lua_setfield(l, -2, "write");
  lua_pushcfunction(l, l_exists);
  lua_setfield(l, -2, "exists");
  lua_pushcfunction(l, l_mkdir);
  lua_setfield(l, -2, "mkdir");
  lua_pushcfunction(l, l_list);
  lua_setfield(l, -2, "list");
  lua_pushcfunction(l, l_rm);
  lua_setfield(l, -2, "rm");
  lua_pushcfunction(l, l_copy);
  lua_setfield(l, -2, "copy");
  lua_pushcfunction(l, l_move);
  lua_setfield(l, -2, "move");
  lua_pushcfunction(l, l_stat);
  lua_setfield(l, -2, "stat");
  lua_pushcfunction(l, l_mkstemp);
  lua_setfield(l, -2, "tempname");
  lua_setfield(l, -2, "fs");

  /* process table */
  lua_newtable(l);
  lua_pushcfunction(l, l_spawn);
  lua_setfield(l, -2, "spawn");
  lua_pushcfunction(l, l_run);
  lua_setfield(l, -2, "run");
  lua_pushcfunction(l, l_capture);
  lua_setfield(l, -2, "capture");
  lua_pushcfunction(l, l_sh);
  lua_setfield(l, -2, "sh");
  lua_pushcfunction(l, l_exec);
  lua_setfield(l, -2, "exec");
  lua_pushcfunction(l, l_which);
  lua_setfield(l, -2, "which");
  lua_pushcfunction(l, l_have);
  lua_setfield(l, -2, "have");
  lua_setfield(l, -2, "proc");

  /* assets table: what this binary carries in its appended zip */
  lua_newtable(l);
  lua_pushcfunction(l, l_assets_read);
  lua_setfield(l, -2, "read");
  lua_pushcfunction(l, l_assets_list);
  lua_setfield(l, -2, "list");
  lua_pushcfunction(l, l_assets_have);
  lua_setfield(l, -2, "have");
  lua_pushcfunction(l, l_assets_dir);
  lua_setfield(l, -2, "dir");
  lua_setfield(l, -2, "assets");

  /* platform table */
  lua_pushcfunction(l, l_platform);
  lua_call(l, 0, 1);
  lua_setfield(l, -2, "platform");

  /* meta information */
  lua_pushstring(l, GG_VERSION);
  lua_setfield(l, -2, "version");
  lua_pushstring(l, GG_URL);
  lua_setfield(l, -2, "url");
  lua_pushstring(l, gg_home());
  lua_setfield(l, -2, "home");
  lua_pushstring(l, gg_dir());
  lua_setfield(l, -2, "dir");
  lua_pushstring(l, gg_modules_dir());
  lua_setfield(l, -2, "modules_dir");
  lua_pushstring(l, gg_bin_dir());
  lua_setfield(l, -2, "bin_dir");
  lua_pushstring(l, gg_config_path());
  lua_setfield(l, -2, "config_path");
  lua_pushboolean(l, T.interactive);
  lua_setfield(l, -2, "interactive");
  lua_pushstring(l, gg_lang_zh() ? "zh" : "en");
  lua_setfield(l, -2, "lang");
  lua_pushstring(l, gg_exe_path());
  lua_setfield(l, -2, "exe");
  lua_pushboolean(l, bundle_present());
  lua_setfield(l, -2, "bundled");
  open_tui(l);
  open_registry(l);
  open_modules(l);
}

/* ------------------------------------------------------------------ */
/* running code                                                        */
/* ------------------------------------------------------------------ */

static int traceback(lua_State *l) {
  const char *msg = lua_tostring(l, 1);
  if (msg) luaL_traceback(l, l, msg, 1);
  else lua_pushstring(l, "(error)");
  return 1;
}

static int pcall_protected(lua_State *l, int nargs, int nresults) {
  int base = lua_gettop(l) - nargs;
  lua_pushcfunction(l, traceback);
  lua_insert(l, base);
  int rc = lua_pcall(l, nargs, nresults, base);
  lua_remove(l, base);
  return rc;
}

int lua_open_runtime(void) {
  if (L) return 0;
  L = luaL_newstate();
  if (!L) return -1;
  luaL_openlibs(L);
  lua_register_gg(L);
  push_gg_table(L);
  lua_setglobal(L, "gg");
  /* preload the config file if the user has one */
  if (gg_is_file(gg_config_path())) {
    if (luaL_loadfile(L, gg_config_path()) != 0 ||
        pcall_protected(L, 0, 1) != 0) {
      gg_warn("%s: %s", gg_config_path(), lua_tostring(L, -1));
      lua_pop(L, 1);
    } else if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "lang");
      if (lua_isstring(L, -1)) {
        const char *lang = lua_tostring(L, -1);
        gg_lang_set(gg_startswith(lang, "zh"));
      }
      lua_pop(L, 1);
      lua_getfield(L, -1, "quiet");
      int quiet = lua_toboolean(L, -1);
      lua_pop(L, 2);
      (void)quiet;
    } else {
      lua_pop(L, 1);
    }
  }
  /* a bundle may ship an init.lua: run it once, now that gg is ready */
  l_bundle_run_init(L);
  return 0;
}

int lua_close_runtime(void) {
  if (L) lua_close(L);
  L = 0;
  return 0;
}

int lua_run_string(const char *code, const char *chunkname) {
  if (!L && lua_open_runtime() != 0) return 1;
  if (luaL_loadbuffer(L, code, strlen(code), chunkname) != 0) {
    gg_error("%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return 1;
  }
  if (pcall_protected(L, 0, 1) != 0) {
    gg_error("%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return 1;
  }
  if (lua_isnumber(L, -1)) {
    int rc = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return rc;
  }
  lua_pop(L, 1);
  return 0;
}

int lua_run_file(const char *path, int argc, char **argv) {
  if (!L && lua_open_runtime() != 0) return 1;
  char *abs = gg_path_abs(path);
  if (!gg_is_file(abs)) {
    gg_error("%s: %s", abs, strerror(ENOENT));
    free(abs);
    return 1;
  }
  /* create the `arg` table like the standalone lua interpreter does */
  lua_newtable(L);
  lua_pushstring(L, abs);
  lua_rawseti(L, -2, 0);
  for (int i = 0; i < argc; i++) {
    lua_pushstring(L, argv[i]);
    lua_rawseti(L, -2, i + 1);
  }
  lua_setglobal(L, "arg");
  free(abs);
  if (luaL_loadfile(L, path) != 0) {
    gg_error("%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return 1;
  }
  if (pcall_protected(L, 0, 1) != 0) {
    gg_error("%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return 1;
  }
  int rc = 0;
  if (lua_isnumber(L, -1)) rc = (int)lua_tointeger(L, -1);
  lua_pop(L, 1);
  return rc;
}

int lua_check_syntax(const char *path, char **err) {
  if (!L && lua_open_runtime() != 0) return 1;
  if (luaL_loadfile(L, path) != 0) {
    if (err) *err = gg_strdup(lua_tostring(L, -1));
    lua_pop(L, 1);
    return 1;
  }
  lua_pop(L, 1);
  return 0;
}

int lua_eval_expr_string(const char *expr, char **out) {
  if (!L && lua_open_runtime() != 0) return 1;
  char *code = gg_asprintf("return tostring(%s)", expr);
  int rc = luaL_loadbuffer(L, code, strlen(code), "=expr");
  free(code);
  if (rc != 0) {
    if (out) *out = gg_strdup(lua_tostring(L, -1));
    lua_pop(L, 1);
    return 1;
  }
  if (pcall_protected(L, 0, 1) != 0) {
    if (out) *out = gg_strdup(lua_tostring(L, -1));
    lua_pop(L, 1);
    return 1;
  }
  if (out) *out = gg_strdup(lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
  lua_pop(L, 1);
  return 0;
}

/* ------------------------------------------------------------------ */
/* module runner                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
  char name[64];
  char shrt[8];
  char lng[64];
  char type[16]; /* string | int | number | bool | choice */
  int required;
  int has_default;
  char *def;
  int pos;
  char help[256];
  char label[128];
  char **choices;
  int nchoices;
  int provided;
} param_t;

static void params_free(param_t *p, int n) {
  for (int i = 0; i < n; i++) {
    free(p[i].def);
    for (int c = 0; c < p[i].nchoices; c++) free(p[i].choices[c]);
    free(p[i].choices);
  }
}

static int parse_params(lua_State *l, int idx, param_t *out, int maxn) {
  lua_getfield(l, idx, "params");
  if (!lua_istable(l, -1)) {
    lua_pop(l, 1);
    return 0;
  }
  int n = (int)lua_rawlen(l, -1);
  if (n > maxn) n = maxn;
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(l, -1, i);
    param_t *p = &out[i - 1];
    memset(p, 0, sizeof(*p));
    lua_getfield(l, -1, "name");
    snprintf(p->name, sizeof(p->name), "%s", lua_tostring(l, -1) ? lua_tostring(l, -1) : "");
    lua_pop(l, 1);
    lua_getfield(l, -1, "short");
    if (lua_isstring(l, -1))
      snprintf(p->shrt, sizeof(p->shrt), "%s", lua_tostring(l, -1));
    lua_pop(l, 1);
    lua_getfield(l, -1, "long");
    if (lua_isstring(l, -1))
      snprintf(p->lng, sizeof(p->lng), "%s", lua_tostring(l, -1));
    lua_pop(l, 1);
    lua_getfield(l, -1, "type");
    snprintf(p->type, sizeof(p->type), "%s",
             lua_isstring(l, -1) ? lua_tostring(l, -1) : "string");
    lua_pop(l, 1);
    lua_getfield(l, -1, "required");
    p->required = lua_toboolean(l, -1);
    lua_pop(l, 1);
    lua_getfield(l, -1, "pos");
    p->pos = (int)lua_tointeger(l, -1);
    lua_pop(l, 1);
    lua_getfield(l, -1, "help");
    if (lua_isstring(l, -1))
      snprintf(p->help, sizeof(p->help), "%s", lua_tostring(l, -1));
    lua_pop(l, 1);
    lua_getfield(l, -1, "label");
    if (lua_isstring(l, -1))
      snprintf(p->label, sizeof(p->label), "%s", lua_tostring(l, -1));
    lua_pop(l, 1);
    lua_getfield(l, -1, "default");
    if (!lua_isnil(l, -1)) {
      p->has_default = 1;
      if (lua_isboolean(l, -1))
        p->def = gg_strdup(lua_toboolean(l, -1) ? "1" : "0");
      else
        p->def = gg_strdup(lua_tostring(l, -1) ? lua_tostring(l, -1) : "");
    }
    lua_pop(l, 1);
    lua_getfield(l, -1, "choices");
    if (lua_istable(l, -1)) {
      int cn = (int)lua_rawlen(l, -1);
      p->choices = malloc(sizeof(char *) * (size_t)(cn + 1));
      for (int c = 1; c <= cn; c++) {
        lua_rawgeti(l, -1, c);
        p->choices[c - 1] = gg_strdup(lua_tostring(l, -1) ? lua_tostring(l, -1) : "");
        lua_pop(l, 1);
      }
      p->nchoices = cn;
    }
    lua_pop(l, 1);
    lua_pop(l, 1); /* param table */
  }
  lua_pop(l, 1); /* params */
  return n;
}

static param_t *find_param(param_t *p, int n, const char *name) {
  for (int i = 0; i < n; i++) {
    if (gg_streq(p[i].name, name)) return &p[i];
    if (p[i].shrt[0] && gg_streq(p[i].shrt, name)) return &p[i];
    if (p[i].lng[0] && gg_streq(p[i].lng, name)) return &p[i];
  }
  return 0;
}

static void set_ctx_value(lua_State *l, int tblidx, const char *name,
                          const char *value, const char *type) {
  if (gg_streq(type, "int")) {
    lua_pushinteger(l, (lua_Integer)strtoll(value ? value : "0", 0, 10));
  } else if (gg_streq(type, "number") || gg_streq(type, "float")) {
    lua_pushnumber(l, strtod(value ? value : "0", 0));
  } else if (gg_streq(type, "bool")) {
    lua_pushboolean(l, value && (gg_streq(value, "1") || gg_streq(value, "true")));
  } else if (!value) {
    lua_pushnil(l);
  } else {
    lua_pushstring(l, value);
  }
  lua_setfield(l, tblidx, name);
}

static void usage_for(const char *name, module_info *m, param_t *p, int n) {
  printf("%sgg %s%s", c_bold(), name, c_reset());
  for (int i = 0; i < n; i++) {
    if (p[i].pos) continue;
    const char *flag = p[i].shrt[0] ? p[i].shrt : p[i].name;
    printf(" %s%s%s", p[i].required ? "" : "[", flag, p[i].required ? "" : "]");
  }
  printf(" [args]\n\n");
  if (m->title && *m->title) printf("  %s%s%s\n", c_bold(), m->title, c_reset());
  if (m->desc && *m->desc) printf("  %s\n", m->desc);
  if (n) printf("\n%s%s%s\n", c_bold(), gg_tr("options:", "选项:"), c_reset());
  for (int i = 0; i < n; i++) {
    char left[128];
    if (p[i].shrt[0] && p[i].lng[0])
      snprintf(left, sizeof(left), "-%s, --%s", p[i].shrt, p[i].lng);
    else if (p[i].shrt[0])
      snprintf(left, sizeof(left), "-%s", p[i].shrt);
    else if (p[i].lng[0])
      snprintf(left, sizeof(left), "--%s", p[i].lng);
    else
      snprintf(left, sizeof(left), "--%s", p[i].name);
    printf("  %s%-20s%s %s", c_accent(), left, c_reset(), p[i].help);
    if (p[i].nchoices) {
      printf(" [");
      for (int c = 0; c < p[i].nchoices; c++) printf("%s%s", c ? "|" : "", p[i].choices[c]);
      printf("]");
    }
    if (p[i].has_default)
      printf("%s (default: %s)%s", c_dim(), p[i].def, c_reset());
    if (p[i].pos) printf("%s (arg %d)%s", c_dim(), p[i].pos, c_reset());
    printf("\n");
  }
  printf("\n%s%s%s\n", c_dim(),
         gg_tr("run without arguments to get the interactive TUI form",
               "不带参数运行会打开交互式 TUI 表单"),
         c_reset());
}

/* fill ctx.args defaults for parameters the user did not provide */
static void apply_defaults(lua_State *l, int args_idx, param_t *p, int n) {
  for (int i = 0; i < n; i++) {
    if (p[i].provided || !p[i].has_default) continue;
    set_ctx_value(l, args_idx, p[i].name, p[i].def, p[i].type);
    p[i].provided = 1;
  }
}

/* auto generated TUI form for modules that do not implement M.tui */
static int auto_form(lua_State *l, const char *modname, int args_idx, param_t *p,
                     int n) {
  tui_field fields[64];
  int fn = 0;
  for (int i = 0; i < n; i++) {
    tui_field *f = &fields[fn];
    memset(f, 0, sizeof(*f));
    f->name = gg_strdup(p[i].name);
    f->label = gg_strdup(p[i].label[0] ? p[i].label : p[i].name);
    f->help = gg_strdup(p[i].help);
    if (gg_streq(p[i].type, "bool")) {
      f->kind = 1;
      lua_getfield(l, args_idx, p[i].name);
      int val = lua_isnil(l, -1) ? (p[i].has_default && gg_streq(p[i].def, "1"))
                                 : lua_toboolean(l, -1);
      lua_pop(l, 1);
      f->value = gg_strdup(val ? "1" : "0");
    } else if (gg_streq(p[i].type, "choice") || p[i].nchoices) {
      f->kind = 2;
      lua_getfield(l, args_idx, p[i].name);
      const char *cur = lua_isstring(l, -1) ? lua_tostring(l, -1)
                                            : (p[i].has_default ? p[i].def : "");
      f->value = gg_strdup(cur ? cur : "");
      lua_pop(l, 1);
      if (p[i].nchoices) {
        f->choices = (const char **)p[i].choices;
        f->nchoices = p[i].nchoices;
      } else {
        static const char *yesno[] = {"yes", "no"};
        f->choices = yesno;
        f->nchoices = 2;
      }
    } else {
      f->kind = 0;
      lua_getfield(l, args_idx, p[i].name);
      const char *cur = lua_isstring(l, -1) ? lua_tostring(l, -1)
                                            : (p[i].has_default ? p[i].def : "");
      f->value = gg_strdup(cur ? cur : "");
      lua_pop(l, 1);
    }
    fn++;
  }
  char title[256];
  snprintf(title, sizeof(title), "gg %s", modname);
  int rc = tui_form(title, gg_tr("fill in the parameters, ctrl-s to run",
                                 "填写参数，ctrl-s 执行"),
                    fields, fn);
  if (rc == 0) {
    for (int i = 0; i < fn; i++) {
      set_ctx_value(l, args_idx, fields[i].name, fields[i].value, p[i].type);
      p[i].provided = 1;
    }
  }
  for (int i = 0; i < fn; i++) {
    free((void *)fields[i].name);
    free((void *)fields[i].label);
    free((void *)fields[i].help);
    free(fields[i].value);
  }
  return rc;
}

/* substitute {name} placeholders inside an action command */
static char *subst(const char *in, lua_State *l, int args_idx) {
  sbuf b;
  sb_init(&b, 128);
  for (const char *p = in; *p;) {
    if (*p == '{') {
      const char *e = strchr(p, '}');
      if (e) {
        char key[64];
        size_t n = (size_t)(e - p - 1);
        if (n < sizeof(key)) {
          memcpy(key, p + 1, n);
          key[n] = 0;
          lua_getfield(l, args_idx, key);
          if (lua_isstring(l, -1) || lua_isnumber(l, -1)) {
            char *v = lua_getcstr(l, -1);
            sb_adds(&b, v);
            free(v);
            lua_pop(l, 1);
            p = e + 1;
            continue;
          }
          lua_pop(l, 1);
        }
      }
    }
    sb_addc(&b, *p++);
  }
  return b.p;
}

/* call gg.run{ argv = ..., sudo = ..., pause = ... } from C */
static int call_gg_run(lua_State *l, strvec *argv, int sudo, int pause) {
  lua_getglobal(l, "gg");
  lua_getfield(l, -1, "run");
  lua_remove(l, -2);
  if (!lua_isfunction(l, -1)) {
    lua_pop(l, 1);
    return 1;
  }
  lua_newtable(l);
  int idx = lua_gettop(l);
  lua_newtable(l);
  for (int i = 0; i < argv->n; i++) {
    lua_pushstring(l, argv->v[i]);
    lua_rawseti(l, -2, i + 1);
  }
  lua_setfield(l, idx, "argv");
  if (sudo) {
    lua_pushboolean(l, 1);
    lua_setfield(l, idx, "sudo");
  }
  lua_pushboolean(l, pause);
  lua_setfield(l, idx, "pause");
  if (pcall_protected(l, 1, 2) != 0) {
    gg_error("%s", lua_tostring(l, -1));
    lua_pop(l, 1);
    return 1;
  }
  int ok = lua_toboolean(l, -2);
  lua_pop(l, 2);
  return ok ? 0 : 1;
}

/* interactive menu built from M.actions */
static int run_actions(lua_State *l, const char *modname, int mod_idx,
                       int args_idx, param_t *params, int nparams) {
  lua_getfield(l, mod_idx, "actions");
  if (!lua_istable(l, -1)) {
    lua_pop(l, 1);
    return -1;
  }
  int act_idx = lua_gettop(l);
  while (1) {
    int n = (int)lua_rawlen(l, act_idx);
    if (!n) {
      lua_pop(l, 1);
      return -1;
    }
    const char **items = malloc(sizeof(char *) * (size_t)n);
    for (int i = 1; i <= n; i++) {
      lua_rawgeti(l, act_idx, i);
      lua_getfield(l, -1, "name");
      const char *an = lua_tostring(l, -1);
      lua_getfield(l, -2, "desc");
      const char *ad = lua_tostring(l, -1);
      items[i - 1] = gg_asprintf("%-16s %s%s%s", an ? an : "",
                                 tui_style(ST_MUTED), ad ? ad : "",
                                 tui_style(ST_RESET));
      lua_pop(l, 3);
    }
    char title[256];
    snprintf(title, sizeof(title), "gg %s", modname);
    tui_menu m;
    tui_menu_init(&m, title, items, n);
    m.status = gg_tr("esc to go back", "esc 返回");
    int rc = tui_menu_run(&m, 0);
    for (int i = 0; i < n; i++) free((void *)items[i]);
    free(items);
    if (rc < 0) {
      lua_pop(l, 1);
      return 0;
    }
    lua_rawgeti(l, act_idx, rc + 1);
    int action_idx = lua_gettop(l);
    /* collect missing params for this action */
    lua_getfield(l, action_idx, "params");
    if (lua_istable(l, -1)) {
      int pn = (int)lua_rawlen(l, -1);
      for (int i = 1; i <= pn; i++) {
        lua_rawgeti(l, -1, i);
        const char *pname = lua_tostring(l, -1);
        lua_pop(l, 1);
        if (!pname) continue;
        param_t *p = find_param(params, nparams, pname);
        char buf[1024] = "";
        lua_getfield(l, args_idx, pname);
        if (lua_isstring(l, -1)) snprintf(buf, sizeof(buf), "%s", lua_tostring(l, -1));
        lua_pop(l, 1);
        char label[160];
        snprintf(label, sizeof(label), "%s: ", p && p->label[0] ? p->label : pname);
        if (p && gg_streq(p->type, "bool")) {
          int def = buf[0] ? gg_streq(buf, "1") : 0;
          int v = tui_confirm(gg_tr("confirm", "确认"), p->help[0] ? p->help : pname, def);
          set_ctx_value(l, args_idx, pname, v ? "1" : "0", "bool");
        } else if (p && (p->nchoices || gg_streq(p->type, "choice"))) {
          const char **items2 = malloc(sizeof(char *) * (size_t)(p->nchoices + 1));
          for (int c = 0; c < p->nchoices; c++) items2[c] = p->choices[c];
          int sel = tui_select(gg_tr("choose", "请选择"), p->help, items2, p->nchoices);
          free(items2);
          if (sel < 0) goto action_done;
          set_ctx_value(l, args_idx, pname, p->choices[sel], p->type);
        } else {
          if (tui_prompt(gg_tr("input", "输入"), label, buf, sizeof(buf),
                         p ? p->help : 0) != 0)
            goto action_done;
          set_ctx_value(l, args_idx, pname, buf, p ? p->type : "string");
        }
      }
    }
    lua_pop(l, 1); /* params */
    {
      lua_getfield(l, action_idx, "cmd");
      strvec argv;
      sv_init(&argv);
      if (lua_isstring(l, -1)) {
        char *s = subst(lua_tostring(l, -1), l, args_idx);
        if (s && *s) sv_push(&argv, s);
        free(s);
      } else if (lua_istable(l, -1)) {
        int cn = (int)lua_rawlen(l, -1);
        for (int c = 1; c <= cn; c++) {
          lua_rawgeti(l, -1, c);
          if (lua_isstring(l, -1)) {
            char *s = subst(lua_tostring(l, -1), l, args_idx);
            if (*s) sv_push(&argv, s);
            free(s);
          }
          lua_pop(l, 1);
        }
      }
      lua_pop(l, 1);
      int sudo = 0, pause = 1;
      lua_getfield(l, action_idx, "sudo");
      sudo = lua_toboolean(l, -1);
      lua_pop(l, 1);
      lua_getfield(l, action_idx, "pause");
      if (!lua_isnil(l, -1)) pause = lua_toboolean(l, -1);
      lua_pop(l, 1);
      if (argv.n) {
        reg_log(modname, argv.v[0] ? gg_join_argv(sv_argv(&argv)) : "");
        call_gg_run(l, &argv, sudo, pause);
      }
      sv_free(&argv);
    }
  action_done:
    lua_pop(l, 1); /* action */
  }
}

int lua_run_module(const char *name, int argc, char **argv, int force_tui) {
  if (!L && lua_open_runtime() != 0) return 1;
  module_info *m = modules_find(name);
  if (!m) {
    gg_error("%s: %s", name, gg_tr("no such module", "没有这个模块"));
    return 1;
  }
  /* an on-disk path is only interesting for the chunk name: running a
   * built-in or bundled module should not litter ~/.gg/modules (that is
   * what `gg edit`/`gg show` are for), otherwise the first run would
   * shadow the bundle with a copy of itself. */
  char *path = (m->path && !m->bundled) ? gg_strdup(m->path) : 0;
  char chunkname[512];
  size_t srclen = 0;
  int kind = 0;
  char *src = module_source_text(name, &srclen, &kind);
  int rc;
  if (!src) {
    gg_error("%s: %s", name, gg_tr("module source not found", "找不到模块源码"));
    free(path);
    return 1;
  }
  if (kind == 0) snprintf(chunkname, sizeof(chunkname), "@%s(builtin)", name);
  else if (kind == 1) snprintf(chunkname, sizeof(chunkname), "@%s(bundled)", name);
  else snprintf(chunkname, sizeof(chunkname), "@%s", m->path ? m->path : name);
  rc = luaL_loadbuffer(L, src, srclen, chunkname);
  free(src);
  if (rc != 0) {
    gg_error("%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    free(path);
    return 1;
  }
  if (pcall_protected(L, 0, 1) != 0) {
    gg_error("%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    free(path);
    return 1;
  }
  if (!lua_istable(L, -1)) {
    gg_error("%s: %s", name, gg_tr("the module must return a table",
                                   "模块必须返回一个表"));
    lua_pop(L, 1);
    free(path);
    return 1;
  }
  int mod_idx = lua_gettop(L);
  /* refresh metadata from the returned table */
  lua_getfield(L, mod_idx, "title");
  if (lua_isstring(L, -1)) {
    free(m->title);
    m->title = gg_strdup(lua_tostring(L, -1));
  }
  lua_pop(L, 1);
  lua_getfield(L, mod_idx, "desc");
  if (lua_isstring(L, -1)) {
    free(m->desc);
    m->desc = gg_strdup(lua_tostring(L, -1));
  }
  lua_pop(L, 1);

  param_t params[64];
  int nparams = parse_params(L, mod_idx, params, 64);

  lua_newtable(L);
  int args_idx = lua_gettop(L);
  lua_newtable(L);
  int rest_idx = lua_gettop(L);

  int want_help = 0;
  int want_tui = force_tui;
  int restn = 0;
  int pos[64], npos = 0;
  for (int k = 0; k < nparams; k++) {
    if (params[k].pos > 0) pos[npos++] = k;
  }
  /* sort positional params by their declared position */
  for (int a = 0; a < npos; a++)
    for (int b = a + 1; b < npos; b++)
      if (params[pos[b]].pos < params[pos[a]].pos) {
        int t = pos[a];
        pos[a] = pos[b];
        pos[b] = t;
      }
  int next_pos = 0;

  for (int i = 0; i < argc; i++) {
    const char *a = argv[i];
    if (gg_streq(a, "--")) {
      for (int k = i + 1; k < argc; k++) {
        lua_pushstring(L, argv[k]);
        lua_rawseti(L, rest_idx, ++restn);
      }
      break;
    }
    if (arg_is_flag(a, "-h", "--help")) {
      want_help = 1;
      continue;
    }
    if (gg_streq(a, "--tui")) {
      want_tui = 1;
      continue;
    }
    if (a[0] == '-' && a[1] && !isdigit((unsigned char)a[1])) {
      int is_long = a[1] == '-';
      char keybuf[128];
      snprintf(keybuf, sizeof(keybuf), "%s", is_long ? a + 2 : a + 1);
      char *val = 0;
      char *eq = strchr(keybuf, '=');
      if (eq) {
        *eq = 0;
        val = eq + 1;
      }
      int negated = 0;
      if (is_long && gg_startswith(keybuf, "no-")) {
        negated = 1;
        memmove(keybuf, keybuf + 3, strlen(keybuf + 3) + 1);
      }
      param_t *p = find_param(params, nparams, keybuf);
      if (!p) {
        gg_warn("%s %s", gg_tr("unknown option, ignored:", "未知选项，已忽略:"), a);
        continue;
      }
      int is_bool = gg_streq(p->type, "bool");
      if (!val && !is_bool) {
        if (i + 1 < argc) val = argv[++i];
        else {
          gg_error("%s %s", gg_tr("missing value for", "缺少参数值:"), a);
          params_free(params, nparams);
          free(path);
          return 1;
        }
      }
      if (is_bool && !val) val = negated ? "0" : "1";
      set_ctx_value(L, args_idx, p->name, val, p->type);
      p->provided = 1;
      continue;
    }
    /* positional argument */
    if (next_pos < npos) {
      param_t *p = &params[pos[next_pos]];
      if (gg_streq(p->type, "string") && p->provided) {
        /* append into a table for variadic positionals */
        lua_getfield(L, args_idx, p->name);
        if (lua_istable(L, -1)) {
          int n = (int)lua_rawlen(L, -1) + 1;
          lua_pushstring(L, a);
          lua_rawseti(L, -2, n);
          lua_pop(L, 1);
        } else {
          lua_pop(L, 1);
          set_ctx_value(L, args_idx, p->name, a, p->type);
        }
      } else {
        set_ctx_value(L, args_idx, p->name, a, p->type);
        p->provided = 1;
        next_pos++;
      }
      continue;
    }
    lua_pushstring(L, a);
    lua_rawseti(L, rest_idx, ++restn);
  }

  if (want_help) {
    usage_for(name, m, params, nparams);
    params_free(params, nparams);
    free(path);
    return 0;
  }

  /* required argument check decides whether we need the TUI */
  int missing = 0;
  for (int i = 0; i < nparams; i++) {
    if (params[i].required && !params[i].provided) missing++;
  }
  if (!want_tui && T.interactive && (missing || (argc == 0 && nparams > 0)))
    want_tui = 1;

  if (missing && !T.interactive) {
    gg_error("%s: %s", name,
             gg_tr("missing required arguments (see --help)",
                   "缺少必需参数（用 --help 查看用法）"));
    usage_for(name, m, params, nparams);
    params_free(params, nparams);
    free(path);
    return 1;
  }

  /* build ctx */
  lua_newtable(L);
  int ctx_idx = lua_gettop(L);
  lua_pushstring(L, name);
  lua_setfield(L, ctx_idx, "name");
  char *module_dir = path ? gg_dirname_of(path) : gg_strdup("");
  lua_pushstring(L, module_dir);
  lua_setfield(L, ctx_idx, "module_dir");
  free(module_dir);
  lua_pushvalue(L, args_idx);
  lua_setfield(L, ctx_idx, "args");
  lua_pushvalue(L, rest_idx);
  lua_setfield(L, ctx_idx, "rest");
  lua_newtable(L);
  for (int i = 0; i < argc; i++) {
    lua_pushstring(L, argv[i]);
    lua_rawseti(L, -2, i + 1);
  }
  lua_setfield(L, ctx_idx, "argv");
  lua_pushvalue(L, mod_idx);
  lua_setfield(L, ctx_idx, "module");
  /* metatable: __index = gg */
  lua_newtable(L);
  lua_getglobal(L, "gg");
  lua_setfield(L, -2, "__index");
  lua_setmetatable(L, ctx_idx);

  apply_defaults(L, args_idx, params, nparams);

  if (want_tui) {
    lua_getfield(L, mod_idx, "tui");
    if (lua_isfunction(L, -1)) {
      lua_pushvalue(L, ctx_idx);
      if (pcall_protected(L, 1, 1) != 0) {
        gg_error("%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        params_free(params, nparams);
        free(path);
        return 1;
      }
      int v = 0;
      if (lua_isnumber(L, -1)) v = (int)lua_tointeger(L, -1);
      lua_pop(L, 1);
      params_free(params, nparams);
      free(path);
      return v;
    }
    lua_pop(L, 1);
    lua_getfield(L, mod_idx, "actions");
    int has_actions = lua_istable(L, -1);
    lua_pop(L, 1);
    if (has_actions) {
      int v = run_actions(L, name, mod_idx, args_idx, params, nparams);
      if (v >= 0) {
        params_free(params, nparams);
        free(path);
        return v;
      }
    }
    if (nparams) {
      if (auto_form(L, name, args_idx, params, nparams) != 0) {
        params_free(params, nparams);
        free(path);
        return 0;
      }
    }
  }
  apply_defaults(L, args_idx, params, nparams);

  lua_getfield(L, mod_idx, "run");
  if (!lua_isfunction(L, -1)) {
    gg_error("%s: %s", name, gg_tr("module has no run() function",
                                   "模块没有 run() 函数"));
    lua_pop(L, 1);
    params_free(params, nparams);
    free(path);
    return 1;
  }
  lua_pushvalue(L, ctx_idx);
  if (pcall_protected(L, 1, 1) != 0) {
    gg_error("%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    params_free(params, nparams);
    free(path);
    return 1;
  }
  int v = 0;
  if (lua_isnumber(L, -1)) v = (int)lua_tointeger(L, -1);
  else if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) v = 1;
  lua_pop(L, 1);
  params_free(params, nparams);
  free(path);
  return v;
}
