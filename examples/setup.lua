-- gg module: setup — one cross-distro entry point for packages, mirrors and dev tools.
-- Detects the native package manager, tests repository mirrors before offering a
-- switch, backs up every system file before editing it, and installs nvm/uv only
-- after an explicit confirmation.

local M = {}

M.title = gg.i18n("System setup", "系统工具箱")
M.desc = gg.i18n(
  "Cross-distro package manager, repository mirror switching, and nvm/uv setup.",
  "统一识别 apt/dnf/yum 等包管理器，支持软件源测速换源及 nvm/uv 安装")

local function tr(en, zh) return gg.i18n(en, zh) end
local function lower(s) return (tostring(s or ""):lower()) end

local function read_os_release()
  local info = {}
  local text = gg.read("/etc/os-release") or ""
  for line in text:gmatch("[^\r\n]+") do
    local key, value = line:match("^([%w_]+)=(.*)$")
    if key then
      value = gg.trim(value)
      value = value:gsub('^"(.*)"$', "%1"):gsub("^'(.*)'$", "%1")
      info[lower(key)] = value
    end
  end
  info.id = lower(info.id)
  info.id_like = lower(info.id_like)
  info.name = info.name or info.id or gg.platform.os or "unknown"
  info.version_id = info.version_id or ""
  info.codename = info.version_codename or info.ubuntu_codename or ""
  return info
end

local OS = read_os_release()

local function has_word(value, target)
  for word in lower(value):gmatch("[%w._-]+") do
    if word == target then return true end
  end
  return false
end

local function has_any_id(values)
  for _, value in ipairs(values) do
    if has_word(OS.id, value) or has_word(OS.id_like, value) then return true end
  end
  return false
end

local function manager_for(bin)
  if not gg.which(bin) then return nil end
  local m = { bin = bin, label = bin, commands = {} }
  local function set(action, command, args, sudo, needs_packages)
    m.commands[action] = {
      bin = command or bin, args = args or {}, sudo = sudo or false,
      needs_packages = needs_packages or false,
    }
  end

  if bin == "apt-get" or bin == "apt" then
    m.label = "apt"
    set("update", bin, { "update" }, true)
    if bin == "apt-get" then
      set("upgrade", bin, { "upgrade", "-y" }, true)
      set("install", bin, { "install", "-y" }, true, true)
      set("remove", bin, { "remove", "-y" }, true, true)
      set("search", gg.which("apt-cache") and "apt-cache" or bin, { "search" }, false, true)
      set("list", gg.which("dpkg-query") and "dpkg-query" or bin,
          gg.which("dpkg-query") and { "-W" } or { "list", "--installed" }, false)
      set("clean", bin, { "clean" }, true)
      set("autoremove", bin, { "autoremove", "-y" }, true)
    else
      set("upgrade", bin, { "upgrade", "-y" }, true)
      set("install", bin, { "install", "-y" }, true, true)
      set("remove", bin, { "remove", "-y" }, true, true)
      set("search", bin, { "search" }, false, true)
      set("list", bin, { "list", "--installed" }, false)
      set("clean", bin, { "clean" }, true)
      set("autoremove", bin, { "autoremove", "-y" }, true)
    end
  elseif bin == "dnf" then
    m.label = "dnf"
    set("update", bin, { "makecache" }, true)
    set("upgrade", bin, { "upgrade", "-y" }, true)
    set("install", bin, { "install", "-y" }, true, true)
    set("remove", bin, { "remove", "-y" }, true, true)
    set("search", bin, { "search" }, false, true)
    set("list", bin, { "list", "installed" }, false)
    set("clean", bin, { "clean", "all" }, true)
    set("autoremove", bin, { "autoremove", "-y" }, true)
  elseif bin == "yum" then
    m.label = "yum"
    set("update", bin, { "makecache" }, true)
    set("upgrade", bin, { "update", "-y" }, true)
    set("install", bin, { "install", "-y" }, true, true)
    set("remove", bin, { "remove", "-y" }, true, true)
    set("search", bin, { "search" }, false, true)
    set("list", bin, { "list", "installed" }, false)
    set("clean", bin, { "clean", "all" }, true)
    set("autoremove", bin, { "autoremove", "-y" }, true)
  elseif bin == "pacman" then
    m.label = "pacman"
    set("update", bin, { "-Sy" }, true)
    set("upgrade", bin, { "-Syu", "--noconfirm" }, true)
    set("install", bin, { "-S", "--needed", "--noconfirm" }, true, true)
    set("remove", bin, { "-Rns", "--noconfirm" }, true, true)
    set("search", bin, { "-Ss" }, false, true)
    set("list", bin, { "-Q" }, false)
    set("clean", bin, { "-Sc", "--noconfirm" }, true)
  elseif bin == "zypper" then
    m.label = "zypper"
    set("update", bin, { "refresh" }, true)
    set("upgrade", bin, { "update", "-y" }, true)
    set("install", bin, { "install", "-y" }, true, true)
    set("remove", bin, { "remove", "-y" }, true, true)
    set("search", bin, { "search" }, false, true)
    set("list", bin, { "search", "--installed-only" }, false)
    set("clean", bin, { "clean", "--all" }, true)
  elseif bin == "apk" then
    m.label = "apk"
    set("update", bin, { "update" }, true)
    set("upgrade", bin, { "upgrade" }, true)
    set("install", bin, { "add" }, true, true)
    set("remove", bin, { "del" }, true, true)
    set("search", bin, { "search" }, false, true)
    set("list", bin, { "info" }, false)
    set("clean", bin, { "cache", "clean" }, true)
  elseif bin == "pkg" then
    m.label = "pkg"
    set("update", bin, { "update" }, true)
    set("upgrade", bin, { "upgrade", "-y" }, true)
    set("install", bin, { "install", "-y" }, true, true)
    set("remove", bin, { "delete", "-y" }, true, true)
    set("search", bin, { "search" }, false, true)
    set("list", bin, { "info" }, false)
    set("clean", bin, { "clean" }, true)
  elseif bin == "pkgin" then
    m.label = "pkgin"
    set("update", bin, { "update" }, true)
    set("upgrade", bin, { "full-upgrade", "-y" }, true)
    set("install", bin, { "install", "-y" }, true, true)
    set("remove", bin, { "remove", "-y" }, true, true)
    set("search", bin, { "search" }, false, true)
    set("list", bin, { "list" }, false)
    set("clean", bin, { "clean" }, true)
  elseif bin == "brew" then
    m.label = "Homebrew"
    set("update", bin, { "update" }, false)
    set("upgrade", bin, { "upgrade" }, false)
    set("install", bin, { "install" }, false, true)
    set("remove", bin, { "uninstall" }, false, true)
    set("search", bin, { "search" }, false, true)
    set("list", bin, { "list" }, false)
    set("clean", bin, { "cleanup" }, false)
  elseif bin == "winget" then
    m.label = "winget"
    set("update", bin, { "upgrade", "--all", "--accept-package-agreements", "--accept-source-agreements" }, false)
    set("upgrade", bin, { "upgrade", "--all", "--accept-package-agreements", "--accept-source-agreements" }, false)
    set("install", bin, { "install", "--exact", "--accept-package-agreements", "--accept-source-agreements" }, false, true)
    set("remove", bin, { "uninstall", "--exact" }, false, true)
    set("search", bin, { "search" }, false, true)
    set("list", bin, { "list" }, false)
  elseif bin == "choco" then
    m.label = "Chocolatey"
    set("update", bin, { "upgrade", "all", "-y" }, false)
    set("upgrade", bin, { "upgrade", "all", "-y" }, false)
    set("install", bin, { "install", "-y" }, false, true)
    set("remove", bin, { "uninstall", "-y" }, false, true)
    set("search", bin, { "search" }, false, true)
    set("list", bin, { "list", "--limit-output" }, false)
    set("clean", bin, { "cache", "remove", "-y" }, false)
  elseif bin == "scoop" then
    m.label = "Scoop"
    set("update", bin, { "update" }, false)
    set("upgrade", bin, { "update", "*" }, false)
    set("install", bin, { "install" }, false, true)
    set("remove", bin, { "uninstall" }, false, true)
    set("search", bin, { "search" }, false, true)
    set("list", bin, { "list" }, false)
    set("clean", bin, { "cache", "rm", "*" }, false)
  else
    return nil
  end
  return m
end

local function choose_package_manager()
  local osname = lower(gg.platform.os)
  local preferred = {}
  if osname == "windows" then
    preferred = { "winget", "choco", "scoop" }
  elseif osname == "darwin" then
    preferred = { "brew" }
  elseif has_any_id({ "ubuntu", "debian", "linuxmint", "pop", "kali", "raspbian" }) then
    preferred = { "apt-get", "apt" }
  elseif has_any_id({ "fedora", "rhel", "centos", "rocky", "almalinux", "ol", "oracle" }) then
    preferred = { "dnf", "yum" }
  elseif has_any_id({ "arch", "manjaro", "endeavouros" }) then
    preferred = { "pacman" }
  elseif has_any_id({ "alpine" }) then
    preferred = { "apk" }
  elseif has_any_id({ "opensuse", "suse", "sles" }) then
    preferred = { "zypper" }
  elseif osname == "freebsd" then
    preferred = { "pkg" }
  elseif osname == "netbsd" then
    preferred = { "pkgin" }
  else
    preferred = { "apt-get", "apt", "dnf", "yum", "pacman", "zypper", "apk", "pkg", "pkgin", "brew", "winget", "choco", "scoop" }
  end
  for _, bin in ipairs(preferred) do
    local m = manager_for(bin)
    if m then return m end
  end
  -- If the OS ID is unusual (or a minimal image), select any installed manager.
  local fallback = { "apt-get", "apt", "dnf", "yum", "pacman", "zypper", "apk", "pkg", "pkgin", "brew", "winget", "choco", "scoop" }
  for _, bin in ipairs(fallback) do
    local m = manager_for(bin)
    if m then return m end
  end
  return nil
end

local PM = choose_package_manager()

local PACKAGE_ACTIONS = {
  { "update", "Refresh package indexes / 刷新软件源索引" },
  { "upgrade", "Upgrade installed packages / 升级已安装软件包" },
  { "install", "Install packages / 安装软件包" },
  { "remove", "Remove packages / 卸载软件包" },
  { "search", "Search packages / 搜索软件包" },
  { "list", "List installed packages / 列出已安装软件包" },
  { "clean", "Clean package cache / 清理软件缓存" },
  { "autoremove", "Remove unused dependencies / 清理无用依赖" },
}

local function package_tokens(ctx, extra)
  local values = {}
  local function add_words(s)
    for word in tostring(s or ""):gmatch("%S+") do values[#values + 1] = word end
  end
  add_words(ctx.args.package)
  for _, word in ipairs(ctx.rest or {}) do add_words(word) end
  if extra then add_words(extra) end
  return values
end

local function run_package_action(ctx, action, tokens)
  if not PM then
    ctx.err(tr("No supported package manager was found.", "没有找到受支持的包管理器。"))
    return 1
  end
  local command = PM.commands[action]
  if not command then
    ctx.err(tr("%s is not supported by %s.", "%s 不支持在 %s 上执行。"), action, PM.label)
    return 1
  end
  tokens = tokens or {}
  if command.needs_packages and #tokens == 0 then
    if gg.interactive then
      local input = ctx.ask(tr("Package names", "软件包名称"),
                            tr("Separate multiple names with spaces: ", "多个名称用空格分开："), "")
      if input then tokens = gg.split(input) end
    end
  end
  if command.needs_packages and #tokens == 0 then
    ctx.err(tr("Package name is required. Example: gg setup install git curl",
               "需要提供软件包名，例如：gg setup install git curl"))
    return 2
  end
  local argv = { command.bin }
  for _, value in ipairs(command.args) do argv[#argv + 1] = value end
  for _, value in ipairs(tokens) do
    if value:sub(1, 1) == "-" or value:find("[%c]") then
      ctx.err(tr("Invalid package name: %s", "软件包名称无效：%s"), value)
      return 2
    end
    argv[#argv + 1] = value
  end
  ctx.log(tr("Using %s for %s.", "使用 %s 执行 %s。"), PM.label, action)
  if command.sudo and not gg.platform.root and not gg.which("sudo") then
    ctx.err(tr("This action needs administrator privileges; install sudo or run as root.",
               "此操作需要管理员权限；请安装 sudo 或以 root 身份运行。"))
    return 1
  end
  local ok, code = ctx.run({ argv = argv, sudo = command.sudo, pause = true })
  if ok then
    ctx.ok(tr("%s completed.", "%s 已完成。"), action)
    return 0
  end
  ctx.err(tr("%s failed (exit %s).", "%s 失败（退出码 %s）。"), action, tostring(code))
  return code or 1
end

local APT_MIRROR_HOSTS = {
  ["archive.ubuntu.com"] = true,
  ["security.ubuntu.com"] = true,
  ["ports.ubuntu.com"] = true,
  ["deb.debian.org"] = true,
  ["security.debian.org"] = true,
  ["ftp.debian.org"] = true,
  ["mirrors.tuna.tsinghua.edu.cn"] = true,
  ["mirrors.ustc.edu.cn"] = true,
  ["mirrors.aliyun.com"] = true,
}

local function apt_family()
  if has_any_id({ "ubuntu", "linuxmint", "pop", "kali" }) then return "ubuntu" end
  if has_any_id({ "debian", "raspbian" }) then return "debian" end
  return nil
end

local function apt_mirror_candidates(family)
  local codename = OS.codename
  if codename == "" then
    if family == "debian" then codename = "stable"
    else codename = OS.version_id end
  end
  if codename == "" then return nil end
  local arch = lower(gg.platform.arch)
  local ports = family == "ubuntu" and arch ~= "x86_64" and arch ~= "amd64" and arch ~= "i386"
  local root = family == "debian" and "debian" or (ports and "ubuntu-ports" or "ubuntu")
  local path = "/" .. root .. "/dists/" .. codename .. "/InRelease"
  local items
  if family == "debian" then
    items = {
      { key = "tuna", label = "Tsinghua TUNA", host = "mirrors.tuna.tsinghua.edu.cn", probe = "https://mirrors.tuna.tsinghua.edu.cn" .. path },
      { key = "ustc", label = "USTC", host = "mirrors.ustc.edu.cn", probe = "https://mirrors.ustc.edu.cn" .. path },
      { key = "aliyun", label = "Aliyun", host = "mirrors.aliyun.com", probe = "https://mirrors.aliyun.com" .. path },
      { key = "official", label = "Debian official", host = "deb.debian.org", probe = "https://deb.debian.org/debian/dists/" .. codename .. "/InRelease" },
    }
  elseif ports then
    items = {
      { key = "tuna", label = "Tsinghua TUNA", host = "mirrors.tuna.tsinghua.edu.cn", probe = "https://mirrors.tuna.tsinghua.edu.cn" .. path },
      { key = "ustc", label = "USTC", host = "mirrors.ustc.edu.cn", probe = "https://mirrors.ustc.edu.cn" .. path },
      { key = "official", label = "Ubuntu Ports official", host = "ports.ubuntu.com", probe = "https://ports.ubuntu.com/ubuntu-ports/dists/" .. codename .. "/InRelease" },
    }
  else
    items = {
      { key = "tuna", label = "Tsinghua TUNA", host = "mirrors.tuna.tsinghua.edu.cn", probe = "https://mirrors.tuna.tsinghua.edu.cn" .. path },
      { key = "ustc", label = "USTC", host = "mirrors.ustc.edu.cn", probe = "https://mirrors.ustc.edu.cn" .. path },
      { key = "aliyun", label = "Aliyun", host = "mirrors.aliyun.com", probe = "https://mirrors.aliyun.com" .. path },
      { key = "official", label = "Ubuntu official", host = "archive.ubuntu.com", probe = "https://archive.ubuntu.com/ubuntu/dists/" .. codename .. "/InRelease" },
    }
  end
  return items
end

local RPM_REPO_SEGMENTS = {
  baseos = "BaseOS", appstream = "AppStream", extras = "extras", crb = "CRB",
  powertools = "PowerTools", ha = "HighAvailability", highavailability = "HighAvailability",
  resilientstorage = "ResilientStorage", nfv = "NFV", plus = "plus", devel = "Devel",
}

local function rpm_spec()
  local id = OS.id
  local osname = lower(gg.platform.os)
  local major = OS.version_id:match("^(%d+)") or ""
  local arch = lower(gg.platform.arch)
  if arch == "amd64" then arch = "x86_64" end
  if arch == "arm64" then arch = "aarch64" end
  if major == "" or arch == "" then return nil end
  if id == "fedora" then
    return { id = "fedora", major = major, arch = arch,
      roots = {
        { key = "tuna", label = "Tsinghua TUNA", root = "https://mirrors.tuna.tsinghua.edu.cn/fedora" },
        { key = "ustc", label = "USTC", root = "https://mirrors.ustc.edu.cn/fedora" },
        { key = "official", label = "Fedora official", root = "https://download.fedoraproject.org/pub/fedora/linux" },
      } }
  elseif id == "rocky" then
    return { id = "rocky", major = major, arch = arch,
      roots = {
        { key = "ustc", label = "USTC", root = "https://mirrors.ustc.edu.cn/rocky" },
        { key = "official", label = "Rocky official", root = "https://dl.rockylinux.org/pub/rocky" },
      } }
  elseif id == "almalinux" then
    return { id = "almalinux", major = major, arch = arch,
      roots = {
        { key = "ustc", label = "USTC", root = "https://mirrors.ustc.edu.cn/almalinux" },
        { key = "official", label = "AlmaLinux official", root = "https://repo.almalinux.org/almalinux" },
      } }
  elseif id == "centos" and (OS.pretty_name or ""):lower():find("stream", 1, true) then
    return { id = "centos-stream", major = major, arch = arch,
      roots = {
        { key = "tuna", label = "Tsinghua TUNA", root = "https://mirrors.tuna.tsinghua.edu.cn/centos-stream" },
        { key = "official", label = "CentOS Stream official", root = "https://mirror.stream.centos.org" },
      } }
  end
  if osname == "linux" and has_any_id({ "rhel", "ol", "oracle" }) then
    return nil -- Subscription-managed repositories are intentionally not rewritten.
  end
  return nil
end

local function rpm_repo_url(spec, repo_id, root)
  local id = lower(repo_id)
  if spec.id == "fedora" then
    if id == "fedora" then return root .. "/releases/$releasever/Everything/$basearch/os/" end
    if id == "updates" then return root .. "/updates/$releasever/Everything/$basearch/" end
    return nil
  end
  if spec.id == "centos-stream" and id ~= "baseos" and id ~= "appstream" and id ~= "crb" then
    return nil
  end
  local segment = RPM_REPO_SEGMENTS[id]
  if not segment then return nil end
  if spec.id == "rocky" then
    return root .. "/$releasever/" .. segment .. "/$basearch/os/"
  elseif spec.id == "almalinux" then
    return root .. "/$releasever/" .. segment .. "/$basearch/os/"
  elseif spec.id == "centos-stream" then
    return root .. "/$releasever-stream/" .. segment .. "/$basearch/os/"
  end
  return nil
end

local function rpm_probe_url(spec, root)
  local base
  if spec.id == "fedora" then
    base = root .. "/releases/" .. spec.major .. "/Everything/" .. spec.arch .. "/os/"
  elseif spec.id == "centos-stream" then
    base = root .. "/" .. spec.major .. "-stream/BaseOS/" .. spec.arch .. "/os/"
  else
    base = root .. "/" .. spec.major .. "/BaseOS/" .. spec.arch .. "/os/"
  end
  return base .. "repodata/repomd.xml"
end

local function mirror_candidates()
  local apt = apt_family()
  if apt then return apt_mirror_candidates(apt), "apt", apt end
  local spec = rpm_spec()
  if spec then
    local result = {}
    for _, root in ipairs(spec.roots) do
      local item = {}
      for k, v in pairs(root) do item[k] = v end
      item.probe = rpm_probe_url(spec, root.root)
      item.root = root.root
      result[#result + 1] = item
    end
    return result, "rpm", spec
  end
  return nil
end

local function test_mirror(item)
  local curl = gg.which("curl")
  if not curl then return nil, "curl is required" end
  local ok, code, output = gg.spawn({
    argv = { curl, "--location", "--fail", "--silent", "--show-error",
      "--connect-timeout", "3", "--max-time", "6", "--output", "/dev/null",
      "--write-out", "GG_RESULT:%{http_code}:%{time_total}:%{speed_download}", item.probe },
    capture = true, echo = false,
  })
  local status, elapsed, speed = (output or ""):match("GG_RESULT:(%d+):([%d%.]+):([%d%.]+)")
  if not ok or code ~= 0 or status ~= "200" then
    return nil, (output or ""):match("GG_RESULT:(%d+)") or tostring(code or "failed")
  end
  return { elapsed = tonumber(elapsed) or 0, bytes_per_sec = tonumber(speed) or 0 }
end

local function format_speed(bytes_per_sec)
  if bytes_per_sec >= 1024 * 1024 then return string.format("%.2f MB/s", bytes_per_sec / (1024 * 1024)) end
  if bytes_per_sec >= 1024 then return string.format("%.0f KB/s", bytes_per_sec / 1024) end
  return string.format("%.0f B/s", bytes_per_sec)
end

local function apt_source_paths()
  local result = {}
  local main = "/etc/apt/sources.list"
  if gg.is_file(main) then result[#result + 1] = main end
  local dir = "/etc/apt/sources.list.d"
  if gg.is_dir(dir) then
    for _, path in ipairs(gg.list(dir, true)) do
      local base = gg.basename(path)
      if gg.is_file(path) and (base:match("%.list$") or base:match("%.sources$")) then
        result[#result + 1] = path
      end
    end
  end
  return result
end

local function escape_pattern(s)
  return (s:gsub("([%^%$%(%)%%%.%[%]%*%+%-%?])", "%%%1"))
end

local function replace_apt_hosts(line, new_host)
  if line:match("^%s*#") then return line end
  local changed = false
  local text = line
  for old_host in pairs(APT_MIRROR_HOSTS) do
    local pattern = "(https?://)" .. escape_pattern(old_host) .. "([^%w%.%-])"
    text = text:gsub(pattern, function(scheme, tail)
      changed = true
      return scheme .. new_host .. tail
    end)
  end
  return text, changed
end

local function make_apt_changes(candidate)
  local paths = apt_source_paths()
  local changes = {}
  for _, path in ipairs(paths) do
    local old = gg.read(path)
    if old then
      local any = false
      local ended = old:sub(-1) == "\n"
      local out = {}
      for line in (old .. "\n"):gmatch("(.-)\n") do
        local replaced, changed = replace_apt_hosts(line:gsub("\r$", ""), candidate.host)
        out[#out + 1] = replaced
        if changed then any = true end
      end
      local new = table.concat(out, "\n")
      if not ended then new = new:gsub("\n$", "") end
      if any and new ~= old then changes[#changes + 1] = { path = path, old = old, new = new } end
    end
  end
  return changes
end

local function split_file_lines(text)
  local lines = {}
  local position = 1
  while position <= #text do
    local newline = text:find("\n", position, true)
    if newline then
      lines[#lines + 1] = text:sub(position, newline - 1)
      position = newline + 1
    else
      lines[#lines + 1] = text:sub(position)
      position = #text + 1
    end
  end
  return lines, text:sub(-1) == "\n"
end

local function known_rpm_source(spec, block)
  local hosts = {
    fedora = { "fedoraproject.org", "download.example", "mirrors.tuna.tsinghua.edu.cn", "mirrors.ustc.edu.cn" },
    rocky = { "rockylinux.org", "mirrors.ustc.edu.cn", "mirrors.tuna.tsinghua.edu.cn" },
    almalinux = { "almalinux.org", "mirrors.ustc.edu.cn", "mirrors.tuna.tsinghua.edu.cn" },
    ["centos-stream"] = { "centos.org", "mirrors.tuna.tsinghua.edu.cn" },
  }
  for _, line in ipairs(block) do
    local trimmed = gg.trim(line)
    local uncommented = trimmed:gsub("^#%s*", "")
    local key, value = uncommented:match("^([%w_]+)%s*=%s*(.-)%s*$")
    key = lower(key)
    if key == "baseurl" or key == "mirrorlist" or key == "metalink" then
      value = lower(value)
      for _, host in ipairs(hosts[spec.id] or {}) do
        if value:find(host, 1, true) then return true end
      end
    end
  end
  return false
end

local function rewrite_rpm_file(text, spec, root)
  local lines, had_trailing_newline = split_file_lines(text)
  local blocks, current = {}, {}
  for _, line in ipairs(lines) do
    if line:match("^%s*%[[^%]]+%]%s*$") and #current > 0 then
      blocks[#blocks + 1] = current
      current = {}
    end
    current[#current + 1] = line
  end
  if #current > 0 then blocks[#blocks + 1] = current end

  local changed = false
  local result = {}
  for _, block in ipairs(blocks) do
    local repo_id = block[1] and block[1]:match("^%s*%[([^%]]+)%]")
    local baseurl = repo_id and rpm_repo_url(spec, repo_id, root) or nil
    if baseurl and known_rpm_source(spec, block) then
      local replaced_baseurl = false
      local new_block = {}
      for _, line in ipairs(block) do
        local trimmed = gg.trim(line)
        local uncommented = trimmed:gsub("^#%s*", "")
        local key = uncommented:match("^([%w_]+)%s*=")
        key = lower(key)
        if key == "baseurl" then
          if not replaced_baseurl then
            new_block[#new_block + 1] = "baseurl=" .. baseurl
            replaced_baseurl = true
          end
          changed = true
        elseif key == "metalink" or key == "mirrorlist" then
          if not trimmed:match("^#") then
            new_block[#new_block + 1] = "# gg setup disabled: " .. trimmed
            changed = true
          else
            new_block[#new_block + 1] = line
          end
        else
          new_block[#new_block + 1] = line
        end
      end
      if not replaced_baseurl then
        table.insert(new_block, 2, "baseurl=" .. baseurl)
        changed = true
      end
      for _, line in ipairs(new_block) do result[#result + 1] = line end
    else
      for _, line in ipairs(block) do result[#result + 1] = line end
    end
  end
  local rewritten = table.concat(result, "\n")
  if had_trailing_newline then rewritten = rewritten .. "\n" end
  if changed and rewritten ~= text then return rewritten end
  return nil
end

local function dnf_major_version()
  if not PM or PM.label ~= "dnf" then return 0 end
  local ok, _, out = gg.spawn({ argv = { PM.bin, "--version" }, capture = true, echo = false })
  if not ok then return 0 end
  local version = (out or ""):match("(%d+)%.%d+")
  return tonumber(version) or 0
end

local function make_fedora_dnf5_override(candidate)
  local path = "/etc/dnf/repos.override.d/99-gg-setup-mirror.repo"
  local old = gg.read(path)
  if old and not old:find("# Managed by gg setup", 1, true) then
    return nil, tr("Refusing to overwrite an unmanaged DNF5 override: %s", "拒绝覆盖未由 gg 管理的 DNF5 配置：%s"):format(path)
  end
  local root = candidate.root
  local content = table.concat({
    "# Managed by gg setup; original configuration is backed up before each change.",
    "[fedora]",
    "baseurl=" .. root .. "/releases/$releasever/Everything/$basearch/os/",
    "metalink=",
    "mirrorlist=",
    "",
    "[updates]",
    "baseurl=" .. root .. "/updates/$releasever/Everything/$basearch/",
    "metalink=",
    "mirrorlist=",
    "",
  }, "\n")
  return { { path = path, old = old, new = content,
             mkdir = "/etc/dnf/repos.override.d" } }
end

local function make_rpm_changes(spec, candidate)
  if spec.id == "fedora" and dnf_major_version() >= 5 then
    return make_fedora_dnf5_override(candidate)
  end
  local changes = {}
  local dirs = { "/etc/yum.repos.d", "/etc/dnf/repos.d" }
  for _, dir in ipairs(dirs) do
    if gg.is_dir(dir) then
      for _, path in ipairs(gg.list(dir, true)) do
        if gg.is_file(path) and gg.basename(path):match("%.repo$") then
          local old = gg.read(path)
          if old then
            local new = rewrite_rpm_file(old, spec, candidate.root)
            if new then changes[#changes + 1] = { path = path, old = old, new = new } end
          end
        end
      end
    end
  end
  if #changes == 0 then
    return nil, tr("No safe, supported repository entries were found; no files were changed.",
                   "没有找到可安全修改的受支持仓库配置；未更改任何文件。")
  end
  return changes
end

local function unique_backup_path(path)
  local stamp = os.date("%Y%m%d-%H%M%S")
  local candidate = path .. ".gg.bak." .. stamp
  local suffix = 1
  while gg.exists(candidate) do
    candidate = path .. ".gg.bak." .. stamp .. "." .. tostring(suffix)
    suffix = suffix + 1
  end
  return candidate
end

local function privileged_run(ctx, argv)
  if not gg.platform.root and not gg.which("sudo") then
    return false, 127
  end
  return ctx.run({ argv = argv, sudo = true, pause = false, echo = false })
end

local function rollback_changes(ctx, changes)
  for i = #changes, 1, -1 do
    local change = changes[i]
    if change.applied then
      if change.backup then
        privileged_run(ctx, { "cp", "-a", change.backup, change.path })
      else
        privileged_run(ctx, { "rm", "-f", change.path })
      end
    end
  end
end

local function apply_config_changes(ctx, changes)
  if not gg.platform.root and not gg.which("sudo") then
    ctx.err(tr("Switching system repositories requires root or sudo.", "切换系统软件源需要 root 或 sudo。"))
    return false
  end
  if not gg.which("cp") then
    ctx.err(tr("The cp utility is required to create backups and apply the change.", "需要 cp 命令来备份并应用修改。"))
    return false
  end
  local backups = {}
  -- Back up every existing file before the first write. A failed backup aborts.
  for _, change in ipairs(changes) do
    if change.old ~= nil or gg.exists(change.path) then
      local backup = unique_backup_path(change.path)
      local ok, code = privileged_run(ctx, { "cp", "-a", change.path, backup })
      if not ok then
        ctx.err(tr("Could not back up %s (exit %s); nothing was changed.",
                   "无法备份 %s（退出码 %s）；未修改配置。"), change.path, tostring(code))
        return false
      end
      change.backup = backup
      backups[#backups + 1] = backup
      ctx.log(tr("Backup created: %s", "已创建备份：%s"), backup)
    end
  end
  for _, change in ipairs(changes) do
    if change.mkdir then
      local ok, code = privileged_run(ctx, { "mkdir", "-p", change.mkdir })
      if not ok then
        ctx.err(tr("Could not create %s (exit %s).", "无法创建 %s（退出码 %s）。"), change.mkdir, tostring(code))
        rollback_changes(ctx, changes)
        return false
      end
    end
    local temp = gg.mkstemp(".gg-setup")
    local wrote = gg.write(temp, change.new)
    if not wrote then
      gg.rm(temp)
      ctx.err(tr("Could not write a temporary configuration for %s.", "无法为 %s 写入临时配置。"), change.path)
      rollback_changes(ctx, changes)
      return false
    end
    -- Mark it before cp: a failed copy can still have partially truncated the target.
    change.applied = true
    local ok, code = privileged_run(ctx, { "cp", temp, change.path })
    gg.rm(temp)
    if not ok then
      ctx.err(tr("Could not update %s (exit %s); restoring backups.", "无法更新 %s（退出码 %s）；正在还原备份。"),
              change.path, tostring(code))
      rollback_changes(ctx, changes)
      return false
    end
    change.applied = true
  end
  ctx.ok(tr("Mirror configuration updated.", "软件源配置已更新。"))
  if #backups == 0 then ctx.log(tr("  (new override file; no existing file was overwritten)", "  （新建覆盖配置，没有覆盖旧文件）")) end
  ctx.log(tr("Run `gg setup update` to refresh package metadata.", "运行 `gg setup update` 刷新软件源索引。"))
  return true
end

local function apply_mirror(ctx, family, spec, candidate)
  local changes, err
  if family == "apt" then
    changes = make_apt_changes(candidate)
    if #changes == 0 then
      ctx.warn(tr("No official or supported mirror URLs were found in apt source files; nothing was changed.",
                  "apt 源文件中没有发现官方或受支持的镜像地址；未更改配置。"))
      return 1
    end
  else
    changes, err = make_rpm_changes(spec, candidate)
    if not changes then
      ctx.warn(err or tr("No supported DNF/YUM source configuration was found.", "未找到受支持的 DNF/YUM 源配置。"))
      return 1
    end
  end
  local prompt = tr(
    "Switch repository configuration to %s? Each existing file will be backed up before editing.",
    "确定切换到 %s？每个现有配置文件都会在修改前备份。")
  if not ctx.confirm(prompt:format(candidate.label), 0) then
    ctx.warn(tr("Cancelled; no repository files were changed.", "已取消；未更改软件源配置。"))
    return 0
  end
  return apply_config_changes(ctx, changes) and 0 or 1
end

local function run_mirror_flow(ctx, selected_key)
  local items, family, spec = mirror_candidates()
  if not items then
    ctx.err(tr("Safe mirror switching is currently supported for Debian/Ubuntu, Fedora, Rocky, AlmaLinux, and CentOS Stream.",
               "目前仅对 Debian/Ubuntu、Fedora、Rocky、AlmaLinux 和 CentOS Stream 提供安全的镜像测速/换源配置。"))
    return 1
  end
  if not gg.which("curl") then
    if not PM or not PM.commands.install then
      ctx.err(tr("curl is required to test mirror download speed, and no installer is available.",
                 "测速需要 curl，但没有可用的安装方式。"))
      return 1
    end
    local prompt = tr("curl is missing. Install it with %s before testing mirrors?",
                      "缺少 curl。先使用 %s 安装，再测试镜像吗？"):format(PM.label)
    if not ctx.confirm(prompt, 0) then
      ctx.warn(tr("Mirror test cancelled; no source files were changed.", "已取消测速；未更改软件源配置。"))
      return 0
    end
    run_package_action(ctx, "install", { "curl" })
    gg.refresh_tools()
    if not gg.which("curl") then
      ctx.err(tr("curl was not found after installation.", "安装后仍未找到 curl。"))
      return 1
    end
  end
  ctx.log(tr("Testing %d repository mirrors (up to 6 seconds each)...", "正在测试 %d 个软件源（每个最多 6 秒）……"), #items)
  local tested = {}
  for _, item in ipairs(items) do
    ctx.log(tr("Testing %s", "测速：%s"), item.label)
    local result, reason = test_mirror(item)
    item.result = result
    item.failure = reason
    if result then
      ctx.log("  %s  %.2fs  %s", item.label, result.elapsed, format_speed(result.bytes_per_sec))
      tested[#tested + 1] = item
    else
      ctx.warn("  %s  %s", item.label, tr("unavailable", "不可用"))
    end
  end
  table.sort(tested, function(a, b)
    if a.result.bytes_per_sec == b.result.bytes_per_sec then
      return a.result.elapsed < b.result.elapsed
    end
    return a.result.bytes_per_sec > b.result.bytes_per_sec
  end)
  if #tested == 0 then
    ctx.err(tr("None of the tested mirrors responded successfully; no files were changed.",
               "所有测试源均不可用；未更改任何配置。"))
    return 1
  end
  local selected
  if selected_key and selected_key ~= "" then
    selected_key = lower(selected_key)
    for _, item in ipairs(tested) do
      if item.key == selected_key then selected = item break end
    end
    if not selected then
      ctx.err(tr("Mirror '%s' was not reachable or is not a valid choice. Run `gg setup mirrors` to retest.",
                 "镜像“%s”不可用或不是有效选项。运行 `gg setup mirrors` 重新测速。"), selected_key)
      return 2
    end
  elseif gg.interactive then
    local menu = { tr("Cancel / keep current source", "取消 / 保持当前源") }
    for _, item in ipairs(tested) do
      menu[#menu + 1] = string.format("%s  ·  %.2fs  ·  %s", item.label,
                                      item.result.elapsed, format_speed(item.result.bytes_per_sec))
    end
    local choice = ctx.tui.menu({
      title = tr("Choose a tested mirror (fastest first)", "选择已测速的软件源（按速度排序）"),
      status = tr("No configuration is changed until you confirm.", "确认前不会修改任何配置。"),
      items = menu,
    })
    if not choice or choice == 1 then
      ctx.log(tr("Keeping the current source.", "保持当前软件源。"))
      return 0
    end
    selected = tested[choice - 1]
  else
    ctx.log(tr("Choose a reachable mirror explicitly: gg setup mirror <tuna|ustc|aliyun|official>",
               "请显式选择可用镜像：gg setup mirror <tuna|ustc|aliyun|official>"))
    return 0
  end
  if not selected then return 0 end
  return apply_mirror(ctx, family, spec, selected)
end

local function home_dir()
  return gg.getenv("HOME") or gg.getenv("USERPROFILE") or gg.home or "."
end

local function path_join(a, b)
  return gg.join_path(a, b)
end

local function local_backup(path)
  if not gg.is_file(path) then return nil end
  local backup = unique_backup_path(path)
  if not gg.copy(path, backup) then return false end
  return backup
end

local function shell_kind()
  local shell = lower(gg.getenv("SHELL") or "")
  local base = shell:match("([^/\\]+)$") or shell
  if base == "fish" then return "fish" end
  if base == "zsh" then return "zsh" end
  if base == "bash" then return "bash" end
  if gg.is_file(path_join(home_dir(), ".zshrc")) then return "zsh" end
  if gg.is_file(path_join(home_dir(), ".bashrc")) then return "bash" end
  return "bash"
end

local function profile_path(kind)
  if kind == "fish" then return path_join(home_dir(), ".config/fish/config.fish") end
  if kind == "zsh" then return path_join(home_dir(), ".zshrc") end
  if kind == "bash" then return path_join(home_dir(), ".bashrc") end
  return path_join(home_dir(), ".profile")
end

local function append_profile_block(ctx, path, marker, block)
  local old = gg.read(path) or ""
  if old:find(marker, 1, true) then
    ctx.log(tr("Shell profile already contains the setup block: %s", "Shell 配置中已存在此设置：%s"), path)
    return true
  end
  local backup = local_backup(path)
  if backup == false then
    ctx.err(tr("Could not back up shell profile %s; it was not changed.", "无法备份 shell 配置 %s；未做修改。"), path)
    return false
  end
  local parent = gg.dirname(path)
  if not gg.is_dir(parent) and not gg.mkdir(parent) then
    ctx.err(tr("Could not create shell profile directory %s.", "无法创建 shell 配置目录 %s。"), parent)
    return false
  end
  local sep = (old == "" or old:sub(-1) == "\n") and "" or "\n"
  local new = old .. sep .. block .. "\n"
  if not gg.write(path, new) then
    if backup then gg.copy(backup, path) end
    ctx.err(tr("Could not update shell profile %s.", "无法更新 shell 配置 %s。"), path)
    return false
  end
  if backup then ctx.log(tr("Profile backup: %s", "配置备份：%s"), backup) end
  ctx.ok(tr("Updated shell profile: %s", "已更新 shell 配置：%s"), path)
  return true
end

local function detected_nvm()
  if gg.which("nvm") or gg.which("nvm.exe") then return true end
  local nvm_dir = gg.getenv("NVM_DIR") or path_join(home_dir(), ".nvm")
  return gg.is_file(path_join(nvm_dir, "nvm.sh"))
end

local function uv_path()
  local path = gg.which("uv") or gg.which("uv.exe")
  if path then return path end
  local home = home_dir()
  local candidates = {
    path_join(home, ".local/bin/uv"), path_join(home, ".local/bin/uv.exe"),
    path_join(home, ".cargo/bin/uv"), path_join(home, ".cargo/bin/uv.exe"),
  }
  for _, candidate in ipairs(candidates) do
    if gg.is_file(candidate) then return candidate end
  end
  return nil
end

local function path_contains(path)
  local path_value = gg.getenv("PATH") or ""
  local separator = lower(gg.platform.os) == "windows" and ";" or ":"
  local expected = path:gsub("[\\/]$", "")
  for entry in (path_value .. separator):gmatch("(.-)" .. escape_pattern(separator)) do
    if lower(entry:gsub("[\\/]$", "")) == lower(expected) then return true end
  end
  return false
end

local function ensure_local_bin_path(ctx)
  local kind = shell_kind()
  local path = profile_path(kind)
  local marker = "# >>> gg uv path >>>"
  local block
  if kind == "fish" then
    block = table.concat({ marker, 'fish_add_path --global "$HOME/.local/bin"', "# <<< gg uv path <<<" }, "\n")
  else
    block = table.concat({
      marker,
      'case ":$PATH:" in',
      '  *":$HOME/.local/bin:"*) ;;',
      '  *) PATH="$HOME/.local/bin:$PATH" ;;',
      "esac",
      "export PATH",
      "# <<< gg uv path <<<",
    }, "\n")
  end
  return append_profile_block(ctx, path, marker, block)
end

local function ensure_nvm(ctx)
  if detected_nvm() then
    ctx.ok(tr("nvm is already installed.", "nvm 已安装。"))
    return 0
  end
  if lower(gg.platform.os) == "windows" then
    local winget = gg.which("winget")
    if not winget then
      ctx.err(tr("nvm is missing. Install winget, then retry (Windows package: CoreyButler.NVMforWindows).",
                 "未找到 nvm。请先安装 winget 后重试（Windows 包：CoreyButler.NVMforWindows）。"))
      return 1
    end
    if not ctx.confirm(tr("nvm is not installed. Install nvm-windows using winget?", "nvm 尚未安装。现在使用 winget 安装 nvm-windows 吗？"), 0) then
      ctx.warn(tr("Installation skipped.", "已跳过安装。"))
      return 0
    end
    local ok, code = ctx.run({ argv = { winget, "install", "--exact", "--id", "CoreyButler.NVMforWindows",
      "--accept-package-agreements", "--accept-source-agreements" }, pause = true })
    if ok then ctx.ok(tr("nvm-windows installed; open a new terminal to use it.", "nvm-windows 已安装；请重新打开终端使用。")) end
    return ok and 0 or (code or 1)
  end
  if lower(gg.platform.os) ~= "linux" and lower(gg.platform.os) ~= "darwin" then
    ctx.err(tr("The nvm-sh installer is supported on Linux and macOS; this platform needs a manual setup.",
               "nvm-sh 安装器支持 Linux 和 macOS；此平台需要手动安装。"))
    return 1
  end
  local kind = shell_kind()
  if kind == "fish" then
    ctx.err(tr("nvm-sh does not support fish directly. Use bash/zsh or a fish compatibility plugin.",
               "nvm-sh 不直接支持 fish。请使用 bash/zsh 或 fish 兼容插件。"))
    return 1
  end
  local git = gg.which("git")
  if not git then
    ctx.err(tr("git is required to install nvm.", "安装 nvm 需要 git。"))
    return 1
  end
  local home = home_dir()
  local nvm_dir = gg.getenv("NVM_DIR") or path_join(home, ".nvm")
  if gg.exists(nvm_dir) then
    ctx.err(tr("%s already exists but nvm.sh was not found; refusing to overwrite it.",
               "%s 已存在但找不到 nvm.sh；为避免覆盖，已停止安装。"), nvm_dir)
    return 1
  end
  if not ctx.confirm(tr("nvm is not installed. Clone the official nvm-sh repository and update your shell profile?",
                        "nvm 尚未安装。现在克隆官方 nvm-sh 仓库并更新 shell 配置吗？"), 0) then
    ctx.warn(tr("Installation skipped.", "已跳过安装。"))
    return 0
  end
  local ok, code = ctx.run({ argv = { git, "clone", "--depth", "1", "https://github.com/nvm-sh/nvm.git", nvm_dir }, pause = true })
  if not ok then
    ctx.err(tr("nvm installation failed (exit %s).", "nvm 安装失败（退出码 %s）。"), tostring(code))
    return code or 1
  end
  local profile = profile_path(kind)
  local block = table.concat({
    "# >>> gg nvm >>>",
    'export NVM_DIR="${NVM_DIR:-$HOME/.nvm}"',
    '[ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh"',
    '[ -s "$NVM_DIR/bash_completion" ] && . "$NVM_DIR/bash_completion"',
    "# <<< gg nvm <<<",
  }, "\n")
  if not append_profile_block(ctx, profile, "# >>> gg nvm >>>", block) then
    ctx.warn(tr("nvm was cloned, but shell startup configuration still needs to be added manually.",
                "nvm 已克隆，但仍需手动添加 shell 启动配置。"))
    return 1
  end
  ctx.ok(tr("nvm installed. Reopen the shell (or source %s) to load it.",
            "nvm 已安装。重新打开 shell（或 source %s）后生效。"), profile)
  return 0
end

local function ensure_uv(ctx)
  local existing = uv_path()
  if existing then
    ctx.ok(tr("uv is already installed: %s", "uv 已安装：%s"), existing)
    return 0
  end
  if lower(gg.platform.os) == "windows" then
    local winget = gg.which("winget")
    if not winget then
      ctx.err(tr("uv is missing; install winget or install uv manually from astral.sh.",
                 "未找到 uv；请安装 winget，或从 astral.sh 手动安装 uv。"))
      return 1
    end
    if not ctx.confirm(tr("uv is not installed. Install it using winget?", "uv 尚未安装。现在使用 winget 安装吗？"), 0) then
      ctx.warn(tr("Installation skipped.", "已跳过安装。"))
      return 0
    end
    local ok, code = ctx.run({ argv = { winget, "install", "--exact", "--id", "astral-sh.uv",
      "--accept-package-agreements", "--accept-source-agreements" }, pause = true })
    if ok then ctx.ok(tr("uv installed; open a new terminal to update PATH.", "uv 已安装；请重新打开终端刷新 PATH。")) end
    return ok and 0 or (code or 1)
  end
  if lower(gg.platform.os) ~= "linux" and lower(gg.platform.os) ~= "darwin" then
    ctx.err(tr("The official uv installer supports Linux, macOS and Windows; install uv manually on this platform.",
               "官方 uv 安装器支持 Linux、macOS 和 Windows；此平台请手动安装。"))
    return 1
  end
  local curl = gg.which("curl")
  if not curl then
    ctx.err(tr("curl is required to download the official uv installer.", "下载 uv 官方安装器需要 curl。"))
    return 1
  end
  if not ctx.confirm(tr("uv is not installed. Download and run the official installer from astral.sh?",
                        "uv 尚未安装。现在下载并运行 astral.sh 官方安装器吗？"), 0) then
    ctx.warn(tr("Installation skipped.", "已跳过安装。"))
    return 0
  end
  local temp = gg.mkstemp(".uv-install.sh")
  local downloaded, download_code = gg.spawn({
    argv = { curl, "--fail", "--location", "--silent", "--show-error",
      "--connect-timeout", "10", "--max-time", "60", "--output", temp,
      "https://astral.sh/uv/install.sh" },
    echo = false,
  })
  if not downloaded then
    gg.rm(temp)
    ctx.err(tr("Could not download the uv installer (exit %s).", "无法下载 uv 安装器（退出码 %s）。"), tostring(download_code))
    return download_code or 1
  end
  local env = gg.which("env")
  if not env then
    gg.rm(temp)
    ctx.err(tr("The env utility is required to run the uv installer safely.", "安全运行 uv 安装器需要 env 命令。"))
    return 1
  end
  local ok, code = ctx.run({ argv = { env, "UV_NO_MODIFY_PATH=1", "sh", temp }, pause = true })
  gg.rm(temp)
  if not ok then
    ctx.err(tr("uv installation failed (exit %s).", "uv 安装失败（退出码 %s）。"), tostring(code))
    return code or 1
  end
  gg.refresh_tools()
  local installed = uv_path()
  if installed then
    ctx.ok(tr("uv installed: %s", "uv 已安装：%s"), installed)
    if not path_contains(path_join(home_dir(), ".local/bin")) then
      ensure_local_bin_path(ctx)
      ctx.log(tr("Open a new shell, or source the profile, to use uv by name.",
                 "重新打开 shell 或 source 配置文件后即可直接使用 uv。"))
    end
  else
    ctx.warn(tr("The installer finished, but uv was not found on disk; inspect the output above.",
                "安装器已运行，但未在磁盘上找到 uv；请检查上方输出。"))
  end
  return 0
end

local function tool_status()
  local nvm = detected_nvm() and tr("installed", "已安装") or tr("missing", "未安装")
  local uv = uv_path()
  return nvm, uv and (tr("installed: ", "已安装：") .. uv) or tr("missing", "未安装")
end

local function show_status(ctx)
  ctx.log(tr("OS: %s (%s)", "系统：%s（%s）"), OS.name, OS.version_id ~= "" and OS.version_id or "unknown")
  ctx.log(tr("Package manager: %s", "包管理器：%s"), PM and PM.label or tr("not found", "未找到"))
  local nvm, uv = tool_status()
  ctx.log("nvm: %s", nvm)
  ctx.log("uv: %s", uv)
  return 0
end

local function tools_menu(ctx)
  while true do
    local nvm, uv = tool_status()
    local choice = ctx.tui.menu({
      title = tr("Developer tool managers", "开发工具管理器"),
      status = tr("Missing tools are installed only after you confirm.", "缺少的工具只有在你确认后才会安装。"),
      items = {
        tr("Check / install nvm — ", "检查 / 安装 nvm — ") .. nvm,
        tr("Check / install uv — ", "检查 / 安装 uv — ") .. uv,
        tr("Back", "返回"),
      },
    })
    if not choice or choice == 3 then return 0 end
    if choice == 1 then ensure_nvm(ctx)
    elseif choice == 2 then ensure_uv(ctx) end
  end
end

function M.tui(ctx)
  local status = PM and tr("Detected package manager: %s", "已识别包管理器：%s"):format(PM.label)
                         or tr("No supported package manager found", "未找到受支持的包管理器")
  local menu = {
    tr("Refresh package index / update", "刷新软件源索引 / update"),
    tr("Upgrade installed packages", "升级已安装软件包"),
    tr("Install packages", "安装软件包"),
    tr("Remove packages", "卸载软件包"),
    tr("Search packages", "搜索软件包"),
    tr("List installed packages", "列出已安装软件包"),
    tr("Clean package cache", "清理软件缓存"),
    tr("Remove unused dependencies", "清理无用依赖"),
    tr("Test mirror speed and switch source", "测试镜像速度并选择换源"),
    tr("Manage nvm / uv", "管理 nvm / uv"),
    tr("Show system status", "查看系统状态"),
  }
  while true do
    local choice = ctx.tui.menu({ title = M.title, status = status, items = menu })
    if not choice then return 0 end
    if choice >= 1 and choice <= 8 then
      local action = PACKAGE_ACTIONS[choice][1]
      run_package_action(ctx, action, package_tokens(ctx))
    elseif choice == 9 then
      run_mirror_flow(ctx)
    elseif choice == 10 then
      tools_menu(ctx)
    elseif choice == 11 then
      show_status(ctx)
      ctx.tui.message(tr("System status", "系统状态"), tr("See the command output for OS, package manager, nvm and uv.", "OS、包管理器、nvm 和 uv 信息已输出。"))
    end
  end
end

M.params = {
  { name = "action", pos = 1, type = "string", label = "Action / 操作",
    help = "update, upgrade, install, remove, search, list, mirrors, nvm, uv, status" },
  { name = "package", pos = 2, type = "string", label = "Package / mirror",
    help = "Package name(s), or mirror key: tuna, ustc, aliyun, official" },
}

function M.run(ctx)
  local action = lower(gg.trim(ctx.args.action or ""))
  if action == "" then
    ctx.log(tr("Unified system setup; detected package manager: %s", "统一系统工具箱；已识别包管理器：%s"),
            PM and PM.label or tr("none", "无"))
    ctx.log(tr("Run without arguments in a terminal for the menu, or `gg setup --help` for CLI usage.",
               "在终端中不带参数运行可打开菜单，CLI 用法请看 `gg setup --help`。"))
    return 0
  end
  if action == "status" then return show_status(ctx) end
  if action == "mirror" or action == "mirrors" then
    local key = ctx.args.package
    if not key and ctx.rest and ctx.rest[1] then key = ctx.rest[1] end
    return run_mirror_flow(ctx, key)
  end
  if action == "tools" then
    if gg.interactive then return tools_menu(ctx) end
    local nvm, uv = tool_status()
    ctx.log("nvm: %s", nvm); ctx.log("uv: %s", uv)
    return 0
  end
  if action == "nvm" then return ensure_nvm(ctx) end
  if action == "uv" then return ensure_uv(ctx) end
  for _, item in ipairs(PACKAGE_ACTIONS) do
    if item[1] == action then return run_package_action(ctx, action, package_tokens(ctx)) end
  end
  ctx.err(tr("Unknown action: %s", "未知操作：%s"), action)
  ctx.log(tr("Actions: update, upgrade, install, remove, search, list, clean, autoremove, mirrors, tools, nvm, uv, status",
             "操作：update、upgrade、install、remove、search、list、clean、autoremove、mirrors、tools、nvm、uv、status"))
  return 2
end

return M
