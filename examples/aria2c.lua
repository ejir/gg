-- gg module: aria2c — turn a long aria2c incantation into `gg aria2c <url>`.
--
--   aria2c.exe -c -x 16 -s 16 -k 10M --disk-cache=128M \
--            --check-certificate=false <url>
--
-- becomes `gg aria2c <url>` with a TUI on top when you run it bare.

local M = {}

M.title = "aria2c 下载助手 / aria2c downloader"
M.desc  = "带推荐参数的 aria2c 封装，支持多链接、断点续传和实时进度"

-- 推荐的默认参数，改这里即可 / tweak the defaults here
M.defaults = {
  jobs     = 16,
  split    = 16,
  chunk    = "10M",
  cache    = "128M",
  insecure = true,
  continue = true,
  dir      = nil,
  out      = nil,
}

M.params = {
  { name = "url", pos = 1, type = "string", required = true,
    label = "下载地址 / URL", help = "可以给多个链接 / several urls are allowed" },
  { name = "dir", short = "d", type = "string",
    label = "保存目录 / directory", help = "aria2c -d" },
  { name = "out", short = "o", type = "string",
    label = "文件名 / file name", help = "aria2c -o" },
  { name = "jobs", short = "x", type = "int", default = M.defaults.jobs,
    label = "连接数 / connections", help = "-x / -s" },
  { name = "chunk", short = "k", type = "string", default = M.defaults.chunk,
    label = "分片大小 / chunk", help = "aria2c -k" },
  { name = "cache", type = "string", default = M.defaults.cache,
    label = "磁盘缓存 / disk cache", help = "--disk-cache" },
  { name = "insecure", type = "bool", default = M.defaults.insecure,
    label = "忽略证书 / ignore certificates", help = "--check-certificate=false" },
  { name = "resume", short = "c", type = "bool", default = true,
    label = "断点续传 / continue", help = "aria2c -c" },
  { name = "seed", short = "s", type = "bool", default = false,
    label = "做种 / seed after download", help = "--seed-time" },
}

local installed_command

local function find_aria2()
  return installed_command or gg.which("aria2c") or gg.which("aria2c.exe")
end

local INSTALLERS = {
  { bin = "apt-get", label = "apt", sudo = true,
    argv = { "apt-get", "install", "-y", "aria2" } },
  { bin = "dnf", label = "dnf", sudo = true,
    argv = { "dnf", "install", "-y", "aria2" } },
  { bin = "yum", label = "yum", sudo = true,
    argv = { "yum", "install", "-y", "aria2" } },
  { bin = "pacman", label = "pacman", sudo = true,
    argv = { "pacman", "-S", "--noconfirm", "aria2" } },
  { bin = "zypper", label = "zypper", sudo = true,
    argv = { "zypper", "--non-interactive", "install", "aria2" } },
  { bin = "apk", label = "apk", sudo = true,
    argv = { "apk", "add", "aria2" } },
  { bin = "pkg", label = "pkg", sudo = true,
    argv = { "pkg", "install", "-y", "aria2" } },
  { bin = "pkgin", label = "pkgin", sudo = true,
    argv = { "pkgin", "-y", "install", "aria2" } },
  { bin = "pkg_add", label = "pkg_add", sudo = true,
    argv = { "pkg_add", "aria2" } },
  { bin = "brew", label = "Homebrew", sudo = false,
    argv = { "brew", "install", "aria2" } },
  { bin = "winget", label = "winget", sudo = false,
    argv = { "winget", "install", "--id", "aria2.aria2", "--exact",
             "--accept-package-agreements", "--accept-source-agreements" } },
  { bin = "choco", label = "Chocolatey", sudo = false,
    argv = { "choco", "install", "aria2", "-y" } },
  { bin = "scoop", label = "Scoop", sudo = false,
    argv = { "scoop", "install", "aria2" } },
}

local function ensure_aria(ctx)
  local bin = find_aria2()
  if bin then return bin end

  local installer
  for _, candidate in ipairs(INSTALLERS) do
    if gg.which(candidate.bin) then installer = candidate break end
  end
  if not installer then
    ctx.err("aria2c 未安装，且未找到受支持的包管理器 / aria2c is missing and no supported package manager was found")
    ctx.log("Debian/Ubuntu: sudo apt install aria2")
    ctx.log("macOS: brew install aria2")
    ctx.log("Windows: winget install aria2.aria2")
    return nil
  end

  local question = gg.i18n(
      "aria2c is not installed. Install it now using " .. installer.label .. "?",
      "aria2c 尚未安装。现在使用 " .. installer.label .. " 安装吗？")
  if not ctx.confirm(question, 0) then
    ctx.warn("已跳过安装 / installation skipped; install aria2 manually to continue")
    return nil
  end

  ctx.log("正在安装 aria2 / installing aria2 with %s", installer.label)
  local ok, code = ctx.run({ argv = installer.argv, sudo = installer.sudo })
  if not ok then
    ctx.err("安装 aria2 失败 / aria2 installation failed (exit %s)", tostring(code))
    return nil
  end
  gg.refresh_tools()
  bin = find_aria2()
  if not bin then
    -- Some package managers update PATH or command shims only for new shells.
    bin = gg.platform.os == "windows" and "aria2c.exe" or "aria2c"
    installed_command = bin
  end
  return bin
end

function M.argv(ctx)
  local a = ctx.args
  local bin = ensure_aria(ctx)
  if not bin then return nil, "aria2c" end
  local argv = { bin }
  if a.resume ~= false then argv[#argv + 1] = "-c" end
  argv[#argv + 1] = "-x"; argv[#argv + 1] = tostring(a.jobs or 16)
  argv[#argv + 1] = "-s"; argv[#argv + 1] = tostring(a.jobs or 16)
  argv[#argv + 1] = "-k"; argv[#argv + 1] = a.chunk or "10M"
  argv[#argv + 1] = "--disk-cache=" .. (a.cache or "128M")
  argv[#argv + 1] = "--file-allocation=none"
  argv[#argv + 1] = "--summary-interval=1"
  if a.insecure then argv[#argv + 1] = "--check-certificate=false" end
  if a.seed then argv[#argv + 1] = "--seed-time=60" end
  if a.dir and a.dir ~= "" then argv[#argv + 1] = "-d"; argv[#argv + 1] = a.dir end
  if a.out and a.out ~= "" then argv[#argv + 1] = "-o"; argv[#argv + 1] = a.out end
  argv[#argv + 1] = a.url
  for _, extra in ipairs(ctx.rest) do argv[#argv + 1] = extra end
  return argv
end

function M.pretty(ctx)
  local argv, missing = M.argv(ctx)
  if not argv then return nil, missing end
  local parts = {}
  for _, v in ipairs(argv) do parts[#parts + 1] = gg.str.quote(v) end
  return table.concat(parts, " ")
end

function M.run(ctx)
  local argv, missing = M.argv(ctx)
  if not argv then
    ctx.err("%s 未安装 / is not installed", missing)
    ctx.log("安装方式 / install it:")
    ctx.log("  debian/ubuntu:  sudo apt install aria2")
    ctx.log("  macos:          brew install aria2")
    ctx.log("  windows:        winget install aria2.aria2")
    return 1
  end
  local ok, code = ctx.run({ argv = argv })
  if ok then
    ctx.ok("下载完成 / download finished")
  else
    ctx.err("aria2c 退出码 %s / exit code %s", tostring(code), tostring(code))
  end
  return ok and 0 or code
end

function M.tui(ctx)
  if not ensure_aria(ctx) then return 1 end
  while true do
    local sel = ctx.tui.menu({
      title = M.title,
      status = find_aria2() and "" or "aria2c missing!",
      items = {
        "新建下载 / new download",
        "把这套参数注册成命令 / register as a command",
        "打印完整命令 / print the command",
        "帮助 / help",
      },
    })
    if sel == 2 then
      -- 把推荐参数固化成一个普通命令，之后 `gg dl <url>` 就行
      local cmd = nil
      if find_aria2() then
        cmd = table.concat({
          "aria2c -c -x " .. tostring(M.defaults.jobs),
          "-s " .. tostring(M.defaults.split),
          "-k " .. tostring(M.defaults.chunk),
          "--disk-cache=" .. tostring(M.defaults.cache),
          tostring(M.defaults.insecure and "--check-certificate=false" or ""),
        }, " ")
      end
      if cmd then
        local name = ctx.ask("注册命令", "命令名 / name: ", "dl")
        if name then
          gg.registry.add(name, cmd, "aria2c 快速下载 / quick aria2c download (附加参数会原样传给 aria2c)")
          ctx.ok("已注册 / registered: gg " .. name)
        end
      else
        ctx.err("需要先安装 aria2c / install aria2c first")
      end
    elseif sel == 3 then
      local line = M.pretty(ctx)
      ctx.tui.message("命令 / command", line or "aria2c not found")
    elseif sel == 4 then
      ctx.tui.message(M.title, table.concat({
        "推荐参数 / recommended flags:",
        "  -c              断点续传 / continue",
        "  -x 16 -s 16     16 个连接 / 16 connections",
        "  -k 10M          10 MB 分片 / chunks",
        "  --disk-cache=128M",
        "  --check-certificate=false",
        "",
        "可以直接给多个 URL，多余的参数原样传给 aria2c。",
        "Extra arguments are forwarded to aria2c as-is.",
      }, "\n"))
    elseif sel == 1 or sel == nil then
      if sel == nil then return 0 end
      -- form: 用 TUI 收集参数
      local f = ctx.tui.form({
        title = "新建下载 / new download",
        subtitle = "tab 下一个字段 · ctrl-s 开始下载",
        fields = {
          { name = "url", label = "URL", kind = "text", default = ctx.args.url or "" },
          { name = "dir", label = "目录 / dir", kind = "text",
            default = ctx.args.dir or (gg.getenv("HOME") .. "/Downloads") },
          { name = "insecure", label = "忽略证书 / insecure", kind = "bool", default = true },
        },
      })
      if not f.ok or f.url == "" then return 0 end
      ctx.args.url = f.url
      ctx.args.dir = f.dir
      ctx.args.insecure = f.insecure
      local argv = M.argv(ctx)
      if not argv then
        ctx.err("aria2c 未安装 / not installed")
      else
        local ok = ctx.run({ argv = argv })
        if ok then ctx.ok("完成 / done") end
      end
      return 0
    end
  end
end

return M
