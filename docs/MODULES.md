# gg modules · gg 模块开发指南

一个模块 = `~/.gg/modules/<名字>.lua` 里的一个 Lua 文件，`return` 一个表。
`gg <名字>` 会加载它；不带参数运行时，gg 会把参数表变成 TUI 表单。

```lua
local M = {}
M.title = "…"   -- 列表里显示的名字
M.desc  = "…"   -- 一句话说明
M.params = { … } -- 可选，见下
function M.run(ctx) … end
function M.tui(ctx) … end     -- 可选：自定义界面
M.actions = { … }             -- 可选：动作菜单（和 M.tui 二选一即可）
return M
```

内置示例（`gg modules install-examples` 可落盘到 `~/.gg/modules`）：
`hello.lua`（最小示例）、`aria2c.lua`（参数 + 自定义 TUI）、`apt.lua`（动作菜单 + 自动识别包管理器）、
`demo.lua`（各个 TUI 组件演示）。

---

## 1. 参数 `M.params`

```lua
M.params = {
  { name = "url",      pos = 1, type = "string", required = true,
    label = "下载地址", help = "支持多个" },
  { name = "out",      short = "o", long = "output", type = "string",
    label = "输出", help = "输出文件", default = nil },
  { name = "jobs",     short = "x", type = "int", default = 16,
    label = "连接数", help = "并发连接" },
  { name = "mode",     type = "choice", choices = { "fast", "safe" },
    default = "fast", label = "模式" },
  { name = "insecure", type = "bool", default = false, label = "忽略证书" },
}
```

| 字段 | 说明 |
| --- | --- |
| `name` | 必须，`ctx.args.<name>` |
| `pos` | 第 N 个位置参数（可省略；`pos = 1` 表示 `gg mod foo` 里的 `foo`） |
| `short` / `long` | `-o value` / `--output value` / `--output=value` |
| `type` | `string`（默认）、`int`、`number`、`bool`、`choice` |
| `required` | 缺失时：终端里自动打开表单；非终端则报错并打印用法 |
| `default` | 默认值（TUI 表单和 `ctx.args` 里都会体现） |
| `label` / `help` | TUI 表单里的名字与提示 |
| `choices` | `type = "choice"` 时的可选值 |

命令行规则：`--flag`、`--no-flag`、`--key=value`、`-k value`、`--tui` 强制打开界面、
`--help` 打印由 params 生成的用法、`--` 之后的内容原样进入 `ctx.rest`。

`ctx.args` 同时包含：命名参数（按类型转换后的值）与位置参数；
`ctx.rest` 是多余的位置参数数组；`ctx.argv` 是原始命令行数组。

---

## 2. `M.run(ctx)`

返回值：`0`/`nil` 表示成功，数字即退出码，`false` 会被当成 1。

```lua
function M.run(ctx)
  ctx.log("hello %s", ctx.args.name)   -- printf 风格（也接受 print 风格多参数）
  ctx.info/warn/err/ok("…")

  -- 前台运行子进程：TUI 会自动退出备用屏幕让子进程接管终端，跑完按回车返回
  local ok, code = ctx.run({ argv = { "aria2c", "-x", "16", url } })
  -- opts: sudo = true, cwd = "…", shell = true, pause = false, input = "y\n"

  -- 捕获输出
  local out, code2 = ctx.capture("uname -a")

  -- 流式：每行回调（用来画进度条最合适）
  ctx.spawn({
    argv = { "curl", "-o", dest, url },
    on_line = function(line) ctx.log(line) end,
  })

  -- 交互
  if ctx.confirm("确定要删除吗？") then os.remove(file) end
  local v = ctx.ask("输入", "值：", "默认值")
  local i = ctx.select("选择", "选一个", { "a", "b" })
  ctx.message("标题", "一段可以上下滚动的文本")
end
```

`ctx.run` / `ctx.spawn` 传 `sudo = true` 会自动加 `sudo`（没有 sudo 或在 Windows 上就原样执行）。
`spawn` 的返回值是 `ok, code`，带 `capture = true` 时还有第三个返回值 `output`。

底层还有 `gg.exec{argv = {…}}`（替换当前进程，不返回）。

---

## 3. `M.tui(ctx)` —— 自定义界面

```lua
function M.tui(ctx)
  while true do
    local sel = ctx.tui.menu({
      title = M.title,                 -- 顶部标题
      status = "右侧小字",              -- 可选
      items = { "第一项", "第二项 说明" },
      footer = "自定义底部按键提示",     -- 可选
    })
    if sel == nil then return 0 end    -- esc / q
    if sel == 1 then return M.run(ctx) end
  end
end
```

菜单里可以直接打字过滤（`esc` 清空过滤，再按一次退出）。

### 表单

```lua
local f = ctx.tui.form({
  title = "新建下载", subtitle = "tab 切换 · ctrl-s 提交",
  fields = {
    { name = "url",  label = "URL",     kind = "text",   default = "https://" },
    { name = "dir",  label = "目录",     kind = "text",   default = gg.home },
    { name = "mode", label = "模式",     kind = "choice", choices = { "fast", "safe" }, default = "fast" },
    { name = "bg",   label = "后台",     kind = "bool",   default = false },
  },
})
if f.ok then … f.url, f.mode, f.bg … end   -- 取消时 f.ok = false
```

按键：`↑↓`/`tab` 移动、`空格` 切换布尔与枚举、`←→` 切枚举、可打印字符编辑文本、
`backspace` 删除、`ctrl-s` 提交、`esc` 取消。

### 文本框（内置编辑器）

```lua
local text = ctx.tui.textbox({ title = "编辑", filename = "notes.txt", text = old })
if text then gg.write(path, text) end   -- 取消返回 nil
```

### 进度条 / 实时日志

```lua
local bar = ctx.tui.progress({ title = "安装中", subtitle = "apt" })
bar:set(42, "正在解包")        -- 0..100，第二个参数是当前状态行
bar:log("Get:1 http://… 1.2 MB")  -- 追加到滚动日志
bar:done(true, "完成")          -- 必须调用（或让它被 GC 回收）
```

`bar:set(-1)` 显示不确定进度动画。

### 其它

```lua
ctx.tui.available()      -- 是否在真终端里
local w, h = ctx.tui.size()
ctx.tui.message("标题", "内容")   -- 可滚动分页
ctx.tui.confirm("要撤销吗？")     -- 是/否
```

---

## 4. `M.actions` —— 动作菜单

适合「一堆固定子命令」的工具（apt、brew、systemctl、docker…）：gg 会自动渲染菜单，
缺的参数会先弹输入框，`{name}` 占位符用参数值替换。

```lua
M.actions = {
  { name = "update",  desc = "刷新索引",  cmd = { "apt", "update" }, sudo = true },
  { name = "install", desc = "安装",      cmd = { "apt", "install", "-y", "{pkgs}" },
    sudo = true, params = { "pkgs" } },
  { name = "list",    desc = "列出已装",  cmd = "apt list --installed" },  -- 字符串也行（走 shell）
}
M.params = {
  { name = "pkgs", type = "string", label = "包名" },
}
```

字段：`name`（菜单里的名字）、`desc`（说明）、`cmd`（数组 = 直接执行；字符串 = 交给 shell）、
`sudo`、`pause`（跑完是否等回车，默认 true）、`params`（需要临时询问的参数名数组）。

---

## 5. `gg` 完整 API

### 进程

| 函数 | 说明 |
| --- | --- |
| `gg.run(opts)` | 前台运行（TUI 感知、暂停等回车）。返回 `ok, code` |
| `gg.spawn(opts)` | `capture = true` 拿输出；`on_line = function(line)` 实时逐行回调 |
| `gg.capture(cmd)` / `gg.sh(cmd)` | 通过 shell 执行并返回输出（`capture` 还返回退出码） |
| `gg.exec(opts)` | `execvp` 替换当前进程 |

`opts`：`argv`（数组，或单字符串）、`shell`、`sudo`、`cwd`、`input`（喂给 stdin 的文本）、
`echo`、`pause`、`capture`、`on_line`。

### 文件与环境

`gg.exists` `gg.is_dir` `gg.is_file` `gg.mkdir` `gg.read` `gg.write` `gg.list(dir[, full_path])`
`gg.stat`（`size/mtime/is_dir/is_file`）`gg.rm(path[, {recursive=true}])` `gg.copy` `gg.move`
`gg.chmod_x` `gg.mkstemp(suffix)` `gg.join_path` `gg.abs` `gg.basename` `gg.dirname` `gg.ext`
`gg.cwd()` `gg.getenv/setenv` `gg.which(name)` `gg.have(name)` `gg.sleep(秒)` `gg.now()`

### 文本与数据

`gg.trim` `gg.split(s, sep)` `gg.join(t, sep)` `gg.str.quote(s)`（shell 安全引号）
`gg.json.encode(v)` / `gg.json.decode(s)`（空表编码成 `{}`，数组保持数组）
`gg.i18n(en, zh)`（按 `GG_LANG` / `LANG` 选一个）

### 界面与输出

`gg.log/info/warn/err/ok/print`（`log` 支持 `%s` 格式）、`gg.style(name)`、`gg.colorize(text, name)`、
`gg.confirm/ask/select/message`、`gg.tui.*`、`gg.open(路径或网址)`、`gg.download(url[, 目标])`

### 元信息

`gg.version` `gg.url` `gg.exe`（当前二进制的绝对路径）`gg.dir` `gg.home` `gg.modules_dir`
`gg.bin_dir` `gg.config_path` `gg.interactive` `gg.lang` `gg.bundled`（这个文件里有没有归档）
`gg.platform` = `{ os, arch, cpus, root, interactive, lang, home, dir }`

### 自带资源（`gg bundle` 打出来的文件）

`gg bundle app.gg mod.lua assets/` 会把二进制和 zip 粘在一起（见 README 第 5 节）。
归档里的内容通过下面这些函数读取：

| 函数 | 说明 |
| --- | --- |
| `gg.assets.read(name)` | 读 `assets/<name>`（也可以直接写 `modules/x.lua` 这种完整路径）。没有则返回 `nil` |
| `gg.assets.list([前缀])` | 列出归档里由 `gg bundle` 加入的条目（带 `.gg-manifest` 时只列这些） |
| `gg.assets.have(name)` | 是否存在 |
| `gg.assets.dir()` | 需要真实文件路径时用：把整个归档解包到一个临时目录并返回目录路径 |

在 APE 上还可以直接用 Cosmopolitan 的 zipos：`io.open("/zip/assets/logo.txt")`。
宿主构建没有 zipos，所以优先用 `gg.assets.*`（两种构建都工作）。

归档里被 gg 自动识别的东西：

| 归档路径 | 作用 |
| --- | --- |
| `modules/<name>.lua` | 就是模块，`gg <name>` 直接跑；同名时优先级：用户文件 > 归档 > 内置 |
| `assets/<...>` | `gg.assets.read()` 读的数据 |
| `init.lua` | 启动钩子：Lua 环境就绪后执行一次（适合设默认值、注册命令） |
| `registry.tsv` | 预先注册的命令行，格式 `名字<TAB>命令<TAB>描述`（只读，`gg rm` 拒绝删除） |
| `.gg-manifest` | gg 自己写的清单（记录哪些条目是打包进来的），不用手动维护 |

### 注册表与模块

```lua
for _, c in ipairs(gg.registry.list()) do print(c.name, c.cmd) end
gg.registry.add("dl", "aria2c -c -x 16", "快速下载")
gg.registry.remove("dl")
for _, m in ipairs(gg.modules.list()) do print(m.name, m.title, m.builtin) end
print(gg.modules.path("aria2c"))   -- 落盘路径（内置模块也会写出来方便编辑）
gg.modules.install_examples()      -- 把 examples/*.lua 写到 ~/.gg/modules
```

---

## 6. 配置文件 `~/.gg/config.lua`

启动时执行，返回一个表：

```lua
return {
  lang = "zh",      -- 强制界面语言（zh / en）
}
```

`gg config` 会用内置编辑器打开它。

---

## 7. 调试与测试

* `gg <模块> --help` 看自动生成的用法。
* `gg edit <模块>` 保存前会自动做语法检查并定位错误行。
* 看归档里到底有什么：`gg -e 'print(table.concat(gg.assets.list(), "\n"))'`。
* 非交互场景：`GG_PLAIN=1 gg ls --json`、`GG_PLAIN=1 gg mymod arg1`。
  非交互时菜单返回 `nil`（等于取消）、表单返回默认值、`ctx.confirm` 默认返回真 —— 想强制拒绝
  就设 `GG_ASSUME_YES=0`。
* 模块报错会打印带行号的 Lua traceback。
* 开发时用 `tools/ggshot.py` 在伪终端里给 TUI 截图：

  ```sh
  python3 tools/ggshot.py 100 30 'text:aria2c,enter,delay:1' -- ./bin/gg
  ```
