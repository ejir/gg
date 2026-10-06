/* gg — built-in subcommands: active, ls, add, rm, show, edit, run, init,
 * doctor, help, version, modules, link, config, upgrade, exec, tui. */
#include "gg.h"

static int module_remove_tree(const char *path);

/* ------------------------------------------------------------------ */
/* small shared helpers                                                */
/* ------------------------------------------------------------------ */

void gg_pause_key(const char *msg) {
  if (!T.tty_out) return;
  printf("\n%s%s%s ", c_dim(), msg ? msg : gg_tr("press enter to continue",
                                                 "按回车继续"),
         c_reset());
  fflush(stdout);
  int c;
  while ((c = getchar()) != '\n' && c != EOF) {
  }
}

int run_shell_paused(const char *cmd) {
  int was_alt = T.interactive && T.in_alt;
  if (was_alt) tui_suspend();
  gg_echo_line(cmd);
  int rc = proc_shell_run(cmd);
  if (was_alt) {
    gg_pause_key(gg_tr("-- done, press enter to go back",
                       "-- 完成，按回车返回"));
    tui_resume();
  }
  return rc;
}

/* ------------------------------------------------------------------ */
/* gg active — put ~/.gg/bin on PATH                                    */
/* ------------------------------------------------------------------ */

#define BLOCK_BEGIN "# >>> gg >>>"
#define BLOCK_END "# <<< gg <<<"

static const char *detect_shell(void) {
  const char *forced = getenv("GG_SHELL");
  if (forced && *forced) return forced;
#if GG_WINDOWS
  if (getenv("PSModulePath")) return "powershell";
  return "cmd";
#else
  const char *sh = getenv("SHELL");
  if (sh && *sh) {
    const char *b = gg_basename(sh);
    if (strstr(b, "zsh")) return "zsh";
    if (strstr(b, "fish")) return "fish";
    if (strstr(b, "bash")) return "bash";
    if (strstr(b, "ksh")) return "ksh";
    if (strstr(b, "dash")) return "sh";
    if (strstr(b, "sh")) return "sh";
  }
  if (getenv("BASH_VERSION")) return "bash";
  if (getenv("ZSH_VERSION")) return "zsh";
  if (getenv("FISH_VERSION")) return "fish";
  return "bash";
#endif
}

static void active_files(const char *shell, strvec *out) {
  const char *home = gg_home();
  if (gg_streq(shell, "fish")) {
    sv_pushf(out, "%s/.config/fish/config.fish", home);
    return;
  }
  if (gg_streq(shell, "powershell")) {
    const char *profile = getenv("PROFILE");
    if (profile && *profile) {
      sv_push(out, profile);
      return;
    }
    sv_pushf(out, "%s/Documents/PowerShell/Microsoft.PowerShell_profile.ps1",
             home);
    sv_pushf(out, "%s/Documents/WindowsPowerShell/Microsoft.PowerShell_profile.ps1",
             home);
    return;
  }
  if (gg_streq(shell, "zsh")) {
    sv_pushf(out, "%s/.zshrc", home);
    return;
  }
  if (gg_streq(shell, "cmd")) return;
  /* bash & friends */
  char *bashrc = gg_asprintf("%s/.bashrc", home);
  if (gg_exists(bashrc)) {
    sv_push(out, bashrc);
  } else {
    char *profile = gg_asprintf("%s/.bash_profile", home);
    if (gg_exists(profile)) sv_push(out, profile);
    else sv_push(out, bashrc);
    free(profile);
  }
  free(bashrc);
}

/* The snippet normally says $HOME/.gg/bin so it survives copying the rc file
 * to another machine; with a custom GG_DIR we have to spell the real path. */
static int bin_is_default(void) {
  char expect[PATH_MAX];
  snprintf(expect, sizeof(expect), "%s/%s/bin", gg_home(), GG_DIR_NAME);
  return gg_streq(gg_bin_dir(), expect);
}

static char *active_block(const char *shell) {
  sbuf b;
  sb_init(&b, 256);
  const char *bin = gg_bin_dir();
  int dflt = bin_is_default();
  int spaced = strchr(bin, ' ') != 0;
  char qbin[PATH_MAX + 8]; /* the real path, shell-quoted when needed */
  snprintf(qbin, sizeof(qbin), "%s%s%s", spaced ? "'" : "", bin,
           spaced ? "'" : "");
  const char *p = dflt ? "$HOME/.gg/bin" : qbin;
  sb_addf(&b, "%s\n", BLOCK_BEGIN);
  sb_adds(&b, "# added by `gg active` — remove with `gg active off`\n");
  if (gg_streq(shell, "fish")) {
    sb_addf(&b, "if not contains %s $PATH\n", p);
    sb_addf(&b, "    set -gx PATH %s $PATH\n", p);
    sb_adds(&b, "end\n");
    sb_adds(&b, "if type -q gg\n    gg completion fish | source\nend\n");
  } else if (gg_streq(shell, "powershell")) {
    if (dflt)
      sb_adds(&b, "$ggbin = Join-Path $env:USERPROFILE '.gg\\bin'\n");
    else
      sb_addf(&b, "$ggbin = '%s'\n", bin);
    sb_adds(&b,
            "if (Test-Path $ggbin) { if ($env:PATH -notlike \"*$ggbin*\") "
            "{ $env:PATH = \"$ggbin;$env:PATH\" } }\n"
            "if (Get-Command gg -ErrorAction SilentlyContinue) { "
            "Invoke-Expression (gg completion powershell | Out-String) }\n");
  } else {
    sb_addf(&b,
            "case \":$PATH:\" in\n"
            "  *\":%s:\"*) ;;\n"
            "  *) PATH=\"%s:$PATH\" ;;\n"
            "esac\n"
            "export PATH\n",
            p, p);
    if (gg_streq(shell, "zsh")) {
      sb_adds(&b,
              "if command -v gg >/dev/null 2>&1; then\n"
              "  eval \"$(gg completion zsh)\"\n"
              "fi\n");
    } else if (gg_streq(shell, "bash")) {
      sb_adds(&b,
              "if command -v gg >/dev/null 2>&1; then\n"
              "  eval \"$(gg completion bash)\"\n"
              "fi\n");
    }
  }
  sb_addf(&b, "%s\n", BLOCK_END);
  return b.p;
}

/* strip our block from a file; returns 1 when something changed */
static int strip_block(const char *path) {
  size_t len = 0;
  char *data = gg_read_file(path, &len);
  if (!data) return 0;
  sbuf out;
  sb_init(&out, len + 64);
  char *p = data;
  int in_block = 0, changed = 0;
  while (p && *p) {
    char *nl = strchr(p, '\n');
    size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
    if (linelen && p[linelen - 1] == '\r') linelen--;
    if (!in_block && linelen == strlen(BLOCK_BEGIN) &&
        !memcmp(p, BLOCK_BEGIN, linelen)) {
      in_block = 1;
      changed = 1;
    } else if (in_block) {
      if (linelen == strlen(BLOCK_END) && !memcmp(p, BLOCK_END, linelen))
        in_block = 0;
    } else {
      sb_addn(&out, p, linelen);
      sb_addc(&out, '\n');
    }
    p = nl ? nl + 1 : 0;
  }
  free(data);
  if (changed) gg_write_file(path, out.p, out.n);
  sb_free(&out);
  return changed;
}

/* make sure ~/.gg/bin/gg points at this binary */
static char *ensure_shim(void) {
  char *dir = gg_join(gg_dir(), "bin");
  if (gg_mkdir_p(dir) != 0) {
    free(dir);
    return 0;
  }
  const char *exe = gg_exe_path();
  char *target = gg_join(dir, GG_WINDOWS ? "gg.exe" : "gg");
#if GG_WINDOWS
  gg_copy_file(exe, target);
  char *com = gg_join(dir, "gg.com");
  gg_copy_file(exe, com);
  free(com);
#else
  unlink(target);
  if (symlink(exe, target) != 0) {
    if (gg_copy_file(exe, target) != 0) {
      free(target);
      free(dir);
      return 0;
    }
  }
  gg_chmod_x(target);
#endif
  free(dir);
  return target;
}

static void active_status(void) {
  const char *shell = detect_shell();
  print_kv(gg_tr("gg binary", "gg 可执行文件"), "%s", gg_exe_path());
  print_kv(gg_tr("gg dir", "数据目录"), "%s", gg_dir());
  print_kv(gg_tr("shims", "命令目录"), "%s", gg_bin_dir());
  print_kv(gg_tr("shell", "shell"), "%s", shell);
  print_kv(gg_tr("modules", "模块"), "%s", gg_modules_dir());
  strvec files;
  sv_init(&files);
  active_files(shell, &files);
  for (int i = 0; i < files.n; i++) {
    size_t len = 0;
    char *data = gg_read_file(files.v[i], &len);
    int has = data && strstr(data, BLOCK_BEGIN) != 0;
    free(data);
    print_kv(gg_tr("path entry", "PATH 配置"),
             "%s %s", files.v[i],
             has ? gg_tr("· enabled", "· 已启用") : gg_tr("· not set", "· 未配置"));
  }
  sv_free(&files);
  char *shim = gg_asprintf("%s/gg", gg_bin_dir());
  print_kv(gg_tr("shim", "shim"),
           "%s %s", shim,
           gg_exists(shim) ? gg_tr("· present", "· 存在")
                           : gg_tr("· missing (run `gg active`)",
                                   "· 缺失（运行 `gg active`）"));
  free(shim);
  const char *path = getenv("PATH");
  int on_path = 0;
  if (path) {
    char *needle = gg_asprintf("%s%s%s", ":", gg_bin_dir(), ":");
    char *hay = gg_asprintf(":%s:", path);
    on_path = strstr(hay, needle) != 0;
    free(needle);
    free(hay);
  }
  print_kv(gg_tr("current PATH", "当前 PATH"),
           "%s", on_path ? gg_tr("· contains ~/.gg/bin",
                                 "· 已包含 ~/.gg/bin")
                         : gg_tr("· not in this session yet",
                                 "· 当前会话还没有"));
}

static int active_off(const char *shell) {
  strvec files;
  sv_init(&files);
  active_files(shell, &files);
  int n = 0;
  for (int i = 0; i < files.n; i++) {
    if (!gg_exists(files.v[i])) continue;
    if (strip_block(files.v[i])) {
      gg_ok("%s %s", gg_tr("removed gg block from", "已从以下文件移除 gg 配置:"),
            files.v[i]);
      n++;
    }
  }
  if (!n) gg_info("%s", gg_tr("nothing to remove", "没有需要移除的内容"));
  sv_free(&files);
  const char *path = getenv("PATH");
  (void)path;
  printf("%s%s%s\n", c_dim(),
         gg_tr("restart your shell (or re-source its rc file) to apply",
               "重启终端（或重新 source 配置文件）后生效"),
         c_reset());
  return 0;
}

static void active_usage(void) {
  printf("%s\n", gg_tr("usage: gg active [shell] [status|off|print]\n"
                        "  gg active           wire ~/.gg/bin into PATH (shell auto-detected)\n"
                        "  gg active status    show what is enabled\n"
                        "  gg active off       remove the block again\n"
                        "  gg active print     print the snippet instead of writing it\n"
                        "  gg active zsh       force a shell (bash/zsh/fish/powershell/cmd)",
                        "用法: gg active [shell] [status|off|print]\n"
                        "  gg active           把 ~/.gg/bin 写进 PATH（自动识别 shell）\n"
                        "  gg active status     查看当前状态\n"
                        "  gg active off        移除写入的配置\n"
                        "  gg active print      只打印配置片段，不写文件\n"
                        "  gg active zsh        指定 shell（bash/zsh/fish/powershell/cmd）"));
}

int cmd_active(int argc, char **argv) {
  const char *shell = 0;
  int print_only = 0, off = 0, status = 0;
  for (int i = 0; i < argc; i++) {
    const char *a = argv[i];
    if (a[0] == '-' && a[1]) {
      if (arg_is_flag(a, 0, "--print")) print_only = 1;
      else if (arg_is_flag(a, 0, "--off")) off = 1;
      else if (arg_is_flag(a, "-h", "--help")) { active_usage(); return 0; }
      else if (arg_is_flag(a, "-s", "--shell") && i + 1 < argc) shell = argv[++i];
      /* unknown flags are ignored, they are not shell names */
      continue;
    }
    if (gg_streq(a, "off") || gg_streq(a, "deactivate") || gg_streq(a, "disable"))
      off = 1;
    else if (gg_streq(a, "status")) status = 1;
    else if (gg_streq(a, "print")) print_only = 1;
    else if (gg_streq(a, "on") || gg_streq(a, "enable") || gg_streq(a, "activate"))
      ;                              /* the default action already */
    else if (!shell) shell = a;
  }
  if (!shell) shell = detect_shell();
  if (status) {
    active_status();
    return 0;
  }
  if (off) return active_off(shell);
  if (print_only) {
    char *block = active_block(shell);
    printf("%s", block);
    free(block);
    return 0;
  }
  if (gg_streq(shell, "cmd")) {
    /* cmd.exe: persist through setx, with a nudge about the 1024 limit */
    const char *existing = getenv("PATH");
    char *needle = gg_asprintf("%s\\bin", gg_dir());
    int already = existing && strstr(existing, needle) != 0;
    free(needle);
    if (!already) {
      const char *cmd =
          "powershell -NoProfile -Command \"$p=[Environment]::GetEnvironmentVariable"
          "('Path','User'); if($p -notlike '*\\.gg\\bin*'){"
          "[Environment]::SetEnvironmentVariable('Path', $p + ';' + "
          "$env:USERPROFILE + '\\.gg\\bin','User')}\"";
      printf("%s%s%s\n", c_dim(),
             gg_tr("setting the user PATH through powershell…",
                   "正在通过 powershell 设置用户 PATH…"),
             c_reset());
      if (proc_shell_run(cmd) != 0)
        gg_warn("%s", gg_tr("could not update PATH automatically; add "
                            "%USERPROFILE%\\.gg\\bin manually",
                            "无法自动更新 PATH，请手动加入 %USERPROFILE%\\.gg\\bin"));
    }
  } else {
    strvec files;
    sv_init(&files);
    active_files(shell, &files);
    char *block = active_block(shell);
    for (int i = 0; i < files.n; i++) {
      strip_block(files.v[i]);
      FILE *f = fopen(files.v[i], "a");
      if (!f) {
        char *dir = gg_dirname_of(files.v[i]);
        gg_mkdir_p(dir);
        free(dir);
        f = fopen(files.v[i], "a");
      }
      if (!f) {
        gg_warn("%s: %s", files.v[i], strerror(errno));
        continue;
      }
      /* leading blank line for readability */
      fprintf(f, "\n%s", block);
      fclose(f);
      gg_ok("%s %s", gg_tr("enabled gg in", "已为以下 shell 启用 gg:"), files.v[i]);
    }
    sv_free(&files);
    free(block);
  }
  char *shim = ensure_shim();
  if (shim) {
    gg_ok("%s %s", gg_tr("shim ready:", "shim 就绪:"), shim);
    free(shim);
  } else {
    gg_warn("%s", gg_tr("could not create ~/.gg/bin/gg", "无法创建 ~/.gg/bin/gg"));
  }
  printf("\n%s %s\n", tui_style(ST_ACCENT),
         gg_tr("open a new shell, or run:  source ~/.bashrc",
               "打开新终端，或执行:  source ~/.bashrc"));
  printf("%s%s%s\n", c_dim(),
         gg_tr("then `gg` is on your PATH everywhere, together with your shims",
               "之后 `gg` 与你定义的所有快捷命令都会在 PATH 中"),
         c_reset());
  return 0;
}

/* ------------------------------------------------------------------ */
/* shims                                                               */
/* ------------------------------------------------------------------ */

static char *shim_path_for(const char *name) {
  if (GG_WINDOWS) return gg_join(gg_bin_dir(), gg_asprintf("%s.cmd", name));
  return gg_join(gg_bin_dir(), name);
}

int cmd_link(int argc, char **argv) {
  if (argc < 1) {
    gg_error("%s", gg_tr("usage: gg link <name> [name...]",
                         "用法: gg link <名字> [名字...]"));
    return 1;
  }
  gg_mkdir_p(gg_bin_dir());
  for (int i = 0; i < argc; i++) {
    const char *name = argv[i];
    if (!modules_find(name) && !reg_find(name)) {
      gg_warn("%s %s", name, gg_tr("is neither a module nor a registered command",
                                   "既不是模块也不是已注册命令"));
      continue;
    }
    char *path = shim_path_for(name);
#if GG_WINDOWS
    char *script = gg_asprintf("@echo off\r\ngg %s %%*\r\n", name);
#else
    char *script = gg_asprintf("#!/bin/sh\n# gg shim for `%s`\nexec gg %s \"$@\"\n",
                               name, name);
#endif
    if (gg_write_file(path, script, strlen(script)) == 0) {
      gg_chmod_x(path);
      gg_ok("%s %s", gg_tr("linked", "已创建快捷方式:"), path);
    } else {
      gg_error("%s: %s", path, strerror(errno));
    }
    free(script);
    free(path);
  }
  return 0;
}

int cmd_unlink(int argc, char **argv) {
  if (argc < 1) {
    gg_error("%s", gg_tr("usage: gg unlink <name>", "用法: gg unlink <名字>"));
    return 1;
  }
  for (int i = 0; i < argc; i++) {
    char *path = shim_path_for(argv[i]);
    if (unlink(path) == 0)
      gg_ok("%s %s", gg_tr("removed", "已删除:"), path);
    else
      gg_warn("%s: %s", path, strerror(errno));
    free(path);
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* listing / registry                                                  */
/* ------------------------------------------------------------------ */

int cmd_ls(int argc, char **argv) {
  int json = 0;
  for (int i = 0; i < argc; i++)
    if (arg_is_flag(argv[i], 0, "--json")) json = 1;
  if (json) {
    lua_open_runtime();
    char *out = 0;
    lua_eval_expr_string(
        "gg.json.encode({ modules = gg.modules.list(), "
        "commands = gg.registry.list() })",
        &out);
    printf("%s\n", out ? out : "{}");
    free(out);
    return 0;
  }
  printf("%s%s%s\n", c_bold(),
         gg_tr("modules (lua scripts)", "模块（Lua 脚本）"), c_reset());
  for (int i = 0; i < modules_count(); i++) {
    module_info *m = modules_at(i);
    printf("  %s%-14s%s %s%s%s", c_accent(), m->name, c_reset(), m->title,
           (m->builtin || m->bundled) ? tui_style(ST_MUTED) : "",
           m->builtin   ? gg_tr("  (built-in)", "  (内置)")
           : m->bundled ? gg_tr("  (bundled)", "  (打包在文件里)")
                        : "");
    if (!m->builtin && !m->bundled) printf("%s", c_reset());
    printf("\n");
    if (m->desc && *m->desc) printf("      %s%s%s\n", c_dim(), m->desc, c_reset());
  }
  int n = reg_count();
  if (n) {
    printf("\n%s%s%s\n", c_bold(), gg_tr("commands (gg add)", "命令（gg add）"),
           c_reset());
    for (int i = 0; i < n; i++) {
      reg_entry *e = reg_at(i);
      printf("  %s%-14s%s %s%s%s\n", c_accent(), e->name, c_reset(), e->cmd,
             (e->source && gg_streq(e->source, "bundle")) ? tui_style(ST_MUTED) : "",
             (e->source && gg_streq(e->source, "bundle"))
                 ? gg_tr("  (bundled)", "  (打包在文件里)")
                 : "");
      if (e->desc && *e->desc)
        printf("      %s%s%s\n", c_dim(), e->desc, c_reset());
    }
  }
  printf("\n%s%s: %s%s\n", c_dim(), gg_tr("run `gg <name>` or just `gg`",
                                          "运行 `gg <名字>` 或直接运行 `gg`"),
         gg_dir(), c_reset());
  return 0;
}

int cmd_add(int argc, char **argv) {
  char name[128] = "", desc[256] = "";
  strvec parts;
  sv_init(&parts);
  for (int i = 0; i < argc; i++) {
    if ((arg_is_flag(argv[i], "-d", "--desc") ||
         arg_is_flag(argv[i], "-m", "--message")) &&
        i + 1 < argc) {
      snprintf(desc, sizeof(desc), "%s", argv[++i]);
    } else if (!name[0] && (arg_is_flag(argv[i], "-n", "--name")) &&
               i + 1 < argc) {
      snprintf(name, sizeof(name), "%s", argv[++i]);
    } else {
      sv_push(&parts, argv[i]);
    }
  }
  if (!name[0] && parts.n) {
    snprintf(name, sizeof(name), "%s", parts.v[0]);
    /* shift */
    for (int i = 1; i < parts.n; i++) parts.v[i - 1] = parts.v[i];
    parts.n--;
    parts.v[parts.n] = 0;
  }
  char cmd[1024] = "";
  if (parts.n) {
    size_t off = 0;
    for (int i = 0; i < parts.n && off < sizeof(cmd) - 2; i++) {
      if (i) cmd[off++] = ' ';
      size_t n = strlen(parts.v[i]);
      if (n > sizeof(cmd) - 2 - off) n = sizeof(cmd) - 2 - off;
      memcpy(cmd + off, parts.v[i], n);
      off += n;
    }
    cmd[off] = 0;
  }
  if ((!name[0] || !cmd[0]) && T.interactive) {
    if (!name[0] &&
        tui_prompt(gg_tr("new command", "新建命令"), gg_tr("name: ", "名字: "),
                   name, sizeof(name), 0) != 0)
      return 0;
    if (!cmd[0] &&
        tui_prompt(gg_tr("new command", "新建命令"),
                   gg_tr("shell command: ", "shell 命令: "), cmd, sizeof(cmd),
                   0) != 0)
      return 0;
    tui_prompt(gg_tr("new command", "新建命令"),
               gg_tr("description: ", "描述: "), desc, sizeof(desc), 0);
  }
  sv_free(&parts);
  if (!name[0] || !cmd[0]) {
    gg_error("%s", gg_tr("usage: gg add <name> <command> [--desc text]",
                         "用法: gg add <名字> <命令> [--desc 描述]"));
    return 1;
  }
  if (reg_add(name, cmd, desc) != 0) {
    gg_error("%s", gg_tr("could not write the registry", "无法写入注册表"));
    return 1;
  }
  gg_ok("%s gg %s", gg_tr("added — try", "已添加，试试"), name);
  return 0;
}

int cmd_rm(int argc, char **argv) {
  if (argc < 1) {
    gg_error("%s", gg_tr("usage: gg rm <name>", "用法: gg rm <名字>"));
    return 1;
  }
  int rc = 0;
  for (int i = 0; i < argc; i++) {
    module_info *m = modules_find(argv[i]);
    int done = 0;
    if (m && m->bundled) {
      gg_warn("%s %s", argv[i],
              gg_tr("is bundled into this file — rebuild the bundle without it",
                    "打包在这个文件里 — 重新打包时不要包含它"));
      rc = 1;
      continue;
    }
    if (m && !m->builtin && m->path) {
      if (tui_confirm(gg_tr("delete module", "删除模块"), argv[i], 0)) {
        char *parent = gg_dirname_of(m->path);
        int is_package = !gg_streq(parent, gg_modules_dir());
        int removed = is_package ? module_remove_tree(parent) : unlink(m->path);
        if (removed == 0) {
          gg_ok("%s %s", gg_tr("deleted", "已删除:"),
                is_package ? parent : m->path);
          done = 1;
        }
        free(parent);
      } else {
        done = 1;
      }
    }
    int rr = reg_remove(argv[i]);
    if (!done && rr == 0) {
      gg_ok("%s %s", gg_tr("removed command", "已删除命令:"), argv[i]);
      done = 1;
    } else if (!done && rr == -2) {
      gg_warn("%s %s", argv[i],
              gg_tr("is bundled into this file — rebuild the bundle without it",
                    "打包在这个文件里 — 重新打包时不要包含它"));
      done = 1; /* handled: not an unknown name */
    }
    if (!done) {
      gg_warn("%s %s", argv[i], gg_tr("not found (built-ins cannot be removed)",
                                      "未找到（内置模块不可删除）"));
      rc = 1;
    }
  }
  modules_rescan();
  return rc;
}

int cmd_show(int argc, char **argv) {
  if (argc < 1) {
    gg_error("%s", gg_tr("usage: gg show <name>", "用法: gg show <名字>"));
    return 1;
  }
  const char *name = argv[0];
  module_info *m = modules_find(name);
  reg_entry *e = reg_find(name);
  if (!m && !e) {
    gg_error("%s: %s", name, gg_tr("not found", "未找到"));
    return 1;
  }
  if (m) {
    print_kv(gg_tr("module", "模块"), "%s", m->name);
    print_kv(gg_tr("title", "标题"), "%s", m->title);
    if (m->desc && *m->desc) print_kv(gg_tr("about", "说明"), "%s", m->desc);
    if (m->builtin)
      print_kv(gg_tr("source", "来源"), "%s",
               gg_tr("built into gg", "内置在 gg 中"));
    else if (m->bundled)
      print_kv(gg_tr("source", "来源"), "%s (%s)", m->path,
               gg_tr("bundled", "打包在可执行文件里"));
    else if (m->path)
      print_kv(gg_tr("file", "文件"), "%s", m->path);
    char *p = module_path_of(name);
    size_t len = 0;
    char *src = p ? gg_read_file(p, &len) : 0;
    if (src) {
      printf("\n%s%s%s\n", c_dim(), gg_tr("--- source ---", "--- 源码 ---"),
             c_reset());
      int lines = 0;
      for (char *q = src; *q && lines < 60; q++) {
        if (*q == '\n') lines++;
      }
      if (lines >= 60) {
        /* only the head */
        char *end = src;
        for (int k = 0; k < 60 && *end; end++)
          if (*end == '\n') k++;
        *end = 0;
        printf("%s%s%s", c_dim(), src, c_reset());
        printf("%s...\n%s", c_dim(), c_reset());
      } else {
        printf("%s", src);
      }
      free(src);
    }
    free(p);
  }
  if (e) {
    printf("\n");
    print_kv(gg_tr("command", "命令"), "%s", e->name);
    print_kv(gg_tr("runs", "执行"), "%s", e->cmd);
    if (e->desc && *e->desc) print_kv(gg_tr("about", "说明"), "%s", e->desc);
  }
  printf("\n%s%s%s\n", c_dim(),
         gg_tr("run it with: gg ", "运行方式: gg "), name);
  return 0;
}

int cmd_edit(int argc, char **argv) {
  const char *name = argc ? argv[0] : 0;
  if (!name) {
    if (!T.interactive) {
      gg_error("%s", gg_tr("usage: gg edit <module>", "用法: gg edit <模块>"));
      return 1;
    }
    const char **items = malloc(sizeof(char *) * (size_t)(modules_count() + 1));
    for (int i = 0; i < modules_count(); i++)
      items[i] = modules_at(i)->name;
    int sel = tui_select(gg_tr("edit which module?", "编辑哪个模块?"), 0, items,
                         modules_count());
    free(items);
    if (sel < 0) return 0;
    name = modules_at(sel)->name;
  }
  module_info *m = modules_find(name);
  if (!m) {
    gg_error("%s: %s", name, gg_tr("no such module", "没有这个模块"));
    return 1;
  }
  char *path = module_path_of(name);
  if (!path) return 1;
  size_t len = 0;
  char *src = gg_read_file(path, &len);
  if (!src) src = gg_strdup("");
  if (!T.interactive) {
    printf("%s\n", path);
    free(src);
    free(path);
    return 0;
  }
  for (;;) {
    char title[256];
    snprintf(title, sizeof(title), "gg edit %s", name);
    char *edited = tui_textbox(title, gg_basename(path), src);
    if (!edited) {
      free(src);
      free(path);
      return 0;
    }
    free(src);
    src = edited;
    /* syntax check */
    char *tmp = gg_asprintf("%s.new", path);
    gg_write_file(tmp, src, strlen(src));
    char *err = 0;
    if (lua_check_syntax(tmp, &err) != 0) {
      tui_message(gg_tr("syntax error", "语法错误"), err ? err : "?");
      free(err);
      unlink(tmp);
      continue;
    }
    unlink(tmp);
    if (gg_write_file(path, src, strlen(src)) == 0) {
      gg_ok("%s %s", gg_tr("saved", "已保存:"), path);
      modules_rescan();
    } else {
      gg_error("%s: %s", path, strerror(errno));
    }
    break;
  }
  free(src);
  free(path);
  return 0;
}

/* ------------------------------------------------------------------ */
/* init — scaffold a new module                                        */
/* ------------------------------------------------------------------ */

static const char *TEMPLATE_BASIC =
    "-- gg module: %s\n"
    "-- %s\n"
    "--\n"
    "-- docs: `gg help modules`\n"
    "\n"
    "local M = {}\n"
    "\n"
    "M.title = \"%s\"\n"
    "M.desc = \"%s\"\n"
    "\n"
    "M.params = {\n"
    "  { name = \"target\", pos = 1, type = \"string\", required = true,\n"
    "    label = \"target\", help = \"what to operate on\" },\n"
    "  { name = \"verbose\", short = \"v\", type = \"bool\", default = false,\n"
    "    label = \"verbose\", help = \"print more\" },\n"
    "}\n"
    "\n"
    "function M.run(ctx)\n"
    "  local target = ctx.args.target\n"
    "  ctx.log(\"target: %%s\", tostring(target))\n"
    "  -- replace this with the real work\n"
    "  local out = ctx.capture(\"uname -a\")\n"
    "  ctx.log(out)\n"
    "  return 0\n"
    "end\n"
    "\n"
    "-- optional: a custom TUI (delete the whole function to get an\n"
    "-- auto-generated form instead)\n"
    "function M.tui(ctx)\n"
    "  local sel = ctx.tui.menu({\n"
    "    title = M.title,\n"
    "    items = { \"run with the parameters above\", \"edit the script\" },\n"
    "  })\n"
    "  if sel == 1 then return M.run(ctx) end\n"
    "  if sel == 2 then\n"
    "    ctx.tui.message(\"tip\", \"run `gg edit %s` in another shell\")\n"
    "  end\n"
    "  return 0\n"
    "end\n"
    "\n"
    "return M\n";

static const char *TEMPLATE_ACTIONS =
    "-- gg module with an action menu (handy for package managers, brew, aria2)\n"
    "\n"
    "local M = {}\n"
    "M.title = \"%s\"\n"
    "M.desc = \"%s\"\n"
    "\n"
    "M.params = {\n"
    "  { name = \"pkgs\", pos = 1, type = \"string\", label = \"packages\",\n"
    "    help = \"one or more package names\" },\n"
    "}\n"
    "\n"
    "M.actions = {\n"
    "  { name = \"list\", desc = \"show what is installed\",\n"
    "    cmd = { \"echo\", \"this is where the real command goes\" } },\n"
    "  { name = \"install\", desc = \"install the given packages\",\n"
    "    cmd = { \"echo\", \"install\", \"{pkgs}\" }, sudo = true, params = { \"pkgs\" } },\n"
    "}\n"
    "\n"
    "function M.run(ctx)\n"
    "  return 0\n"
    "end\n"
    "\n"
    "return M\n";

int cmd_init(int argc, char **argv) {
  const char *name = 0;
  const char *from = 0;
  const char *title = 0;
  const char *desc = 0;
  int actions = 0;
  for (int i = 0; i < argc; i++) {
    if (arg_is_flag(argv[i], "-f", "--from") && i + 1 < argc) from = argv[++i];
    else if (arg_is_flag(argv[i], "-t", "--title") && i + 1 < argc) title = argv[++i];
    else if (arg_is_flag(argv[i], "-d", "--desc") && i + 1 < argc) desc = argv[++i];
    else if (arg_is_flag(argv[i], "-a", "--actions")) actions = 1;
    else if (!name) name = argv[i];
  }
  if (!name && T.interactive) {
    char buf[128];
    snprintf(buf, sizeof(buf), "my-tool");
    if (tui_prompt(gg_tr("new module", "新建模块"),
                   gg_tr("name: ", "名字: "), buf, sizeof(buf), 0) != 0)
      return 0;
    name = gg_strdup(buf);
    char tbuf[160];
    snprintf(tbuf, sizeof(tbuf), "%s", name);
    if (tui_prompt(gg_tr("new module", "新建模块"),
                   gg_tr("title: ", "标题: "), tbuf, sizeof(tbuf), 0) == 0)
      title = gg_strdup(tbuf);
    actions = tui_confirm(gg_tr("new module", "新建模块"),
                          gg_tr("start from the action-menu template?",
                                "使用“动作菜单”模板？"),
                          0);
    char *path = gg_join(gg_modules_dir(),
                         gg_asprintf("%s.lua", name));
    gg_mkdir_p(gg_modules_dir());
    if (gg_exists(path)) {
      gg_error("%s %s", path, gg_tr("already exists", "已存在"));
      free(path);
      return 1;
    }
    sbuf b;
    sb_init(&b, 2048);
    if (actions)
      sb_addf(&b, TEMPLATE_ACTIONS, name, desc ? desc : name);
    else
      sb_addf(&b, TEMPLATE_BASIC, name, desc ? desc : name,
              title ? title : name, desc ? desc : name, name);
    gg_write_file(path, b.p, b.n);
    sb_free(&b);
    gg_ok("%s %s", gg_tr("created", "已创建:"), path);
    free(path);
    modules_rescan();
    strvec args;
    sv_init(&args);
    sv_push(&args, name);
    cmd_edit(1, sv_argv(&args));
    sv_free(&args);
    free((void *)name);
    free((void *)title);
    return 0;
  }
  if (!name) {
    gg_error("%s", gg_tr("usage: gg init <name> [--actions] [--from <module>]",
                         "用法: gg init <名字> [--actions] [--from <模块>]"));
    return 1;
  }
  if (from) {
    module_info *m = modules_find(from);
    if (!m) {
      gg_error("%s: %s", from, gg_tr("no such module", "没有这个模块"));
      return 1;
    }
    char *srcpath = module_path_of(from);
    size_t len = 0;
    char *src = srcpath ? gg_read_file(srcpath, &len) : 0;
    char *dest = gg_join(gg_modules_dir(), gg_asprintf("%s.lua", name));
    gg_mkdir_p(gg_modules_dir());
    if (gg_exists(dest)) {
      gg_error("%s %s", dest, gg_tr("already exists", "已存在"));
    } else if (src) {
      gg_write_file(dest, src, len);
      gg_ok("%s %s", gg_tr("created", "已创建:"), dest);
    } else {
      gg_error("%s", gg_tr("could not read the source module",
                           "无法读取源模块"));
    }
    free(src);
    free(dest);
    free(srcpath);
    modules_rescan();
    return 0;
  }
  {
    char *path = gg_join(gg_modules_dir(), gg_asprintf("%s.lua", name));
    gg_mkdir_p(gg_modules_dir());
    if (gg_exists(path)) {
      gg_error("%s %s", path, gg_tr("already exists", "已存在"));
      free(path);
      return 1;
    }
    sbuf b;
    sb_init(&b, 2048);
    if (actions)
      sb_addf(&b, TEMPLATE_ACTIONS, name, desc ? desc : name);
    else
      sb_addf(&b, TEMPLATE_BASIC, name, desc ? desc : name,
              title ? title : name, desc ? desc : name, name);
    gg_write_file(path, b.p, b.n);
    sb_free(&b);
    gg_ok("%s %s", gg_tr("created", "已创建:"), path);
    free(path);
    modules_rescan();
    return 0;
  }
}

/* ------------------------------------------------------------------ */
/* doctor                                                              */
/* ------------------------------------------------------------------ */

int cmd_doctor(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("%sgg doctor%s\n\n", c_bold(), c_reset());
  print_kv(gg_tr("version", "版本"), "%s", GG_VERSION);
#if GG_WINDOWS
  print_kv(gg_tr("platform", "平台"), "windows (%s)", "ape");
#elif GG_MACOS
  print_kv(gg_tr("platform", "平台"), "macos");
#else
  print_kv(gg_tr("platform", "平台"), "linux");
#endif
  print_kv("exe", "%s", gg_exe_path());
  print_kv(gg_tr("gg dir", "gg 目录"), "%s", gg_dir());
  print_kv(gg_tr("shell", "shell"), "%s", detect_shell());
  print_kv(gg_tr("interactive", "交互终端"), "%s",
           T.interactive ? gg_tr("yes", "是") : gg_tr("no", "否"));
  print_kv(gg_tr("language", "语言"), "%s", gg_lang_zh() ? "zh" : "en");
  printf("\n%s%s%s\n", c_bold(), gg_tr("tools", "工具"), c_reset());
  const char *tools[] = {"curl", "wget", "aria2c", "git", "python3", "node",
                         "tar", "unzip", "7z", "sudo", "apt", "apt-get", "brew",
                         "pacman", "dnf", "yum", "winget", "choco", "powershell",
                         "code", "nvim", "vim", 0};
  for (int i = 0; tools[i]; i++) {
    const char *p = gg_which(tools[i]);
    printf("  %s%-12s%s %s\n", p ? c_ok() : c_dim(), tools[i],
           c_reset(), p ? p : gg_tr("(missing)", "（未安装）"));
  }
  printf("\n%s%s%s\n", c_bold(), gg_tr("modules", "模块"), c_reset());
  printf("  %s%d%s %s\n", c_accent(), modules_count(), c_reset(),
         gg_tr("available (`gg ls`)", "个可用（`gg ls`）"));
  printf("  %s%d%s %s\n", c_accent(), reg_count(), c_reset(),
         gg_tr("registered commands", "个已注册命令"));
  char *bin = gg_join(gg_bin_dir(), "gg");
  printf("\n%s %s %s\n", gg_tr("shim:", "shim:"), bin,
         gg_exists(bin) ? gg_tr("ok", "正常")
                        : gg_tr("missing — run `gg active`",
                                "缺失 — 请运行 `gg active`"));
  free(bin);
  return 0;
}

/* ------------------------------------------------------------------ */
/* run / exec / tui                                                    */
/* ------------------------------------------------------------------ */

int cmd_run(int argc, char **argv) {
  if (argc < 1) {
    gg_error("%s", gg_tr("usage: gg run <command line>",
                         "用法: gg run <命令行>"));
    return 1;
  }
  /* a single argument is the whole command line, verbatim */
  char *line = 0;
  if (argc == 1) {
    line = gg_strdup(argv[0]);
  } else {
    sbuf b;
    sb_init(&b, 128);
    for (int i = 0; i < argc; i++) sb_addf(&b, "%s%s", i ? " " : "", argv[i]);
    line = b.p;
  }
  reg_log("run", line);
  int rc;
  int was_alt = T.interactive && T.in_alt;
  if (was_alt) tui_suspend();
  gg_echo_line(line);
  rc = proc_shell_run(line);
  if (was_alt) {
    gg_pause_key(gg_tr("-- done, press enter to go back",
                       "-- 完成，按回车返回"));
    tui_resume();
  }
  free(line);
  return rc;
}

int cmd_exec(int argc, char **argv) {
  if (argc < 1) {
    gg_error("%s", gg_tr("usage: gg exec <command> [args...]",
                         "用法: gg exec <命令> [参数...]"));
    return 1;
  }
  int was_alt = T.interactive && T.in_alt;
  if (was_alt) tui_suspend();
  int rc = proc_exec(argv, 0);
  if (was_alt) tui_resume();
  return rc;
}

int cmd_tui(int argc, char **argv) {
  if (argc < 1) {
    gg_error("%s", gg_tr("usage: gg tui <module>", "用法: gg tui <模块>"));
    return 1;
  }
  return lua_run_module(argv[0], argc - 1, argv + 1, 1);
}

/* ------------------------------------------------------------------ */
/* modules / config / upgrade                                          */
/* ------------------------------------------------------------------ */

static int module_copy_tree(const char *src, const char *dst) {
  if (gg_is_file(src)) return gg_copy_file(src, dst);
  if (!gg_is_dir(src) || gg_mkdir_p(dst) != 0) return -1;
  DIR *d = opendir(src);
  if (!d) return -1;
  int rc = 0;
  struct dirent *de;
  while ((de = readdir(d))) {
    if (gg_streq(de->d_name, ".") || gg_streq(de->d_name, "..")) continue;
    char *from = gg_join(src, de->d_name);
    char *to = gg_join(dst, de->d_name);
    if (module_copy_tree(from, to) != 0) rc = -1;
    free(from);
    free(to);
    if (rc != 0) break;
  }
  closedir(d);
  return rc;
}

static int module_remove_tree(const char *path) {
  if (!gg_is_dir(path)) return unlink(path);
  DIR *d = opendir(path);
  if (!d) return -1;
  int rc = 0;
  struct dirent *de;
  while ((de = readdir(d))) {
    if (gg_streq(de->d_name, ".") || gg_streq(de->d_name, "..")) continue;
    char *child = gg_join(path, de->d_name);
    if (module_remove_tree(child) != 0) rc = -1;
    free(child);
    if (rc != 0) break;
  }
  closedir(d);
  if (rc == 0 && rmdir(path) != 0) rc = -1;
  return rc;
}

static int modules_install_source(const char *input, int force) {
  char *src = gg_path_abs(input);
  size_t n = strlen(src);
  while (n > 1 && (src[n - 1] == '/' || src[n - 1] == '\\')) src[--n] = 0;
  const char *base = gg_basename(src);
  if (!*base || gg_streq(base, ".") || gg_streq(base, "..")) {
    gg_error("%s", gg_tr("invalid module path", "模块路径无效"));
    free(src);
    return 1;
  }

  int directory = gg_is_dir(src);
  if (!directory && (!gg_is_file(src) || !gg_endswith(base, ".lua"))) {
    gg_error("%s", gg_tr("install expects a .lua file or a module directory",
                         "install 需要 .lua 文件或模块目录"));
    free(src);
    return 1;
  }
  char *name = directory ? gg_strdup(base)
                         : gg_strndup(base, strlen(base) - 4);
  if (!*name || strchr(name, '/') || strchr(name, '\\') ||
      gg_streq(name, ".") || gg_streq(name, "..")) {
    gg_error("%s", gg_tr("invalid module name", "模块名无效"));
    free(name);
    free(src);
    return 1;
  }
  if (directory) {
    char *init = gg_join(src, "init.lua");
    char *leaf = gg_asprintf("%s.lua", name);
    char *named = gg_join(src, leaf);
    free(leaf);
    int valid = gg_is_file(init) || gg_is_file(named);
    free(init);
    free(named);
    if (!valid) {
      gg_error("%s", gg_tr("module directory needs init.lua or <name>.lua",
                           "模块目录需要 init.lua 或 <目录名>.lua"));
      free(name);
      free(src);
      return 1;
    }
  }

  gg_mkdir_p(gg_modules_dir());
  char *target;
  if (directory) {
    target = gg_join(gg_modules_dir(), name);
  } else {
    char *leaf = gg_asprintf("%s.lua", name);
    target = gg_join(gg_modules_dir(), leaf);
    free(leaf);
  }
  if (gg_streq(src, target)) {
    gg_info("%s %s", gg_tr("module already installed:", "模块已安装:"), name);
    free(target);
    free(name);
    free(src);
    return 0;
  }
  if (gg_exists(target)) {
    if (!force && (!T.interactive ||
                   !tui_confirm(gg_tr("replace module?", "覆盖模块?"), target, 0))) {
      gg_error("%s %s", target,
               gg_tr("already exists (use -f to replace)", "已存在（用 -f 覆盖）"));
      free(target);
      free(name);
      free(src);
      return 1;
    }
    if (module_remove_tree(target) != 0) {
      gg_error("%s %s", gg_tr("could not replace", "无法覆盖"), target);
      free(target);
      free(name);
      free(src);
      return 1;
    }
  }
  int rc = directory ? module_copy_tree(src, target)
                     : gg_copy_file(src, target);
  if (rc != 0) {
    module_remove_tree(target);
    gg_error("%s %s", gg_tr("could not install module from", "无法安装模块:"), src);
    free(target);
    free(name);
    free(src);
    return 1;
  }
  modules_rescan();
  gg_ok("%s %s %s", gg_tr("installed module", "已安装模块"), name, target);
  free(target);
  free(name);
  free(src);
  return 0;
}

static int modules_install_is_local(const char *arg) {
  if (!arg || !*arg) return 0;
  if (arg[0] == '.' || arg[0] == '/' || arg[0] == '\\' || arg[0] == '~')
    return 1;
#if GG_WINDOWS
  if (isalpha((unsigned char)arg[0]) && arg[1] == ':') return 1;
#endif
  if (gg_endswith(arg, ".lua") || gg_exists(arg)) return 1;
  return 0;
}

static int modules_registry_action(const char *action) {
  static const char *const actions[] = {
      "catalog", "help", "info", "install", "list", "run", "search",
      "test", "update", 0};
  for (int i = 0; actions[i]; i++)
    if (gg_streq(action, actions[i])) return 1;
  return 0;
}

int cmd_modules(int argc, char **argv) {
  if (argc >= 1 && gg_streq(argv[0], "install")) {
    const char *source = 0;
    int force = 0;
    for (int i = 1; i < argc; i++) {
      if (arg_is_flag(argv[i], "-f", "--force")) force = 1;
      else if (!source) source = argv[i];
    }
    if (!source || !modules_install_is_local(source))
      return lua_run_module("modstore", argc, argv, 0);
    return modules_install_source(source, force);
  }
  if (argc >= 1 && modules_registry_action(argv[0]))
    return lua_run_module("modstore", argc, argv, 0);
  if (argc >= 1 && (gg_streq(argv[0], "install-examples") ||
                    gg_streq(argv[0], "examples"))) {
    int force = 0;
    for (int i = 1; i < argc; i++)
      if (arg_is_flag(argv[i], "-f", "--force")) force = 1;
    lua_open_runtime();
    char *out = 0;
    char expr[64];
    snprintf(expr, sizeof(expr), "gg.modules.install_examples(%s)",
             force ? "true" : "false");
    lua_eval_expr_string(expr, &out);
    gg_ok("%s %s", gg_tr("installed examples:", "已安装示例模块:"),
          out ? out : "0");
    free(out);
    modules_rescan();
    return 0;
  }
  if (argc >= 1 && gg_streq(argv[0], "path")) {
    printf("%s\n", gg_modules_dir());
    return 0;
  }
  return cmd_ls(0, 0);
}

/* ------------------------------------------------------------------ */
/* bundle — copy this binary plus your files into one self-contained    */
/* executable (redbean style)                                          */
/* ------------------------------------------------------------------ */

static void bundle_usage(void) {
  printf("%s\n", gg_tr(
      "usage: gg bundle <target[.gg]> [file-or-dir ...] [rm:pattern]...\n"
      "\n"
      "  Writes a copy of this binary with a zip archive glued to it, so the\n"
      "  copy runs your scripts and reads your assets on any machine.\n"
      "\n"
      "  file.lua      -> modules/<name>.lua   (`gg <name>` in the copy)\n"
      "  directory/    -> directory/**         (walked recursively)\n"
      "  name=path     -> stored under that exact zip name\n"
      "  rm:pattern    -> drop every entry matching the glob\n"
      "  init.lua      -> startup hook, runs once before the lua state is used\n"
      "  -f            overwrite an existing target\n"
      "\n"
      "  example:  gg bundle dl.gg dl.lua assets/\n"
      "            ./dl.gg dl https://example.com/big.iso",
      "用法: gg bundle <目标[.gg]> [文件或目录 ...] [rm:模式]...\n"
      "\n"
      "  生成一个把本二进制和 zip 归档粘在一起的副本，拷到任何机器上都能直接跑你的脚本、\n"
      "  读你的资源文件，不需要装 gg。\n"
      "\n"
      "  file.lua      -> modules/<名字>.lua   （副本里 `gg <名字>` 就能跑）\n"
      "  dir/          -> dir/**                （递归打包）\n"
      "  name=path     -> 用指定的 zip 内路径存放\n"
      "  rm:模式       -> 删掉归档里匹配的条目\n"
      "  init.lua      -> 启动钩子，Lua 环境就绪时执行一次\n"
      "  -f            覆盖已存在的目标文件\n"
      "\n"
      "  例:  gg bundle dl.gg dl.lua assets/\n"
      "       ./dl.gg dl https://example.com/big.iso"));
}

int cmd_bundle(int argc, char **argv) {
  char **files = calloc((size_t)(argc + 1), sizeof(char *));
  char **rms = calloc((size_t)(argc + 1), sizeof(char *));
  int nfiles = 0, nrms = 0, force = 0;
  const char *target = 0;
  for (int i = 0; i < argc; i++) {
    const char *a = argv[i];
    if (arg_is_flag(a, "-h", "--help")) {
      bundle_usage();
      free(files);
      free(rms);
      return 0;
    }
    if (arg_is_flag(a, "-f", "--force")) {
      force = 1;
      continue;
    }
    if (gg_startswith(a, "rm:")) {
      rms[nrms++] = (char *)a + 3;
      continue;
    }
    if (!target) {
      target = a;
      continue;
    }
    files[nfiles++] = (char *)a;
  }
  if (!target) {
    bundle_usage();
    free(files);
    free(rms);
    return 1;
  }
  /* a bare "dl" becomes "dl.gg"; anything with an extension is used as is */
  const char *out = target;
  char *owned = 0;
  if (!gg_endswith(target, ".gg") && !strchr(gg_basename(target), '.')) {
    owned = gg_asprintf("%s.gg", target);
    out = owned;
  }
  char *outdir = gg_dirname_of(out);
  if (*outdir) gg_mkdir_p(outdir);
  free(outdir);
  char err[512] = {0};
  char **zipnames = 0, **diskpaths = 0;
  int rc = 1;
  int n = bundle_expand_args((const char *const *)files, nfiles, &zipnames,
                             &diskpaths, err, sizeof(err));
  if (n < 0) {
    gg_error("%s", err);
  } else if (bundle_write(out, (const char *const *)zipnames,
                          (const char *const *)diskpaths, n, force,
                          (const char *const *)rms, nrms, err, sizeof(err)) != 0) {
    gg_error("%s", err);
  } else {
    /* report what the new file is: size on disk + what it carries */
    bundle_scan_file(out);
    struct stat st;
    size_t size = stat(out, &st) == 0 ? (size_t)st.st_size : 0;
    int mods = 0, assets = 0;
    for (int i = 0; i < n; i++) {
      if (gg_startswith(zipnames[i], "modules/")) mods++;
      else if (gg_startswith(zipnames[i], "assets/")) assets++;
    }
    if (n > 0) {
      printf("%s %s — %.2f MB", gg_tr("wrote", "已生成"), out,
             (double)size / (1024.0 * 1024.0));
      if (mods) printf(" · %d %s", mods, gg_tr("modules", "个模块"));
      if (assets) printf(" · %d assets", assets);
      printf("\n");
    } else {
      printf("%s %s — %.2f MB (%s)\n", gg_tr("wrote", "已生成"), out,
             (double)size / (1024.0 * 1024.0),
             gg_tr("no extra files given", "没有附加文件"));
    }
    int user = bundle_user_count();
    if (user > 0) {
      printf("%s%s:%s\n", c_dim(), gg_tr("carries", "内含"), c_reset());
      for (int i = 0; i < user && i < 40; i++)
        printf("%s  %s%s\n", c_dim(), bundle_user_name(i), c_reset());
    }
    printf("%s%s%s\n", c_dim(),
           gg_tr("run it anywhere: the file is a complete gg with your script "
                 "inside",
                 "拷到任何机器都能直接跑：这个文件就是带了脚本的完整 gg"),
           c_reset());
    rc = 0;
  }
  for (int i = 0; i < (n > 0 ? n : 0); i++) {
    free(zipnames[i]);
    free(diskpaths[i]);
  }
  free(zipnames);
  free(diskpaths);
  free(files);
  free(rms);
  free(owned);
  return rc;
}

int cmd_config(int argc, char **argv) {
  for (int i = 0; i < argc; i++) {
    if (arg_is_flag(argv[i], 0, "--path")) {
      printf("%s\n", gg_config_path());
      return 0;
    }
  }
  if (!gg_exists(gg_config_path())) {
    gg_mkdir_p(gg_dir());
    const char *tpl =
        "-- gg configuration\n"
        "-- executed at startup; return a table of settings\n"
        "return {\n"
        "  -- lang = \"zh\",   -- force the interface language\n"
        "}\n";
    gg_write_file(gg_config_path(), tpl, strlen(tpl));
  }
  if (!T.interactive) {
    printf("%s\n", gg_config_path());
    return 0;
  }
  size_t len = 0;
  char *src = gg_read_file(gg_config_path(), &len);
  if (!src) src = gg_strdup("");
  char *edited = tui_textbox(gg_tr("gg config", "gg 配置"), "config.lua", src);
  if (edited) {
    gg_write_file(gg_config_path(), edited, strlen(edited));
    gg_ok("%s", gg_tr("config saved", "配置已保存"));
    free(edited);
  }
  free(src);
  return 0;
}

int cmd_upgrade(int argc, char **argv) {
  int check_only = 0;
  for (int i = 0; i < argc; i++)
    if (arg_is_flag(argv[i], "-n", "--check")) check_only = 1;
  if (!gg_have("curl") && !gg_have("wget")) {
    gg_error("%s", gg_tr("upgrade needs curl or wget", "升级需要 curl 或 wget"));
    return 1;
  }
  printf("%s%s%s\n", c_dim(),
         gg_tr("checking the latest release…", "正在检查最新版本…"), c_reset());
  int status = 0;
  char *json = proc_shell_capture(
      "curl -fsSL --max-time 20 https://api.github.com/repos/ejir/gg/releases/latest",
      &status);
  if (status != 0 || !json || !*json) {
    if (!json || !*json) {
      free(json);
      json = proc_shell_capture(
          "wget -qO- --timeout=20 https://api.github.com/repos/ejir/gg/releases/latest",
          &status);
    }
  }
  if (status != 0 || !json || !*json) {
    gg_error("%s", gg_tr("could not reach the release API",
                         "无法访问发布 API"));
    printf("%s%s%s\n", c_dim(),
           gg_tr("(no releases published yet? build from source: make ape)",
                 "（还没有发布版本？可以直接从源码构建: make ape）"),
           c_reset());
    free(json);
    return 1;
  }
  lua_open_runtime();
  char *tag = 0, *url = 0;
  {
    /* crude but dependency free JSON plucking */
    char *t = strstr(json, "\"tag_name\"");
    if (t) {
      char *q1 = strchr(t, ':');
      if (q1) {
        char *q = strchr(q1, '"');
        if (q) {
          char *e = strchr(q + 1, '"');
          if (e) tag = gg_strndup(q + 1, (size_t)(e - q - 1));
        }
      }
    }
    /* release assets are named gg, gg.exe and gg.com */
    const char *want = GG_WINDOWS ? "/gg.exe" : "/gg";
    char *p = json;
    while ((p = strstr(p, "browser_download_url"))) {
      char *q = strchr(p, '"');
      if (!q) break;
      q = strchr(q + 1, '"');
      if (!q) break;
      char *e = strchr(q + 1, '"');
      if (!e) break;
      char *cand = gg_strndup(q + 1, (size_t)(e - q - 1));
      if (gg_endswith(cand, want)) {
        url = cand;
        break;
      }
      free(cand);
      p = e;
    }
  }
  free(json);
  if (!tag) {
    gg_error("%s", gg_tr("could not parse the release information",
                         "无法解析发布信息"));
    return 1;
  }
  printf("%s %s %s (gg %s)\n", gg_tr("latest release:", "最新版本:") , tag,
         "", GG_VERSION);
  if (check_only) {
    free(tag);
    free(url);
    return 0;
  }
  if (!url) {
    gg_error("%s", gg_tr("no asset for this platform in that release",
                         "该版本没有对应平台的二进制"));
    free(tag);
    return 1;
  }
  const char *exe = gg_exe_path();
  char *tmp = gg_asprintf("%s.new", exe);
  char *cmd = gg_asprintf("curl -fL --retry 3 -o %s %s", tmp, url);
  printf("%s%s%s\n", c_dim(), gg_tr("downloading…", "正在下载…"), c_reset());
  int rc = proc_shell_run(cmd);
  free(cmd);
  if (rc != 0 || !gg_is_file(tmp)) {
    gg_error("%s", gg_tr("download failed", "下载失败"));
    free(tmp);
    free(tag);
    free(url);
    return 1;
  }
  gg_chmod_x(tmp);
  /* replace the running binary: rename works even while it is running on
   * both unix and windows */
  char *old = gg_asprintf("%s.old", exe);
  unlink(old);
  if (rename(exe, old) != 0) {
    gg_warn("%s: %s", exe, strerror(errno));
  }
  if (rename(tmp, exe) != 0) {
    gg_error("%s: %s", exe, strerror(errno));
    rename(old, exe);
    free(tmp);
    free(old);
    free(tag);
    free(url);
    return 1;
  }
  unlink(old);
  free(old);
  free(tmp);
  gg_ok("%s %s", gg_tr("upgraded to", "已升级到"), tag);
  free(tag);
  free(url);
  return 0;
}

/* ------------------------------------------------------------------ */
/* version                                                             */
/* ------------------------------------------------------------------ */

int cmd_version(void) {
  printf("gg %s%s%s  %s\n", c_bold(), GG_VERSION, c_reset(),
         gg_tr("cosmopolitan single-file multitool",
               "cosmopolitan 单文件多用工具"));
  printf("%s%s%s\n", c_dim(), GG_URL, c_reset());
  return 0;
}
