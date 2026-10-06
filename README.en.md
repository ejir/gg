# gg — one binary, every platform, Lua-powered

[![build](https://github.com/ejir/gg/actions/workflows/ci.yml/badge.svg)](https://github.com/ejir/gg/actions/workflows/ci.yml)
[![release](https://img.shields.io/github/v/release/ejir/gg)](https://github.com/ejir/gg/releases)
[![license](https://img.shields.io/badge/license-ISC-blue)](LICENSE)

**中文文档：[README.md](README.md)** · English documentation

`gg` is a single-file, cross-platform command launcher and Lua 5.4 runtime. Turn long commands into short, reusable modules:

```sh
gg aria2c https://example.com/file.iso
gg apt                         # package-manager menu
gg mytool arg1                 # your own Lua module
```

The release binary is an Actually Portable Executable (APE), built with [Cosmopolitan Libc](https://github.com/jart/cosmopolitan). The same file runs on Linux, macOS, Windows, FreeBSD, OpenBSD, and NetBSD; Lua and the TUI are included.

## Install

### One-line install

```sh
# Linux / macOS / BSD
curl -fsSL https://raw.githubusercontent.com/ejir/gg/main/scripts/install.sh | sh

# Windows PowerShell
irm https://raw.githubusercontent.com/ejir/gg/main/scripts/install.ps1 | iex
```

Or download `gg` from [Releases](https://github.com/ejir/gg/releases), make it executable on Unix, and run:

```sh
gg active
```

`gg active` creates `~/.gg/bin/gg`, adds that directory to the detected shell's startup file, and enables command completion. Reopen the shell (or source its startup file) to use `gg` by name. `gg active status`, `gg active print`, and `gg active off` show, print, or remove the managed configuration block.

Check the installation with `gg doctor` and `gg --version`.

## Usage

| Command | What it does |
| --- | --- |
| `gg` | Open the interactive dashboard. |
| `gg <name> [args...]` | Run a module, registered command, or program found on `PATH`, in that order. |
| `gg script.lua [args...]` | Run a Lua script. |
| `gg -e 'print(gg.version)'` | Evaluate Lua code. |
| `gg repl` | Start the interactive Lua REPL. |
| `gg completion [bash\|zsh\|fish\|powershell]` | Print shell completion code. |

Other useful commands:

```text
gg ls [--json]                    list modules and registered commands
gg show <name>                    show module/command details
gg add <name> <command>           register a shell command
gg rm <name>                      remove a user module or registered command
gg init <name> [--actions]        scaffold a Lua module
gg edit <name>                    edit a module in the built-in editor
gg link <name>                    create a shell shim in ~/.gg/bin
gg run <command line>             run a shell command
gg modules install <path>         install a module file or self-contained folder
gg modules install-examples        install the bundled examples
gg bundle app.gg <files...>        make a standalone gg application with bundled files
gg config / doctor / upgrade      configuration, diagnostics, self-update
```

### TUI and mouse

The dashboard and menus support mouse-wheel scrolling and clicks. A single click selects a row; a double click runs it. Click the filter line to focus it. Press `Tab` to switch focus; `Esc` clears the filter and leaves the filter field, and a second `Esc` goes back or exits. Arrow keys, Enter, and the keyboard shortcuts remain available.

### Shell completion

`gg active` enables dynamic completions for **bash, zsh, fish, and PowerShell**. Completions include built-ins, modules, registered commands, common subcommands, and module flags declared in `M.params`.

To load completion manually:

```sh
# bash / zsh
eval "$(gg completion bash)"   # use zsh instead of bash for zsh

# fish
gg completion fish | source

# PowerShell
gg completion powershell | Out-String | Invoke-Expression
```

## Example: aria2c

The bundled `aria2c` module turns common download flags into a convenient interface:

```sh
gg aria2c https://example.com/file.iso
gg aria2c --help
gg aria2c                     # opens the TUI form/menu
```

If `aria2c` is missing, gg checks for a supported package manager (apt, dnf, yum, pacman, zypper, apk, pkg, pkgin, pkg_add, Homebrew, winget, Chocolatey, or Scoop) and asks before installing. The default answer is **No**; installation is only run after explicit confirmation. The module forwards download arguments to `aria2c` and supports a TUI form when invoked interactively.

## Writing modules

A basic module is a Lua file at `~/.gg/modules/<name>.lua` that returns a table:

```lua
local M = {}
M.title = "My tool"
M.desc = "A short description"
M.params = {
  { name = "target", pos = 1, type = "string", required = true,
    label = "Target", help = "What to process" },
  { name = "force", short = "f", type = "bool", label = "Force" },
}

function M.run(ctx)
  ctx.log("target=%s force=%s", ctx.args.target, tostring(ctx.args.force))
  return ctx.run({ argv = { "ls", "-la" } }) and 0 or 1
end

return M
```

`M.params` provides argument parsing, `--help`, and an automatic TUI form. Implement `M.tui(ctx)` for a custom interface, or set `M.actions` for a generated action menu.

### Self-contained module folders

A module can carry its own helper code and assets as a directory. This is a **self-contained module**: its files stay together, rather than being placed in a shared project directory.

```text
mytool/
  mytool.lua             # or init.lua; returns the module table
  lib.lua
  assets/config.json
```

```lua
function M.run(ctx)
  local helper = dofile(gg.join_path(ctx.module_dir, "lib.lua"))
  local config = gg.read(gg.join_path(ctx.module_dir, "assets", "config.json"))
  -- ...
end
```

Install and run the folder:

```sh
gg modules install ./mytool
gg mytool
gg rm mytool                  # asks before removing the whole module folder
```

The directory entry point must be `init.lua` or `<directory-name>.lua`. `ctx.module_dir` is the directory containing the entry point; it is also set for ordinary single-file modules. Copy the module folder to another machine with gg to carry its helpers/assets with it. For a single executable that also includes gg and the Lua runtime, use `gg bundle` instead.

### Module API

Inside a module, `ctx` is the `gg` API table plus `ctx.name`, `ctx.args`, `ctx.rest`, `ctx.argv`, `ctx.module`, and `ctx.module_dir`.

- Processes: `ctx.run({ argv = {...}, sudo = true })`, `ctx.spawn(...)`, `ctx.capture(command)`, `ctx.exec(...)`.
- Programs: `gg.which(name)`, `gg.have(name)`, and `gg.refresh_tools()` (clear cached `PATH` lookups after installing a program).
- Files: `gg.read`, `gg.write`, `gg.list`, `gg.mkdir`, `gg.copy`, `gg.move`, `gg.rm`, `gg.join_path`.
- Prompts: `ctx.confirm`, `ctx.ask`, `ctx.select`, `ctx.message`.
- TUI: `ctx.tui.menu`, `ctx.tui.form`, `ctx.tui.textbox`, `ctx.tui.progress`.
- Other: `gg.json`, `gg.str`, `gg.platform`, `gg.assets` (for files embedded by `gg bundle`).

See [`docs/MODULES.md`](docs/MODULES.md) and `examples/*.lua` for the full API and examples.

## Standalone bundled applications

A module folder is portable between gg installations. If the recipient should not need to install gg or Lua, create a self-contained executable instead:

```sh
gg bundle myapp.gg mytool.lua assets/ init.lua
./myapp.gg mytool
```

`gg bundle` copies the running gg binary and appends a zip archive. Lua modules are placed under `modules/`, directories keep their paths, and bundled assets can be read with `gg.assets.read/list/have/dir`. The resulting file includes gg, Lua, TUI, modules, and assets. See `gg help bundle` for archive rules.

## Build and test

```sh
make host       # build with the host C compiler
make test       # headless tests and pseudo-terminal TUI tests
make ape        # APE release binaries; requires cosmocc
make install    # install under ~/.gg/bin and run gg active
```

Lua 5.4.7 is vendored under `vendor/lua`; host builds do not require network access. `scripts/fetch-cosmocc.sh` can download cosmocc for APE builds.

## License

ISC; see [LICENSE](LICENSE). The vendored Lua 5.4.7 source is MIT-licensed. Cosmopolitan Libc retains its upstream license.
