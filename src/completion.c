/* gg — shell completion generators and the internal completion query. */
#include "gg.h"

static int prefix_matches(const char *candidate, const char *prefix) {
  if (!candidate) return 0;
  if (!prefix) prefix = "";
  while (*prefix) {
    if (!*candidate || tolower((unsigned char)*candidate) !=
                            tolower((unsigned char)*prefix))
      return 0;
    candidate++;
    prefix++;
  }
  return 1;
}

static void emit_candidate(const char *candidate, const char *prefix) {
  if (prefix_matches(candidate, prefix)) puts(candidate);
}

static void emit_words(const char *const *words, const char *prefix) {
  for (int i = 0; words[i]; i++) emit_candidate(words[i], prefix);
}

static void emit_known_names(const char *prefix) {
  for (int i = 0; i < modules_count(); i++) {
    module_info *m = modules_at(i);
    if (m) emit_candidate(m->name, prefix);
  }
  for (int i = 0; i < reg_count(); i++) {
    reg_entry *e = reg_at(i);
    if (e) emit_candidate(e->name, prefix);
  }
}

static void emit_top_level(const char *prefix) {
  static const char *const commands[] = {
      "active", "add", "bundle", "completion", "config", "dashboard",
      "doctor", "edit", "exec", "help", "info", "init", "link", "list",
      "ls", "module", "modules", "new", "pack", "remove", "repl", "rm",
      "run", "self-update", "show", "tui", "unlink", "upgrade", "version",
      "--help", "--version", 0};
  emit_words(commands, prefix);
  emit_known_names(prefix);
}

static const char *lua_field_string(const char *begin, const char *end,
                                    const char *field) {
  size_t flen = strlen(field);
  for (const char *p = begin; p + flen <= end;) {
    if (*p == '\'' || *p == '"') {
      char quote = *p++;
      while (p < end && *p != quote) {
        if (*p == '\\' && p + 1 < end) p++;
        p++;
      }
      if (p < end) p++;
      continue;
    }
    if (*p == '-' && p + 1 < end && p[1] == '-') {
      while (p < end && *p != '\n') p++;
      continue;
    }
    if ((p == begin || !(isalnum((unsigned char)p[-1]) || p[-1] == '_')) &&
        p + flen <= end && !memcmp(p, field, flen) &&
        (p + flen == end ||
         !(isalnum((unsigned char)p[flen]) || p[flen] == '_'))) {
      const char *q = p + flen;
      while (q < end && isspace((unsigned char)*q)) q++;
      if (q >= end || *q != '=') {
        p += flen;
        continue;
      }
      q++;
      while (q < end && isspace((unsigned char)*q)) q++;
      if (q >= end || (*q != '\'' && *q != '"')) {
        p += flen;
        continue;
      }
      char quote = *q++;
      const char *e = q;
      while (e < end && *e != quote) {
        if (*e == '\\' && e + 1 < end) e++;
        e++;
      }
      if (e < end) return gg_strndup(q, (size_t)(e - q));
    }
    p++;
  }
  return 0;
}

static void emit_module_options(const char *name, const char *prefix) {
  emit_candidate("--help", prefix);
  emit_candidate("--tui", prefix);
  size_t len = 0;
  char *src = module_source_text(name, &len, 0);
  if (!src) return;
  const char *params = strstr(src, "M.params");
  const char *open = params ? strchr(params, '{') : 0;
  if (open) {
    int depth = 0, quote = 0, escape = 0, line_comment = 0;
    const char *entry = 0;
    for (const char *p = open; *p; p++) {
      if (line_comment) {
        if (*p == '\n') line_comment = 0;
        continue;
      }
      if (quote) {
        if (escape) escape = 0;
        else if (*p == '\\') escape = 1;
        else if (*p == quote) quote = 0;
        continue;
      }
      if (*p == '-' && p[1] == '-') {
        line_comment = 1;
        p++;
        continue;
      }
      if (*p == '\'' || *p == '"') {
        quote = *p;
        continue;
      }
      if (*p == '{') {
        if (depth == 1) entry = p;
        depth++;
      } else if (*p == '}') {
        if (depth == 2 && entry) {
          const char *end = p;
          const char *param = lua_field_string(entry, end, "name");
          const char *shrt = lua_field_string(entry, end, "short");
          const char *lng = lua_field_string(entry, end, "long");
          const char *type = lua_field_string(entry, end, "type");
          if (lng && *lng) {
            char *opt = gg_asprintf("--%s", lng);
            emit_candidate(opt, prefix);
            free(opt);
          } else if (param && *param) {
            char *opt = gg_asprintf("--%s", param);
            emit_candidate(opt, prefix);
            free(opt);
          }
          if (shrt && *shrt) {
            char *opt = gg_asprintf("-%s", shrt);
            emit_candidate(opt, prefix);
            free(opt);
          }
          if (type && gg_streq(type, "bool")) {
            const char *flag = lng && *lng ? lng : param;
            if (flag && *flag) {
              char *opt = gg_asprintf("--no-%s", flag);
              emit_candidate(opt, prefix);
              free(opt);
            }
          }
          free((void *)param);
          free((void *)shrt);
          free((void *)lng);
          free((void *)type);
          entry = 0;
        }
        if (depth > 0) depth--;
        if (depth == 0) break;
      }
    }
  }
  free(src);
}

int cmd_complete(int argc, char **argv) {
  int start = 0;
  if (argc > 0 && gg_streq(argv[0], "--")) start++;
  int n = argc - start;
  char **words = argv + start;
  if (n <= 1) {
    emit_top_level(n == 1 ? words[0] : "");
    return 0;
  }

  const char *root = words[0];
  const char *prefix = words[n - 1];
  if (gg_streq(root, "help")) {
    static const char *const topics[] = {
        "active", "api", "bundle", "commands", "keys", "lua", "modules",
        "paths", "script", 0};
    emit_words(topics, prefix);
  } else if (gg_streq(root, "active")) {
    static const char *const active[] = {"status", "off", "print", 0};
    emit_words(active, prefix);
  } else if (gg_streq(root, "modules") || gg_streq(root, "module")) {
    static const char *const modules[] = {"install", "install-examples", "path", 0};
    emit_words(modules, prefix);
  } else if (gg_streq(root, "completion")) {
    static const char *const shells[] = {"bash", "zsh", "fish", "powershell", 0};
    emit_words(shells, prefix);
  } else if (gg_streq(root, "show") || gg_streq(root, "info") ||
             gg_streq(root, "edit") || gg_streq(root, "link") ||
             gg_streq(root, "alias") || gg_streq(root, "unlink") ||
             gg_streq(root, "rm") || gg_streq(root, "remove")) {
    emit_known_names(prefix);
  } else if (modules_find(root)) {
    emit_module_options(root, prefix);
  }
  return 0;
}

static const char *completion_detect_shell(void) {
  const char *forced = getenv("GG_SHELL");
  if (forced && *forced) return forced;
  if (getenv("PSModulePath")) return "powershell";
  if (getenv("FISH_VERSION")) return "fish";
  if (getenv("ZSH_VERSION")) return "zsh";
  const char *shell = getenv("SHELL");
  if (shell && *shell) {
    const char *base = gg_basename(shell);
    if (strstr(base, "fish")) return "fish";
    if (strstr(base, "zsh")) return "zsh";
    if (strstr(base, "bash")) return "bash";
  }
  return "bash";
}

static const char *completion_bash =
    "# gg shell completion for bash\n"
    "_gg_complete() {\n"
    "  local cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
    "  COMPREPLY=( $(command gg __complete -- \"${COMP_WORDS[@]:1:COMP_CWORD}\" 2>/dev/null) )\n"
    "}\n"
    "complete -o nospace -F _gg_complete gg\n";

static const char *completion_zsh =
    "# gg shell completion for zsh\n"
    "_gg() {\n"
    "  local -a args candidates\n"
    "  args=(\"${words[2,CURRENT]}\")\n"
    "  candidates=(\"${(@f)$(command gg __complete -- \"${args[@]}\" 2>/dev/null)}\")\n"
    "  compadd -Q -- \"${candidates[@]}\"\n"
    "}\n"
    "if ! command -v compdef >/dev/null 2>&1; then autoload -Uz compinit && compinit -C; fi\n"
    "compdef _gg gg\n";

static const char *completion_fish =
    "# gg shell completion for fish\n"
    "function __gg_complete\n"
    "    set -l words (commandline -opc)\n"
    "    set -l words $words[2..-1]\n"
    "    set -l current (commandline -ct)\n"
    "    command gg __complete -- $words $current 2>/dev/null\n"
    "end\n"
    "complete -c gg -f -a '(__gg_complete)'\n";

static const char *completion_powershell =
    "# gg shell completion for PowerShell\n"
    "Register-ArgumentCompleter -CommandName gg -ScriptBlock {\n"
    "  param($wordToComplete, $commandAst, $cursorPosition)\n"
    "  $parts = @($commandAst.CommandElements | Select-Object -Skip 1 | ForEach-Object { $_.Extent.Text })\n"
    "  & gg __complete -- @parts 2>$null | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object {\n"
    "    [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)\n"
    "  }\n"
    "}\n";

int cmd_completion(int argc, char **argv) {
  const char *shell = argc > 0 ? argv[0] : completion_detect_shell();
  if (argc > 0 && (gg_streq(argv[0], "-h") || gg_streq(argv[0], "--help"))) {
    printf("usage: gg completion [bash|zsh|fish|powershell]\n");
    printf("Run `gg active` to enable completion in your shell startup file.\n");
    return 0;
  }
  if (gg_streq(shell, "bash")) fputs(completion_bash, stdout);
  else if (gg_streq(shell, "zsh")) fputs(completion_zsh, stdout);
  else if (gg_streq(shell, "fish")) fputs(completion_fish, stdout);
  else if (gg_streq(shell, "powershell") || gg_streq(shell, "pwsh"))
    fputs(completion_powershell, stdout);
  else {
    gg_error("unsupported completion shell: %s", shell);
    return 2;
  }
  return 0;
}
