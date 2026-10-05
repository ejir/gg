/* gg — entry point, argument dispatch and the interactive dashboard. */
#include "gg.h"

/* ------------------------------------------------------------------ */
/* help                                                                */
/* ------------------------------------------------------------------ */

static void help_top(void) {
  printf("%sgg %s%s — %s\n\n", c_bold(), GG_VERSION, c_reset(),
         gg_tr("one APE binary, extended with Lua scripts",
               "一个 APE 单文件二进制，用 Lua 脚本扩展"));
  printf("%s%s%s\n", c_bold(), gg_tr("usage", "用法"), c_reset());
  printf("  gg                              %s\n",
         gg_tr("interactive dashboard", "交互式面板"));
  printf("  gg <module|command> [args...]   %s\n",
         gg_tr("run a module or registered command", "运行模块或已注册命令"));
  printf("  gg <anything> [args...]         %s\n",
         gg_tr("fall back to the system PATH", "回退到系统 PATH 执行"));
  printf("  gg <script.lua> [args...]       %s\n",
         gg_tr("run a lua script", "运行 Lua 脚本"));
  printf("  gg -e 'lua code'                %s\n", gg_tr("evaluate", "执行代码"));
  printf("  gg repl                         %s\n",
         gg_tr("interactive lua", "Lua 交互环境"));
  printf("\n%s%s%s\n", c_bold(), gg_tr("built-ins", "内置命令"), c_reset());
  printf("  %sactive%s [status|off]     %s\n", c_accent(), c_reset(),
         gg_tr("put ~/.gg/bin on PATH (bash/zsh/fish/powershell/cmd)",
               "把 ~/.gg/bin 加入 PATH（bash/zsh/fish/powershell/cmd）"));
  printf("  %sls%s | %sshow%s <name>      %s\n", c_accent(), c_reset(), c_accent(),
         c_reset(), gg_tr("list / describe everything", "列出 / 查看"));
  printf("  %sadd%s <name> <cmd>        %s\n", c_accent(), c_reset(),
         gg_tr("register a command line", "注册一条命令行"));
  printf("  %srm%s <name>               %s\n", c_accent(), c_reset(),
         gg_tr("delete a module or command", "删除模块或命令"));
  printf("  %sinit%s <name> [--actions] %s\n", c_accent(), c_reset(),
         gg_tr("scaffold a lua module", "生成 Lua 模块骨架"));
  printf("  %sedit%s <name>             %s\n", c_accent(), c_reset(),
         gg_tr("built-in editor", "内置编辑器"));
  printf("  %sedit%s or %slink%s <name>     %s\n", c_accent(), c_reset(), c_accent(),
         c_reset(), gg_tr("create a shell shim", "创建 shell 快捷命令"));
  printf("  %srun%s <command line>      %s\n", c_accent(), c_reset(),
         gg_tr("run through the shell", "通过 shell 执行"));
  printf("  %smodules%s install-examples %s\n", c_accent(), c_reset(),
         gg_tr("copy the bundled modules", "安装自带示例模块"));
  printf("  %sdoctor%s | %sconfig%s | %supgrade%s\n", c_accent(), c_reset(),
         c_accent(), c_reset(), c_accent(), c_reset());
  printf("  %shelp%s <topic>             %s\n", c_accent(), c_reset(),
         gg_tr("topics: modules lua active paths bundle",
               "主题: modules lua active paths bundle"));
  printf("\n%s%s%s\n", c_dim(),
         gg_tr("first run:  gg active   then reopen your shell",
               "首次使用:  gg active   然后重开终端"),
         c_reset());
}

static void help_topic(const char *topic) {
  if (gg_streq(topic, "modules") || gg_streq(topic, "module")) {
    printf("%s%s%s\n\n", c_bold(), gg_tr("writing a module", "编写模块"), c_reset());
    printf("%s\n", gg_tr(
        "A module is a lua file in ~/.gg/modules/<name>.lua that returns a\n"
        "table.  Give it a title, a description, optional params and either a\n"
        "run(ctx) function or an actions list.\n\n"
        "    local M = {}\n"
        "    M.title = \"my tool\"\n"
        "    M.desc  = \"does something useful\"\n"
        "    M.params = {\n"
        "      { name=\"input\", pos=1, type=\"string\", required=true,\n"
        "        label=\"input file\", help=\"what to read\" },\n"
        "      { name=\"force\", short=\"f\", type=\"bool\", label=\"force\" },\n"
        "    }\n"
        "    function M.run(ctx)\n"
        "      ctx.log(\"input=%s force=%s\", ctx.args.input, tostring(ctx.args.force))\n"
        "      ctx.run({ \"ls\", \"-la\" })          -- interactive child process\n"
        "      local out = ctx.capture(\"uname -a\") -- capture stdout\n"
        "      ctx.log(out)\n"
        "      return 0\n"
        "    end\n"
        "    return M\n",
        "模块就是 ~/.gg/modules/<名字>.lua，返回一个表。写 M.title / M.desc /\n"
        "M.params 以及 M.run(ctx)；如果想要自己的界面就再加一个 M.tui(ctx)。\n"
        "常用字段见 examples/ 目录：\n"
        "  ctx.args.<name>   解析后的参数\n"
        "  ctx.rest          多余的位置参数数组\n"
        "  ctx.run{...}      前台运行子进程（TUI 会自动让出终端）\n"
        "  ctx.capture(cmd)  捕获输出\n"
        "  ctx.spawn{...}    运行并可选 on_line 回调实时读取输出\n"
        "  ctx.confirm/ask/select/message\n"
        "  ctx.tui.menu/form/textbox/progress\n"));
    printf("\n%s%s%s\n", c_dim(),
           gg_tr("examples: gg modules install-examples", "示例: gg modules install-examples"),
           c_reset());
  } else if (gg_streq(topic, "lua") || gg_streq(topic, "api")) {
    printf("%sgg api%s (also reachable as `ctx.*` inside a module)\n\n", c_bold(),
           c_reset());
    printf(
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n"
        "  %-28s %s\n",
        "gg.run{argv={...}}", "run a child in the foreground (TUI aware)",
        "gg.spawn{...}", "capture / stream output, on_line callback",
        "gg.capture(cmd)", "shell capture helper",
        "gg.sh(cmd)", "same but returns only the output",
        "gg.exec{...}", "replace the current process",
        "gg.which(name)", "resolve a program on PATH",
        "gg.have(name)", "true when a program exists",
        "gg.exists(path) / is_dir / is_file", "filesystem predicates",
        "gg.read/write/list/stat/rm/copy/move", "filesystem helpers",
        "gg.mkdir / mkstemp / chmod_x", "more filesystem helpers",
        "gg.join_path{...} / abs / basename", "path helpers",
        "gg.trim/split/join/quote", "string helpers",
        "gg.json.encode / gg.json.decode", "json in and out",
        "gg.getenv/setenv", "environment",
        "gg.log/info/warn/err/ok/print", "output helpers",
        "gg.i18n(en, zh)", "pick text by language",
        "gg.style(name) / gg.colorize(text, name)", "ansi colors",
        "gg.confirm/ask/select/message", "simple prompts",
        "gg.tui.menu/form/textbox/progress", "full screen widgets",
        "gg.tui.available()", "is a terminal attached",
        "gg.registry.add/list/remove", "the gg add registry",
        "gg.modules.list/path/install_examples", "module discovery",
        "gg.download(url[, dest])", "curl/wget wrapper",
        "gg.open(path_or_url)", "open with the desktop",
        "gg.platform", "os/arch/cpus/interactive/lang table",
        "gg.sleep(ms) / gg.now()", "time",
        "gg.version / gg.dir / gg.home", "about this build");
    printf("\n%s%s%s\n", c_dim(),
           gg_tr("see also: examples/*.lua in the repository",
                 "另见仓库中的 examples/*.lua"),
           c_reset());
  } else if (gg_streq(topic, "active") || gg_streq(topic, "paths")) {
    printf("%s%s%s\n\n", c_bold(), gg_tr("gg active", "gg active"), c_reset());
    printf("%s\n", gg_tr(
        "`gg active` makes `gg` and every shim you create available by name:\n"
        "  1. it writes ~/.gg/bin/gg (symlink or copy of this binary)\n"
        "  2. it appends a PATH block to the rc file of your shell\n"
        "  3. `gg active status` shows what happened, `gg active off` removes it\n"
        "  4. `gg active print` just prints the shell snippet\n"
        "  5. `gg active --shell fish|zsh|bash|powershell|cmd` forces a shell\n",
        "`gg active` 会让 `gg` 以及你创建的所有快捷命令可以直接输入名字运行：\n"
        "  1. 写入 ~/.gg/bin/gg（软链或复制本二进制）\n"
        "  2. 在 shell 配置文件中加入 PATH 配置块\n"
        "  3. `gg active status` 查看状态，`gg active off` 撤销\n"
        "  4. `gg active print` 只打印配置片段\n"
        "  5. `gg active --shell fish|zsh|bash|powershell|cmd` 强制指定 shell\n"));
  } else if (gg_streq(topic, "bundle") || gg_streq(topic, "script")) {
    printf("%s%s%s\n\n", c_bold(),
           gg_tr("self-contained scripts (gg bundle)", "自包含脚本（gg bundle）"),
           c_reset());
    printf("%s\n", gg_tr(
        "`gg bundle app.gg mod.lua assets/` writes a copy of this binary with\n"
        "a zip glued to it: modules/, assets/, init.lua and registry.tsv from\n"
        "that archive are used automatically, so app.gg runs your script on\n"
        "any machine — same file, no gg installation needed.\n\n"
        "  gg bundle app[.gg] [file-or-dir ...] [rm:pattern]...   [-f]\n"
        "  gg bundle app.gg rm:assets                             drop entries\n"
        "\n"
        "inside a module: gg.assets.read(name), gg.assets.list(),\n"
        "gg.assets.have(name), gg.assets.dir() (unpack everything).\n"
        "the plain lua way also works on the ape: io.open('/zip/assets/x')\n",
        "`gg bundle app.gg mod.lua assets/` 会把本二进制和一段 zip 粘在一起：归档里的\n"
        "modules/、assets/、init.lua、registry.tsv 会自动生效，所以 app.gg 在任何机器上\n"
        "都能直接跑你的脚本 —— 同一个文件，不需要目标机器安装 gg。\n\n"
        "  gg bundle app[.gg] [文件或目录 ...] [rm:模式]...   [-f]\n"
        "  gg bundle app.gg rm:assets                             删掉条目\n"
        "\n"
        "模块里可以用: gg.assets.read(name) / gg.assets.list() /\n"
        "gg.assets.have(name) / gg.assets.dir()（解包到临时目录）。\n"
        "在 APE 上也可以用原生写法 io.open('/zip/assets/x')。\n"));
  } else if (gg_streq(topic, "commands") || gg_streq(topic, "keys")) {
    printf("%s%s%s\n", c_bold(), gg_tr("dashboard keys", "面板快捷键"), c_reset());
    printf("  ↑↓ / j k    %s\n", gg_tr("move", "移动"));
    printf("  enter       %s\n", gg_tr("run the selection", "运行选中项"));
    printf("  e / n / a   %s\n", gg_tr("edit / new module / add command",
                                      "编辑 / 新模块 / 添加命令"));
    printf("  d / l       %s\n", gg_tr("delete / create shim", "删除 / 创建快捷方式"));
    printf("  s / r / ?   %s\n", gg_tr("setup PATH / reload / help",
                                      "配置 PATH / 刷新 / 帮助"));
    printf("  q           %s\n", gg_tr("quit", "退出"));
    printf("  /           %s\n", gg_tr("type to filter at any time", "随时输入即过滤"));
  } else {
    help_top();
  }
}

/* ------------------------------------------------------------------ */
/* dashboard                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
  int kind; /* 0 quick action, 1 module, 2 command, 3 history */
  char *name;
  char *title;
  char *desc;
  char *cmd;
  char *path;
} dash_item;

static void dash_item_free(dash_item *it) {
  free(it->name);
  free(it->title);
  free(it->desc);
  free(it->cmd);
  free(it->path);
}

typedef struct {
  dash_item *v;
  int n, cap;
} dash_list;

static dash_item *dash_push(dash_list *l) {
  if (l->n + 1 > l->cap) {
    l->cap = l->cap ? l->cap * 2 : 32;
    l->v = realloc(l->v, (size_t)l->cap * sizeof(dash_item));
  }
  dash_item *it = &l->v[l->n++];
  memset(it, 0, sizeof(*it));
  return it;
}

static void dash_build(dash_list *l) {
  l->n = 0;
  /* quick actions */
  {
    dash_item *it = dash_push(l);
    it->kind = 0;
    it->name = gg_strdup("__setup");
    it->title = gg_strdup(gg_tr("enable gg on PATH", "把 gg 加入 PATH"));
    it->desc = gg_strdup(gg_tr("runs `gg active` for the detected shell",
                     "为当前 shell 运行 `gg active`"));
  }
  {
    dash_item *it = dash_push(l);
    it->kind = 0;
    it->name = gg_strdup("__doctor");
    it->title = gg_strdup(gg_tr("diagnose this machine", "检查本机环境"));
    it->desc = gg_strdup(gg_tr("tool availability, paths, shims",
                     "工具可用性、路径、shim"));
  }
  {
    dash_item *it = dash_push(l);
    it->kind = 0;
    it->name = gg_strdup("__new");
    it->title = gg_strdup(gg_tr("write a new module", "写一个新模块"));
    it->desc = gg_strdup(gg_tr("scaffold ~/.gg/modules/<name>.lua and edit it",
                     "在 ~/.gg/modules 下新建并编辑 Lua 模块"));
  }
  {
    dash_item *it = dash_push(l);
    it->kind = 0;
    it->name = gg_strdup("__add");
    it->title = gg_strdup(gg_tr("register a command line",
                                "注册一条命令行"));
    it->desc = gg_strdup(gg_tr("perfect for long aria2c/curl invocations",
                     "适合 aria2c / curl 这类很长的命令"));
  }
  for (int i = 0; i < modules_count(); i++) {
    module_info *m = modules_at(i);
    dash_item *it = dash_push(l);
    it->kind = 1;
    it->name = gg_strdup(m->name);
    it->title = gg_strdup(m->title ? m->title : m->name);
    it->desc = gg_strdup(m->desc ? m->desc : "");
    it->path = (m->path && !m->bundled) ? gg_strdup(m->path) : 0;
  }
  for (int i = 0; i < reg_count(); i++) {
    reg_entry *e = reg_at(i);
    dash_item *it = dash_push(l);
    it->kind = 2;
    it->name = gg_strdup(e->name);
    it->title = gg_strdup(*e->desc ? e->desc : e->cmd);
    it->desc = gg_strdup(e->cmd);
    it->cmd = gg_strdup(e->cmd);
  }
  char *recent[12];
  int nr = reg_log_recent(12, recent);
  for (int i = 0; i < nr; i++) {
    char *line = recent[i];
    char *t1 = strchr(line, '\t');
    if (!t1) {
      free(line);
      continue;
    }
    *t1 = 0;
    char *t2 = strchr(t1 + 1, '\t');
    if (t2) *t2 = 0;
    dash_item *it = dash_push(l);
    it->kind = 3;
    it->name = gg_strdup(line + 11); /* skip the timestamp */
    it->title = gg_strdup(t1 + 1);
    it->desc = gg_strdup(t2 ? t2 + 1 : "");
    free(line);
  }
}

static int dash_match(dash_item *it, const char *filter) {
  if (!filter || !*filter) return 1;
  const char *parts[3] = {it->name, it->title, it->desc};
  for (int i = 0; i < 3; i++) {
    if (!parts[i]) continue;
    for (const char *p = parts[i]; *p; p++) {
      size_t k = 0, fl = strlen(filter);
      while (k < fl && p[k] &&
             tolower((unsigned char)p[k]) == tolower((unsigned char)filter[k]))
        k++;
      if (k == fl) return 1;
    }
  }
  return 0;
}

static const char *kind_label(int kind) {
  switch (kind) {
    case 0: return gg_tr("quick action", "快捷操作");
    case 1: return gg_tr("module", "模块");
    case 2: return gg_tr("command", "命令");
    default: return gg_tr("history", "历史");
  }
}

static void dash_detail(sbuf *b, int row, int col, int width, dash_item *it) {
  int r = row;
  tui_at(b, r, col);
  sb_adds(b, tui_style(ST_BOLD));
  sb_adds(b, it->title ? it->title : it->name);
  sb_adds(b, ST_RESET);
  r += 1;
  tui_at(b, r++, col);
  sb_adds(b, tui_style(ST_MUTED));
  sb_adds(b, kind_label(it->kind));
  if (it->kind != 0) {
    sb_adds(b, "  ");
    sb_adds(b, it->name);
  }
  sb_adds(b, ST_RESET);
  r++;
  if (it->desc && *it->desc) {
    /* wrap the description */
    const char *p = it->desc;
    while (*p && r < T.h - 3) {
      int take = width < 1 ? 1 : width;
      size_t len = strlen(p);
      char *chunk = gg_strndup(p, len);
      while (strlen(chunk) > 1 && gg_width(chunk) > take) {
        size_t cut = gg_utf8_prev(chunk, strlen(chunk));
        chunk[cut] = 0;
      }
      size_t used = strlen(chunk);
      tui_at(b, r++, col);
      sb_adds(b, chunk);
      sb_adds(b, ST_RESET);
      free(chunk);
      p += used;
      if (*p == '\n') p++;
    }
    r++;
  }
  if (it->kind == 1) {
    /* a module: say where it comes from and preview its source */
    module_info *m = modules_find(it->name);
    size_t len = 0;
    int src_kind = 0;
    char *src = module_source_text(it->name, &len, &src_kind);
    tui_at(b, r++, col);
    sb_adds(b, tui_style(ST_MUTED));
    if (m && m->builtin)
      sb_adds(b, gg_tr("built into gg", "内置在 gg 中"));
    else if (m && m->bundled) {
      char info[PATH_MAX + 32];
      snprintf(info, sizeof(info), "%s (%s)", m->path,
               gg_tr("bundled", "打包在文件里"));
      sb_adds(b, info);
    }
    else if (m && m->path)
      sb_adds(b, m->path);
    sb_adds(b, ST_RESET);
    if (src) {
      r++;
      int lines = 0;
      char *p = src;
      while (*p && r < T.h - 3 && lines < 14) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        tui_at(b, r++, col);
        sb_adds(b, tui_style(ST_MUTED));
        sb_addf(b, "%s", p);
        sb_adds(b, ST_RESET);
        lines++;
        if (!nl) break;
        p = nl + 1;
      }
      free(src);
    }
  } else if (it->path) {
    tui_at(b, r++, col);
    sb_adds(b, tui_style(ST_MUTED));
    sb_adds(b, it->path);
    sb_adds(b, ST_RESET);
  }
  (void)r;
}

static void dash_run_item(dash_item *it);

int cmd_dashboard(void) {
  if (!T.interactive) {
    printf("%sgg %s%s — %s\n\n", c_bold(), GG_VERSION, c_reset(),
           gg_tr("cosmopolitan multitool", "cosmopolitan 多用工具"));
    print_kv(gg_tr("gg dir", "gg 目录"), "%s", gg_dir());
    print_kv(gg_tr("modules", "模块"), "%d", modules_count());
    print_kv(gg_tr("commands", "命令"), "%d", reg_count());
    printf("\n");
    cmd_ls(0, 0);
    printf("%s\n", gg_tr("(not a terminal: use `gg <name>` to run something)",
                         "（当前不是终端：用 `gg <名字>` 直接运行）"));
    return 0;
  }

  dash_list list;
  memset(&list, 0, sizeof(list));
  dash_build(&list);
  int sel = 0, scroll = 0;
  char filter[128] = "";
  const char *status = 0;

  lua_open_runtime();
  tui_enter();
  for (;;) {
    tui_size();
    sbuf *b = &G_FRAME;
    tui_clear(b);
    /* header */
    char right[128];
    snprintf(right, sizeof(right), "gg %s · %s", GG_VERSION,
             gg_lang_zh() ? "中文" : "en");
    tui_at(b, 1, 1);
    sb_adds(b, tui_style(ST_TITLE));
    sb_adds(b, " gg ");
    sb_adds(b, ST_RESET);
    sb_adds(b, tui_style(ST_MUTED));
    sb_adds(b, gg_tr("one binary · lua scripting · everywhere",
                     "一个二进制 · Lua 脚本 · 全平台"));
    sb_adds(b, ST_RESET);
    {
      int w = gg_width(right);
      tui_at(b, 1, T.w - w - 1);
      sb_adds(b, tui_style(ST_MUTED));
      sb_adds(b, right);
      sb_adds(b, ST_RESET);
    }
    tui_hline(b, 2, 1, T.w, tui_style(ST_DIM));

    int listw = T.w / 3;
    if (listw < 26) listw = 26;
    if (listw > 46) listw = 46;
    int detailx = listw + 3;
    int detailw = T.w - detailx - 1;
    int rows = T.h - 4;

    /* filtered navigation */
    int vis = 0;
    for (int i = 0; i < list.n; i++)
      if (dash_match(&list.v[i], filter)) vis++;
    if (sel >= list.n) sel = list.n - 1;
    if (sel < 0) sel = 0;
    /* index of sel within the filtered view */
    int nth = 0, seen = 0;
    for (int i = 0; i < list.n; i++) {
      if (dash_match(&list.v[i], filter)) {
        if (i == sel) {
          nth = seen;
          break;
        }
        seen++;
      }
    }
    if (nth < scroll) scroll = nth;
    if (nth >= scroll + rows) scroll = nth - rows + 1;
    if (scroll < 0) scroll = 0;

    int drawn = 0;
    for (int i = 0; i < list.n && drawn < rows; i++) {
      if (!dash_match(&list.v[i], filter)) continue;
      int nthi = 0;
      for (int k = 0, c = 0; k < i; k++) {
        if (dash_match(&list.v[k], filter)) c++;
        nthi = c;
      }
      if (nthi < scroll) continue;
      if (nthi >= scroll + rows) break;
      dash_item *it = &list.v[i];
      int row = 3 + (nthi - scroll);
      char line[512];
      const char *label = it->kind == 0 ? it->title : it->name;
      const char *icon = it->kind == 1 ? "\u25c6" : it->kind == 2 ? "$"
                         : it->kind == 3 ? "\u21ba" : "\u2699";
      if (!T.utf8) icon = it->kind == 1 ? "*" : it->kind == 2 ? "$" : ">";
      snprintf(line, sizeof(line), " %s %s", icon, label);
      tui_at(b, row, 1);
      sb_adds(b, "\033[K");
      if (i == sel) {
        sb_adds(b, tui_style(ST_SEL));
        sb_adds(b, line);
        sb_adds(b, ST_RESET);
      } else {
        sb_adds(b, tui_style(it->kind == 0 ? ST_ACCENT : ST_BOLD));
        sb_adds(b, line);
        sb_adds(b, ST_RESET);
      }
      /* right hand title */
      if (it->kind != 0 && it->title) {
        int w = gg_width(line);
        int space = listw - w - 2;
        if (space > 4) {
          char *t = gg_strdup(it->title);
          while (strlen(t) > 1 && gg_width(t) > space) {
            size_t cut = gg_utf8_prev(t, strlen(t));
            t[cut] = 0;
          }
          tui_at(b, row, w + 2);
          sb_adds(b, tui_style(ST_MUTED));
          sb_adds(b, t);
          sb_adds(b, ST_RESET);
          free(t);
        }
      }
      drawn++;
    }
    /* vertical separator */
    for (int r = 3; r < T.h - 1; r++) {
      tui_at(b, r, listw + 1);
      sb_adds(b, tui_style(ST_DIM));
      sb_adds(b, T.utf8 ? "\u2502" : "|");
      sb_adds(b, ST_RESET);
    }
    if (list.n) dash_detail(b, 3, detailx, detailw, &list.v[sel]);

    /* filter + footer */
    tui_hline(b, T.h - 2, 1, T.w, tui_style(ST_DIM));
    tui_at(b, T.h - 1, 1);
    sb_adds(b, "\033[K");
    sb_adds(b, " ");
    sb_adds(b, tui_style(ST_MUTED));
    sb_adds(b, gg_tr("filter:", "过滤:"));
    sb_adds(b, ST_RESET);
    sb_adds(b, " ");
    sb_adds(b, tui_style(ST_ACCENT));
    sb_adds(b, filter);
    sb_adds(b, ST_RESET);
    if (status) {
      tui_at(b, T.h - 1, T.w - (int)gg_width(status) - 2);
      sb_adds(b, tui_style(ST_WARN));
      sb_adds(b, status);
      sb_adds(b, ST_RESET);
    }
    tui_at(b, T.h, 1);
    sb_adds(b, "\033[K");
    sb_adds(b, " ");
    sb_adds(b, tui_style(ST_MUTED));
    sb_adds(b, gg_tr("↑↓ move  ⏎ run  e edit  n new  a add  d delete  l link  "
                     "s setup  r reload  ? help  q quit",
                     "↑↓ 移动  ⏎ 运行  e 编辑  n 新建  a 添加  d 删除  l 快捷方式  "
                     "s 配置  r 刷新  ? 帮助  q 退出"));
    sb_adds(b, ST_RESET);
    tui_at(b, T.h - 1, 9 + (int)gg_width(filter));
    sb_adds(b, GG_CUR_SHOW);
    tui_flush(b);

    int k = tui_key(-1);
    status = 0;
    if (k == KEY_NONE) continue;
    if (k == KEY_UP || k == 'k') {
      do {
        if (sel > 0) sel--;
      } while (sel > 0 && !dash_match(&list.v[sel], filter));
      scroll = scroll > 0 && nth <= scroll ? scroll - 1 : scroll;
    } else if (k == KEY_DOWN || k == 'j') {
      do {
        if (sel + 1 < list.n) sel++;
      } while (sel + 1 < list.n && !dash_match(&list.v[sel], filter));
    } else if (k == KEY_PGUP) {
      sel -= rows;
      if (sel < 0) sel = 0;
    } else if (k == KEY_PGDN) {
      sel += rows;
      if (sel >= list.n) sel = list.n - 1;
    } else if (k == KEY_BACKSPACE) {
      if (filter[0]) filter[gg_utf8_prev(filter, strlen(filter))] = 0;
    } else if (k == KEY_ESC) {
      if (filter[0]) filter[0] = 0;
      else break;
    } else if (k == KEY_ENTER) {
      if (list.n) dash_run_item(&list.v[sel]);
      dash_build(&list);
    } else if (k == 'e') {
      if (sel >= 0 && sel < list.n) {
        dash_item *it = &list.v[sel];
        strvec args;
        sv_init(&args);
        sv_push(&args, it->name);
        if (cmd_edit(1, sv_argv(&args)) != 0) status = "no module";
        sv_free(&args);
        dash_build(&list);
      }
    } else if (k == 'n') {
      if (cmd_init(0, 0) == 0) dash_build(&list);
    } else if (k == 'a') {
      if (cmd_add(0, 0) == 0) dash_build(&list);
    } else if (k == 'd') {
      if (sel >= 0 && sel < list.n) {
        dash_item *it = &list.v[sel];
        strvec args;
        sv_init(&args);
        sv_push(&args, it->name);
        cmd_rm(1, sv_argv(&args));
        sv_free(&args);
        dash_build(&list);
        if (sel >= list.n) sel = list.n ? list.n - 1 : 0;
      }
    } else if (k == 'l') {
      if (sel >= 0 && sel < list.n) {
        strvec args;
        sv_init(&args);
        sv_push(&args, list.v[sel].name);
        cmd_link(1, sv_argv(&args));
        sv_free(&args);
        status = gg_tr("shim created", "已创建快捷方式");
      }
    } else if (k == 's') {
      tui_leave();
      cmd_active(0, 0);
      gg_pause_key(0);
      tui_enter();
    } else if (k == 'r') {
      reg_reload();
      modules_scan();
      dash_build(&list);
      status = gg_tr("reloaded", "已刷新");
    } else if (k == '?') {
      tui_leave();
      printf("\n");
      help_topic("commands");
      gg_pause_key(0);
      tui_enter();
    } else if (k == 'q') {
      if (filter[0]) filter[0] = 0;
      else break;
    } else if (k == KEY_TAB) {
      /* cycle between modules and commands by filtering */
    } else if (k >= 32 && k < KEY_UP) {
      size_t n = strlen(filter);
      char tmp[4];
      int nb = gg_utf8_encode((uint32_t)k, tmp);
      if (n + (size_t)nb < sizeof(filter) - 1) {
        memcpy(filter + n, tmp, (size_t)nb);
        filter[n + (size_t)nb] = 0;
      }
      /* jump to the first match */
      for (int i = 0; i < list.n; i++) {
        if (dash_match(&list.v[i], filter)) {
          sel = i;
          break;
        }
      }
    }
  }
  tui_leave();
  for (int i = 0; i < list.n; i++) dash_item_free(&list.v[i]);
  free(list.v);
  return 0;
}

static void dash_run_item(dash_item *it) {
  if (it->kind == 0) {
    if (gg_streq(it->name, "__setup")) {
      tui_leave();
      cmd_active(0, 0);
      gg_pause_key(0);
      tui_enter();
    } else if (gg_streq(it->name, "__doctor")) {
      tui_leave();
      cmd_doctor(0, 0);
      gg_pause_key(0);
      tui_enter();
    } else if (gg_streq(it->name, "__new")) {
      tui_leave();
      tui_enter(); /* keep widgets in alt screen */
      cmd_init(0, 0);
    } else if (gg_streq(it->name, "__add")) {
      cmd_add(0, 0);
    }
    return;
  }
  if (it->kind == 1) {
    lua_run_module(it->name, 0, 0, 1);
    return;
  }
  if (it->kind == 2 || it->kind == 3) {
    const char *cmd = it->cmd ? it->cmd : it->desc;
    if (!cmd || !*cmd) return;
    reg_log(it->name, cmd);
    run_shell_paused(cmd);
  }
}

/* ------------------------------------------------------------------ */
/* REPL                                                                */
/* ------------------------------------------------------------------ */

static int cmd_repl(void) {
  if (!T.interactive) {
    gg_error("%s", gg_tr("the repl needs a terminal", "REPL 需要终端"));
    return 1;
  }
  lua_open_runtime();
  tui_leave();
  printf("%sgg %s repl%s  — %s\n", c_bold(), GG_VERSION, c_reset(),
         gg_tr("ctrl-d or exit to leave", "ctrl-d 或 exit 退出"));
  char line[1024];
  for (;;) {
    printf("%s> %s", c_accent(), c_reset());
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin)) break;
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (!*line) continue;
    if (gg_streq(line, "exit") || gg_streq(line, "quit")) break;
    char *trimmed = gg_trim(line);
    char *out = 0;
    if (lua_eval_expr_string(trimmed, &out) == 0) {
      if (out && *out && !gg_streq(out, "nil")) printf("%s\n", out);
    } else {
      char *stmt = gg_asprintf("%s\n", trimmed);
      if (lua_run_string(stmt, "=repl") != 0) {
        /* error already printed */
      }
      free(stmt);
    }
    free(out);
  }
  printf("\n");
  return 0;
}

/* ------------------------------------------------------------------ */
/* dispatch                                                            */
/* ------------------------------------------------------------------ */

static int run_registered(const char *name, int argc, char **argv) {
  reg_entry *e = reg_find(name);
  if (!e) return -1;
  sbuf b;
  sb_init(&b, 256);
  sb_adds(&b, e->cmd);
  for (int i = 0; i < argc; i++) {
    sb_addc(&b, ' ');
    char *q = gg_shell_quote(argv[i]);
    sb_adds(&b, q);
    free(q);
  }
  reg_log(name, b.p);
  int rc;
  if (T.in_alt) {
    rc = run_shell_paused(b.p);
  } else {
    gg_echo_line(b.p);
    rc = proc_shell_run(b.p);
  }
  sb_free(&b);
  return rc;
}

int main(int argc, char **argv) {
  gg_paths_init();
  tui_init();
  reg_load();

  if (argc < 2) return cmd_dashboard();

  const char *cmd = argv[1];
  int restc = argc - 2;
  char **rest = argv + 2;

  /* ---- global flags (only before the sub-command) ---- */
  if (arg_is_flag(cmd, "-h", "--help")) {
    if (argc > 2) help_topic(argv[2]);
    else help_top();
    return 0;
  }
  if (arg_is_flag(cmd, "-v", "--version")) return cmd_version();

  if (gg_streq(cmd, "-e") || gg_streq(cmd, "--eval")) {
    if (restc < 1) {
      gg_error("%s", gg_tr("usage: gg -e 'lua code'", "用法: gg -e 'lua 代码'"));
      return 1;
    }
    return lua_run_string(rest[0], "=cli");
  }

  if (gg_streq(cmd, "repl")) return cmd_repl();
  if (gg_streq(cmd, "help")) {
    help_topic(restc ? rest[0] : 0);
    return 0;
  }
  if (gg_streq(cmd, "version")) return cmd_version();
  if (gg_streq(cmd, "active")) return cmd_active(restc, rest);
  if (gg_streq(cmd, "deactivate") || gg_streq(cmd, "disable")) {
    char *a[2] = {(char *)"off", 0};
    return cmd_active(1, a);
  }
  if (gg_streq(cmd, "ls") || gg_streq(cmd, "list")) return cmd_ls(restc, rest);
  if (gg_streq(cmd, "show") || gg_streq(cmd, "info")) return cmd_show(restc, rest);
  if (gg_streq(cmd, "add")) return cmd_add(restc, rest);
  if (gg_streq(cmd, "rm") || gg_streq(cmd, "remove")) return cmd_rm(restc, rest);
  if (gg_streq(cmd, "edit")) return cmd_edit(restc, rest);
  if (gg_streq(cmd, "init") || gg_streq(cmd, "new")) return cmd_init(restc, rest);
  if (gg_streq(cmd, "run")) return cmd_run(restc, rest);
  if (gg_streq(cmd, "exec")) return cmd_exec(restc, rest);
  if (gg_streq(cmd, "tui")) return cmd_tui(restc, rest);
  if (gg_streq(cmd, "doctor")) return cmd_doctor(restc, rest);
  if (gg_streq(cmd, "modules") || gg_streq(cmd, "module"))
    return cmd_modules(restc, rest);
  if (gg_streq(cmd, "config")) return cmd_config(restc, rest);
  if (gg_streq(cmd, "bundle") || gg_streq(cmd, "pack"))
    return cmd_bundle(restc, rest);
  if (gg_streq(cmd, "upgrade") || gg_streq(cmd, "self-update"))
    return cmd_upgrade(restc, rest);
  if (gg_streq(cmd, "link") || gg_streq(cmd, "alias")) return cmd_link(restc, rest);
  if (gg_streq(cmd, "unlink")) return cmd_unlink(restc, rest);
  if (gg_streq(cmd, "dashboard")) return cmd_dashboard();

  /* ---- lua scripts by path ---- */
  if (strstr(cmd, "/") || gg_endswith(cmd, ".lua")) {
    return lua_run_file(cmd, restc, rest);
  }

  /* ---- modules ---- */
  if (modules_find(cmd)) return lua_run_module(cmd, restc, rest, 0);

  /* ---- registered commands ---- */
  {
    int rc = run_registered(cmd, restc, rest);
    if (rc >= 0) return rc;
  }

  /* ---- system PATH ---- */
  {
    strvec args;
    sv_init(&args);
    sv_push(&args, cmd);
    for (int i = 0; i < restc; i++) sv_push(&args, rest[i]);
    const char *found = gg_which(cmd);
    if (found) {
      if (T.in_alt) {
        int rc = run_shell_paused(gg_join_argv(sv_argv(&args)));
        sv_free(&args);
        return rc;
      }
      return proc_exec(sv_argv(&args), 0); /* replaces this process */
    }
    sv_free(&args);
  }

  gg_error("gg %s: %s", cmd, gg_tr("unknown module, command or program",
                                   "未知模块、命令或程序"));
  printf("%s%s%s\n", c_dim(),
         gg_tr("try `gg ls`, or `gg help`, or `gg <program>` for something on PATH",
               "试试 `gg ls`、`gg help`，或直接 `gg <PATH 中的程序>`"),
         c_reset());
  return 127;
}
