-- gg module: apt — `apt update && apt install foo` with a menu on top.
-- 跨发行版的包管理器封装：apt / dnf / yum / pacman / zypper / brew / winget / choco

local M = {}

M.title = "包管理器 / package manager"
M.desc = "apt update、apt install 这类操作的安全快捷方式，自动识别本机包管理器"

-- 每个包管理器的子命令映射 / sub-command table per package manager
local MANAGERS = {
  { bin = "apt", sudo = true,
    update = { "update" }, upgrade = { "upgrade", "-y" },
    install = { "install", "-y" }, remove = { "remove", "-y" },
    search = { "search" }, clean = { "clean" },
    autoremove = { "autoremove", "-y" }, list = { "list", "--installed" } },
  { bin = "apt-get", sudo = true,
    update = { "update" }, upgrade = { "upgrade", "-y" },
    install = { "install", "-y" }, remove = { "remove", "-y" },
    search = { "search" }, clean = { "clean" },
    autoremove = { "autoremove", "-y" }, list = { "list", "--installed" } },
  { bin = "dnf", sudo = true,
    update = { "makecache" }, upgrade = { "upgrade", "-y" },
    install = { "install", "-y" }, remove = { "remove", "-y" },
    search = { "search" }, clean = { "clean", "all" },
    autoremove = { "autoremove", "-y" }, list = { "list", "installed" } },
  { bin = "yum", sudo = true,
    update = { "makecache" }, upgrade = { "update", "-y" },
    install = { "install", "-y" }, remove = { "remove", "-y" },
    search = { "search" }, clean = { "clean", "all" },
    autoremove = { "autoremove", "-y" }, list = { "list", "installed" } },
  { bin = "pacman", sudo = true,
    update = { "-Sy" }, upgrade = { "-Syu", "--noconfirm" },
    install = { "-S", "--noconfirm" }, remove = { "-Rns", "--noconfirm" },
    search = { "-Ss" }, clean = { "-Sc", "--noconfirm" },
    list = { "-Q" } },
  { bin = "zypper", sudo = true,
    update = { "refresh" }, upgrade = { "update", "-y" },
    install = { "install", "-y" }, remove = { "remove", "-y" },
    search = { "search" }, clean = { "clean", "--all" },
    list = { "search", "--installed-only" } },
  { bin = "brew", sudo = false,
    update = { "update" }, upgrade = { "upgrade" },
    install = { "install" }, remove = { "uninstall" },
    search = { "search" }, clean = { "cleanup" },
    list = { "list" } },
  { bin = "winget", sudo = false,
    update = { "upgrade", "--all", "--accept-package-agreements" },
    upgrade = { "upgrade", "--all", "-h" },
    install = { "install", "-h", "--accept-package-agreements" },
    remove = { "uninstall" }, search = { "search" },
    list = { "list" } },
  { bin = "choco", sudo = false,
    update = { "upgrade", "all", "-y" }, upgrade = { "upgrade", "all", "-y" },
    install = { "install", "-y" }, remove = { "uninstall", "-y" },
    search = { "search" }, clean = { "cache", "remove", "-y" },
    list = { "list", "--local-only" } },
}

local PM = nil
for _, m in ipairs(MANAGERS) do
  if gg.which(m.bin) then PM = m break end
end

local function action(name, desc, key, params, extra)
  if not PM or not PM[key] then return nil end
  local cmd = { PM.bin }
  for _, v in ipairs(PM[key]) do cmd[#cmd + 1] = v end
  if extra then
    for _, v in ipairs(extra) do cmd[#cmd + 1] = v end
  end
  return { name = name, desc = desc, cmd = cmd, sudo = PM.sudo,
           params = params }
end

M.actions = {}
local function add(a) if a then M.actions[#M.actions + 1] = a end end
add(action("update", "刷新软件源索引 / refresh the index", "update"))
add(action("upgrade", "升级所有已安装的包 / upgrade everything", "upgrade"))
add(action("install", "安装软件包 / install packages", "install", { "pkgs" }))
add(action("remove", "卸载软件包 / remove packages", "remove", { "pkgs" }))
add(action("search", "搜索软件包 / search", "search", { "pkgs" }))
add(action("list", "列出已安装 / list installed", "list"))
add(action("clean", "清理缓存 / clean the cache", "clean"))
add(action("autoremove", "清理无用依赖 / autoremove", "autoremove"))
if PM and PM.bin == "apt" then
  add({ name = "who-owns", desc = "哪个包提供了这个文件 / which package owns a file",
        cmd = { "dpkg", "-S", "{file}" }, sudo = false, params = { "file" } })
end

M.params = {
  { name = "pkgs", pos = 1, type = "string", label = "软件包 / packages",
    help = "空格分隔的包名 / space separated package names" },
}

M.run = function(ctx)
  if not PM then
    ctx.err("没有找到包管理器 / no package manager found")
    return 1
  end
  ctx.log("使用 %s / using %s", PM.bin, PM.bin)
  ctx.log("直接运行 `gg apt` 可以打开动作菜单 / run `gg apt` for the menu")
  return 0
end

return M
