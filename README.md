# gg — one binary. every platform. lua on top. · 一个二进制，全平台，用 Lua 扩展

[![build](https://github.com/ejir/gg/actions/workflows/ci.yml/badge.svg)](https://github.com/ejir/gg/actions/workflows/ci.yml)
[![release](https://img.shields.io/github/v/release/ejir/gg)](https://github.com/ejir/gg/releases)
[![license](https://img.shields.io/badge/license-ISC-blue)](LICENSE)

`gg` 是一个**单文件跨平台二进制**（基于 [Cosmopolitan Libc](https://github.com/jart/cosmopolitan) 的 Actually Portable Executable），
内置 [Lua 5.4](https://www.lua.org/)，自带 TUI 组件，用来把你日常敲的一长串命令变成 `gg 名字`：

```
# 以前 / before
aria2c.exe -c -x 16 -s 16 -k 10M --disk-cache=128M --check-certificate=false <url>
sudo apt update && sudo apt install -y build-essential

# 现在 / now
gg aria2c <url>
gg apt            # 打开菜单：update / install / remove / search …
```

同一个文件（`gg` / `gg.exe` / `gg.com`，字节完全相同）可以直接在
**Linux / macOS / Windows / FreeBSD / OpenBSD / NetBSD** 上运行，不需要编译器、不需要运行时、
不需要安装器 —— 它甚至能把自己注册到 `PATH` 里。

```
$ gg
 gg one binary · lua scripting · everywhere                        gg 0.1.0 · zh
────────────────────────────────────────────────────────────────────────────────
 ⚙ 把 gg 加入 PATH                │ aria2c 下载助手 / aria2c downloader
 ⚙ 检查本机环境                   │ 模块 · aria2c
 ⚙ 写一个新模块                   │
 ⚙ 注册一条命令行                 │ 带推荐参数的 aria2c 封装，支持多链接、
 ◆ apt 包管理器 / package manager │ 断点续传和实时进度
 ◆ aria2c aria2c 下载助手 / aria… │
 ◆ demo gg demo · 功能演示        │ ~/.gg/modules/aria2c.lua
 ◆ hello hello · 示例模块         │
────────────────────────────────────────────────────────────────────────────────
 filter: type to search
 ↑↓ 移动  ⏎ 运行  e 编辑  n 新建  a 添加  d 删除  l 快捷方式  s 配置  q 退出
```

---

## 1. 安装 / install

### 一键安装

```sh
# Linux / macOS / BSD
curl -fsSL https://raw.githubusercontent.com/ejir/gg/main/scripts/install.sh | sh

# Windows (PowerShell)
irm https://raw.githubusercontent.com/ejir/gg/main/scripts/install.ps1 | iex
```

安装脚本做的事情就三步：下载一个文件到 `~/.gg/bin/gg`，加可执行位，运行 `gg active` 让它出现在 `PATH` 里。

### 手动安装（这才是重点：它就是一个文件）

1. 从 [releases](https://github.com/ejir/gg/releases) 下载 `gg`（Windows 上保存成 `gg.exe` 或 `gg.com`）。
2. 放到任意目录，`chmod +x gg`（Windows 不需要）。
3. 运行一次：

```sh
gg active          # 把 ~/.gg/bin 写进你的 shell 配置，生成 shim 目录
```

`gg active` 支持 **bash / zsh / fish / PowerShell / cmd**，它会：

* 创建 `~/.gg/bin/gg`（软链或副本）；
* 在 `~/.bashrc`（或 `~/.zshrc`、`~/.config/fish/config.fish`、`$PROFILE`…）里写入一个可见的块：
  ```sh
  # >>> gg >>>
  case ":$PATH:" in
    *":$HOME/.gg/bin:"*) ;;
    *) PATH="$HOME/.gg/bin:$PATH" ;;
  esac
  export PATH
  # <<< gg <<<
  ```
* 幂等：重复运行不会写第二份；`gg active off` 干净移除；`gg active status` 看现状；`gg active print` 只打印片段。

然后重开终端（或 `source ~/.bashrc`），`gg` 就到处都在了。

### 验证

```sh
gg doctor      # 平台、目录、shell、已装工具、模块数量一览
gg --version
```

---

## 2. 三种用法 / three ways to use it

| 命令 | 作用 |
| --- | --- |
| `gg` | 打开交互面板：左边是所有模块/命令，右边是详情和源码，支持过滤、编辑、运行 |
| `gg <名字> [参数…]` | 运行同名**模块**（Lua 脚本）→ 同名**注册命令** → 最后回退到系统 `PATH` |
| `gg lua 脚本` | `gg script.lua a b` 直接跑 Lua 脚本；`gg -e 'print(gg.version)'` 单行求值；`gg repl` 交互式 Lua |

```
gg                      # 面板
gg aria2c https://...   # 模块
gg apt                  # 模块（动作菜单）
gg dl https://...       # 你注册的命令（gg add dl ...）
gg aria2c …             # 若都没有，等价于直接在 PATH 里找 aria2c 执行
```

其他内置命令：

```
gg active [status|off|print]      PATH / shim 管理
gg ls [--json]                    列出所有模块与注册命令
gg show <name>                    详情 + 源码
gg init <name> [--actions]        生成模块骨架并打开内置编辑器
gg add <name> <命令> [-d 描述]     注册一条命令行
gg rm <name>                      删除模块或注册命令
gg edit <name>                    内置编辑器（带 Lua 语法高亮 + 语法检查）
gg link <name> / gg unlink       把模块/命令做成 shell 快捷命令（~/.gg/bin 里）
gg run <命令行>                   通过 shell 执行（TUI 里会暂停界面、跑完按回车返回）
gg modules install-examples       把内置示例模块写到 ~/.gg/modules
gg config / gg doctor / gg upgrade
gg help modules|api|active|keys   内置帮助
```

---

## 3. 例子一：aria2c 那一长串

以前：

```bat
aria2c.exe -c -x 16 -s 16 -k 10M --disk-cache=128M --check-certificate=false <url>
```

现在，注册成命令（不需要写脚本）：

```sh
gg add dl 'aria2c -c -x 16 -s 16 -k 10M --disk-cache=128M --check-certificate=false' -d 'aria2c 快速下载'
gg dl https://example.com/big.iso          # 追加的参数会原样接在后面
```

想要界面、想支持多链接、想改参数？用模块（`gg aria2c`，源码见 [`examples/aria2c.lua`](examples/aria2c.lua)）：

```lua
M.params = {
  { name = "url", pos = 1, type = "string", required = true, label = "下载地址 / URL" },
  { name = "jobs", short = "x", type = "int", default = 16, label = "连接数" },
  { name = "chunk", short = "k", type = "string", default = "10M", label = "分片大小" },
  { name = "cache", type = "string", default = "128M", label = "磁盘缓存" },
  { name = "insecure", type = "bool", default = true, label = "忽略证书" },
}

function M.run(ctx)
  local a = ctx.args
  local argv = { "aria2c", "-c", "-x", tostring(a.jobs), "-s", tostring(a.jobs),
                 "-k", a.chunk, "--disk-cache=" .. a.cache,
                 "--file-allocation=none", "--summary-interval=1" }
  if a.insecure then argv[#argv + 1] = "--check-certificate=false" end
  argv[#argv + 1] = a.url
  for _, extra in ipairs(ctx.rest) do argv[#argv + 1] = extra end
  return ctx.run({ argv = argv })   -- 前台运行，TUI 自动让出终端
end
```

```
$ gg aria2c              # 不带参数 → 自动生成 TUI 表单
 gg aria2c 下载助手 / aria2c downloader                         填写参数，ctrl-s 执行
────────────────────────────────────────────────────────────────────────────────
 ▸ URL                    https://example.com/big.iso
   保存目录 / dir         /home/me/Downloads
   文件名 / file name
   连接数 / connections   16
   分片大小 / chunk       10M
   磁盘缓存 / disk cache  128M
   [x] 忽略证书 / ignore certificates
   [x] 断点续传 / continue
```

`gg aria2c https://…` 直接跑；`gg aria2c --help` 看用法；参数、布尔开关、枚举全都自动解析。

---

## 4. 例子二：apt / 包管理器

[`examples/apt.lua`](examples/apt.lua) 用「动作菜单」把常见操作列出来，并自动识别本机的
`apt / apt-get / dnf / yum / pacman / zypper / brew / winget / choco`：

```
$ gg apt
 gg apt                                                                 esc to go back
──────────────────────────────────────────────────────────────────────────────────────
 ▸ update           刷新软件源索引 / refresh the index
   upgrade          升级所有已安装的包 / upgrade everything
   install          安装软件包 / install packages
   remove           卸载软件包 / remove packages
   search           搜索软件包 / search
   list             列出已安装 / list installed
   clean            清理缓存 / clean the cache
   autoremove       清理无用依赖 / autoremove
```

选中 `install` 会弹输入框问包名，然后执行 `sudo apt install -y <包名>`；
`apt update && apt install` 这种日常操作从此不用记 sudo 和 `-y`。

想让 `apt` 直接进入动作菜单，也可以做成 shell 快捷命令：

```sh
gg link apt        # 之后直接敲：apt update / apt install foo
```

---

## 5. 写自己的模块 / writing modules

模块就是一个 Lua 文件，放在 `~/.gg/modules/<名字>.lua`，返回一个表：

```lua
local M = {}

M.title = "我的工具 / my tool"
M.desc  = "一句话说明"

M.params = {                       -- 可选：自动解析命令行 + 自动生成表单
  { name = "target", pos = 1, type = "string", required = true,
    label = "目标", help = "要处理的东西" },
  { name = "force", short = "f", type = "bool", label = "强制" },
  { name = "mode", type = "choice", choices = { "fast", "safe" }, default = "fast" },
}

function M.run(ctx)                -- CLI 与 TUI 都走这里
  ctx.log("target=%s force=%s", ctx.args.target, tostring(ctx.args.force))
  local out = ctx.capture("uname -a")     -- 拿输出
  ctx.log(out)
  return ctx.run({ argv = { "ls", "-la" } }) and 0 or 1   -- 前台跑（TUI 会让出终端）
end

function M.tui(ctx)                -- 可选：自定义界面（不写就自动生成表单）
  local sel = ctx.tui.menu({ title = M.title, items = { "开始", "退出" } })
  if sel == 1 then return M.run(ctx) end
  return 0
end

return M
```

命令行解析、`--help`、TUI 表单都是白送的：

```sh
gg mytool 目标 -f            # 直接跑
gg mytool                    # 终端里跑 → 自动表单（tab 切换、空格切换布尔、ctrl-s 执行）
gg mytool --help             # 用法
```

### `ctx` / `gg` API 速查

`ctx` 在模块里就是 `gg`（同一个表，`__index` 指向它），另外多了 `ctx.name / ctx.args / ctx.rest / ctx.argv / ctx.module`。

| 分类 | API |
| --- | --- |
| 执行 | `gg.run{argv={…}}`（前台、TUI 感知）、`gg.spawn{argv=…, capture=, on_line=, sudo=, cwd=, input=}`、`gg.capture(cmd)`、`gg.sh(cmd)`、`gg.exec{…}` |
| 环境 | `gg.which(name)` `gg.have` `gg.getenv/setenv` `gg.cwd` `gg.platform`（os/arch/cpus/root/lang…）`gg.exe` `gg.dir` `gg.home` |
| 文件 | `gg.exists` `gg.read/write` `gg.list` `gg.stat` `gg.mkdir` `gg.rm` `gg.copy` `gg.move` `gg.mkstemp` `gg.join_path` `gg.abs` `gg.basename/dirname/ext` |
| 文本 | `gg.trim/split/join` `gg.str.quote` `gg.json.encode/decode` `gg.i18n(en, zh)` |
| 交互 | `gg.confirm` `gg.ask` `gg.select` `gg.message` `gg.sleep` |
| UI | `gg.tui.available()`、`gg.tui.menu{title,items}`、`gg.tui.form{title,fields}`、`gg.tui.textbox{title,text}`、`gg.tui.progress{title}` → `:set(pct,msg)` `:log(line)` `:done(ok,msg)` |
| 注册表 | `gg.registry.add/list/remove/get`、`gg.modules.list/path/install_examples` |
| 颜色 | `gg.style("accent")`、`gg.colorize(text, "ok")` |

完整说明：`gg help api`、[docs/MODULES.md](docs/MODULES.md)。

### 流式输出 / 进度条

```lua
local bar = ctx.tui.progress({ title = "下载中", subtitle = "aria2c" })
ctx.spawn{
  argv = { "aria2c", "-x", "16", url },
  on_line = function(line)      -- 子进程每输出一行就回调一次
    bar:log(line)
    local pct = line:match("(%d+)%%")
    if pct then bar:set(tonumber(pct)) end
  end,
}
bar:done(true, "完成")
```

`gg demo` 里可以直接看这些组件的效果（平台信息 / 进度条 / 流式读取 / JSON / 编辑器 / 文件工具）。

---

## 6. 为什么是单文件 / how the single file works

* `gg` 用 [cosmocc](https://github.com/jart/cosmopolitan) 编译成 **APE（Actually Portable Executable）**：
  同一个文件里同时带着 Linux ELF、macOS Mach-O、Windows PE 的入口，还有 x86-64 与 aarch64 两套代码。
  Windows 上把它叫 `gg.exe` 就能跑，Linux/macOS 上 `chmod +x` 就能跑（不需要 `ape` loader：
  文件里自带 loader，必要时会自己重新执行）。
* Lua 5.4 解释器、TUI 组件、示例模块全部**编进这一个文件**（`tools/embed.sh` 把 `.lua`
  源码变成 C 数组），所以它不依赖任何外部文件 —— 复制过去就能用，删掉目录也不会坏。
* 终端上用的是纯 ANSI 转义序列（Windows 10+ 的 console 由 Cosmopolitan 自动开启
  virtual terminal processing），所以同一套界面在三个系统上长得一样；`NO_COLOR` / `TERM=dumb` /
  重定向时会自动降级为纯文本，管道里也能用。

```sh
$ ls -l bin/gg
-rwxr-xr-x 1 user user 1510169 bin/gg   # ≈1.5 MB：Lua 5.4 + TUI + 示例模块全在里面
```

---

## 7. 从源码构建 / building

```sh
git clone https://github.com/ejir/gg && cd gg

make host            # 用本机 cc 编译（快速迭代、跑测试）
make test            # 54 项无头测试 + 20 项伪终端 TUI 测试
make ape             # 需要 cosmocc → 产出 bin/gg、bin/gg.exe、bin/gg.com
make install         # 装到 ~/.gg/bin 并执行 gg active
```

没有 cosmocc？一条命令装：

```sh
sh scripts/fetch-cosmocc.sh          # github release → npm 镜像 → cosmo.zip 依次尝试
export PATH="$HOME/.cosmocc/bin:$PATH"
make ape
```

构建只依赖 `make`、一个 C 编译器（cosmocc 自带）和 Lua 源码（已 vendored 在 `vendor/lua`，
MIT 许可），不需要网络。

### 目录结构

```
src/             C 源码（TUI、进程、注册表、Lua 桥、内置命令）
vendor/lua/      Lua 5.4.7 源码（MIT）
examples/        内置示例模块（会被编进二进制，也可 install-examples 落盘）
bin/             预编译的单文件二进制（gg / gg.exe / gg.com，字节相同）
tools/           embed.sh（把文件编进 C）、ggshot.py（在伪终端里截图，开发用）
tests/           run.sh（无头断言）、pty_test.py（伪终端驱动 TUI）
scripts/         fetch-cosmocc.sh、install.sh、install.ps1
docs/            模块与 API 说明
```

---

## 8. FAQ

**它和 shell 别名有什么区别？**
别名只在你的 shell 里、只在那台机器上；gg 的模块是可移植的文件 + 一行 Lua，`gg active` 之后
在 bash/zsh/fish/PowerShell/cmd 里都叫得出来，还能在不同系统之间直接拷贝 `~/.gg/modules`。

**能当通用 Lua 解释器用吗？**
可以：`gg script.lua`、`gg -e '…'`、`gg repl`，标准库齐全（`io.popen` 在 Windows 上也能用，
Cosmopolitan 提供了 Bourne 风格的命令解释器）。

**和 PATH 里的程序重名怎么办？**
优先级是：内置命令 → 模块 → 注册命令 → `PATH`。想强制走系统程序用 `gg run <命令行>` 或 `gg exec <程序>`。

**TUI 在管道/CI 里会坏吗？**
不会。检测到非终端（`GG_PLAIN=1`、`TERM=dumb`、重定向）时，菜单变成编号列表（取消返回 `nil`）、
表单直接用默认值、`gg ls --json` 输出机器可读的 JSON。管道里的 `ctx.confirm("删除吗？")` 默认按
"是" 处理，好让无人值守的脚本跑下去；把 `GG_ASSUME_YES=0` 设上就一律拒绝。

**怎么更新？**
`gg upgrade`（从 releases 下载并原子替换自己；Windows 上先改名旧文件再落新文件）。

**怎么卸载？**
`gg active off` 移除 PATH 配置，然后删掉 `~/.gg` 即可。

---

## License

ISC（见 [LICENSE](LICENSE)）。内置的 Lua 5.4.7 为 MIT 许可（`vendor/lua/LICENSE`）。
Cosmopolitan Libc 由 [Justine Tunney](https://justine.lol/cosmopolitan/) 开发，项目自身的
授权条款见其仓库。
