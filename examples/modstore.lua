-- gg module: modstore — a pinned, hash-verified online module catalog.
-- Registry contents are anchored to the SHA-256 shipped in this gg build;
-- GitHub accelerators are transport only and never change the trust decision.
local M = {}

M.title = "Online modules"
M.desc = "Search, inspect, install, update, and run hash-verified gg modules."
M.params = {
  { name = "action", pos = 1, type = "string", default = "search",
    label = "Action", help = "search, info, install, update, run, or test" },
  { name = "package", pos = 2, type = "string",
    label = "Package", help = "Module name or name@version" },
}

local RAW_ROOT = "https://raw.githubusercontent.com/ejir/gg/main/"
local INDEX_PATH = "modules/index.json"
local FETCH_MARKER = "GG_MODULE_FETCH"
local INDEX_LIMIT = 262144
local MODULE_LIMIT = 524288
local DEFAULT_PROXIES = { "gh-proxy.com", "ghfast.top", "ghproxy.net" }
local ALLOWED_PERMISSIONS = {
  ["process"] = true,
  ["network"] = true,
  ["filesystem-write"] = true,
  ["environment"] = true,
}

local function tr(en, zh) return gg.i18n(en, zh) end
local function safe_name(s)
  return type(s) == "string" and #s > 0 and #s <= 64 and
         s:match("^[a-z0-9][a-z0-9_-]*$") ~= nil
end

local function safe_version(s)
  return type(s) == "string" and s:match("^%d+%.%d+%.%d+$") ~= nil
end

local function version_tuple(s)
  local a, b, c = tostring(s):match("^(%d+)%.(%d+)%.(%d+)$")
  if not a then return nil end
  return tonumber(a), tonumber(b), tonumber(c)
end

local function newer(a, b)
  if not b then return true end
  local aa, ab, ac = version_tuple(a)
  local ba, bb, bc = version_tuple(b)
  if not aa or not ba then return false end
  if aa ~= ba then return aa > ba end
  if ab ~= bb then return ab > bb end
  return ac > bc
end

local function permissions_valid(items)
  if type(items) ~= "table" then return false end
  local seen = {}
  for _, p in ipairs(items) do
    if type(p) ~= "string" or not ALLOWED_PERMISSIONS[p] or seen[p] then return false end
    seen[p] = true
  end
  return true
end

local function validate_catalog(body)
  local ok, catalog = pcall(gg.json.decode, body)
  if not ok or type(catalog) ~= "table" or catalog.schema ~= 1 or
     type(catalog.packages) ~= "table" or #catalog.packages > 200 then
    return nil, "invalid catalog JSON/schema"
  end
  local seen = {}
  for _, item in ipairs(catalog.packages) do
    if type(item) ~= "table" or not safe_name(item.name) or
       not safe_version(item.version) or type(item.description) ~= "string" or
       #item.description > 240 or type(item.file) ~= "string" or
       item.file ~= ("modules/%s/%s.lua"):format(item.name, item.version) or
       type(item.sha256) ~= "string" or #item.sha256 ~= 64 or
       item.sha256:match("^[0-9a-f]+$") == nil or
       type(item.license) ~= "string" or #item.license > 40 or
       item.license == "" or not permissions_valid(item.permissions) then
      return nil, "invalid package entry"
    end
    local key = item.name .. "@" .. item.version
    if seen[key] then return nil, "duplicate package version: " .. key end
    seen[key] = true
  end
  return catalog
end

local function normalize_proxy(host)
  host = tostring(host or ""):gsub("^%s+", ""):gsub("%s+$", "")
  host = host:gsub("^https://", ""):gsub("^http://", "")
  host = host:gsub("/+$", "")
  if host == "" or host:find("[/?:#@]", 1) or host:find("%s") or
     not host:match("^[%w%.%-]+$") then
    return nil
  end
  return host:lower()
end

local function routes()
  local out, seen = {}, {}
  local function add(label, base)
    if seen[label] then return end
    seen[label] = true
    out[#out + 1] = {
      label = label,
      url = function(path)
        local raw = RAW_ROOT .. path
        if base then return "https://" .. base .. "/" .. raw end
        return raw
      end,
    }
  end
  add("raw.githubusercontent.com", nil)
  local custom = gg.getenv("GG_GITHUB_PROXIES") or ""
  for host in custom:gmatch("[^,%s]+") do
    local normalized = normalize_proxy(host)
    if normalized then add(normalized, normalized) end
  end
  for _, host in ipairs(DEFAULT_PROXIES) do add(host, host) end
  return out
end

local function curl_available() return gg.which("curl") ~= nil end
local function wget_available() return gg.which("wget") ~= nil end

local function fetch_index(route)
  local url = route.url(INDEX_PATH)
  local started = gg.now()
  if curl_available() then
    local marker = "\n" .. FETCH_MARKER .. ":%{http_code}:%{time_total}\n"
    local argv = {
      "curl", "--fail", "--location", "--silent", "--show-error",
      "--connect-timeout", "2", "--max-time", "5",
      "--max-filesize", tostring(INDEX_LIMIT),
      "--proto", "=https", "--proto-redir", "=https",
      "--write-out", marker, url,
    }
    local ok, code, output = gg.spawn({ argv = argv, capture = true, echo = false })
    output = output or ""
    local body, status, seconds = output:match("^(.-)\n" .. FETCH_MARKER .. ":(%d%d%d):([%d%.]+)\n?$")
    if not status then return nil, 0, gg.now() - started, "curl returned no HTTP status" end
    if not ok or tonumber(status) ~= 200 then
      return nil, tonumber(status), tonumber(seconds) or 0, "HTTP " .. status .. " (curl " .. tostring(code) .. ")"
    end
    if #body > INDEX_LIMIT then return nil, 200, tonumber(seconds) or 0, "catalog exceeds size limit" end
    return body, 200, tonumber(seconds) or 0
  elseif wget_available() then
    local ok, code, body = gg.spawn({
      argv = { "wget", "--quiet", "--timeout=5", "--output-document=-", url },
      capture = true, echo = false,
    })
    local elapsed = gg.now() - started
    body = body or ""
    if not ok or code ~= 0 then return nil, 0, elapsed, "wget failed" end
    if #body > INDEX_LIMIT then return nil, 200, elapsed, "catalog exceeds size limit" end
    return body, 200, elapsed
  end
  return nil, 0, 0, "curl or wget is required"
end

local function load_catalog(ctx)
  if not curl_available() and not wget_available() then
    ctx.err(tr("Online modules need curl or wget.", "在线模块需要 curl 或 wget。"))
    return nil
  end
  local accepted, failures = {}, {}
  for _, route in ipairs(routes()) do
    local body, status, seconds, err = fetch_index(route)
    if body and status == 200 then
      local got = gg.sha256(body)
      if got ~= gg.module_registry_hash then
        failures[#failures + 1] = route.label .. ": registry hash mismatch"
      else
        local catalog, why = validate_catalog(body)
        if catalog then
          route.seconds = seconds
          route.catalog = catalog
          accepted[#accepted + 1] = route
        else
          failures[#failures + 1] = route.label .. ": " .. tostring(why)
        end
      end
    else
      failures[#failures + 1] = route.label .. ": " .. tostring(err or ("HTTP " .. tostring(status)))
    end
  end
  table.sort(accepted, function(a, b)
    if a.seconds == b.seconds then return a.label < b.label end
    return a.seconds < b.seconds
  end)
  if #accepted == 0 then
    ctx.err(tr("No trusted GitHub route returned the pinned registry index.",
               "没有可用的 GitHub 路由返回与当前 gg 版本匹配的可信索引。"))
    for _, failure in ipairs(failures) do ctx.warn("  " .. failure) end
    ctx.warn(tr("Update gg if the registry was recently changed.",
                "如果 registry 刚更新，请先升级 gg。"))
    return nil
  end
  ctx.info(tr("Registry: %s (%.2fs; index SHA-256 verified)",
              "Registry：%s（%.2f 秒；索引 SHA-256 已验证）"),
           accepted[1].label, accepted[1].seconds)
  for _, failure in ipairs(failures) do ctx.warn(tr("Skipping route: %s", "跳过路由：%s"), failure) end
  return accepted
end

local function package_versions(catalog, name)
  local found = {}
  for _, item in ipairs(catalog.packages) do
    if item.name == name then found[#found + 1] = item end
  end
  table.sort(found, function(a, b) return newer(a.version, b.version) end)
  return found
end

local function split_selector(raw)
  if type(raw) ~= "string" then return nil end
  local name, version = raw:match("^([^@]+)@([^@]+)$")
  if not name then name = raw end
  if not safe_name(name) or (version and not safe_version(version)) then return nil end
  return name, version
end

local function resolve_package(catalog, raw)
  local name, version = split_selector(raw)
  if not name then return nil, "invalid package name (use lowercase letters, digits, dash, underscore)" end
  local found = package_versions(catalog, name)
  if #found == 0 then return nil, "package not found: " .. name end
  if version then
    for _, item in ipairs(found) do
      if item.version == version then return item end
    end
    return nil, "version not found: " .. name .. "@" .. version
  end
  return found[1]
end

local function display_permissions(items)
  if #items == 0 then return "none declared" end
  return table.concat(items, ", ")
end

local function print_package(ctx, item)
  ctx.log("%s@%s — %s", item.name, item.version, item.description)
  ctx.log("SHA-256: %s", item.sha256)
  ctx.log("Declared capabilities: %s", display_permissions(item.permissions))
  ctx.log("License: %s", tostring(item.license or "unspecified"))
end

local function module_state(name)
  for _, item in ipairs(gg.modules.list()) do
    if item.name == name then return item end
  end
  return nil
end

local function installed_registry_version(name)
  local path = gg.join_path(gg.dir, "registry", "installed", name .. ".json")
  local data = gg.read(path)
  if not data then return nil end
  local ok, record = pcall(gg.json.decode, data)
  if ok and type(record) == "table" then return record.version end
  return nil
end

local function download_module(routes_ok, item, ctx)
  if not gg.mkdir(gg.modules_dir) then
    ctx.err(tr("Could not create the gg module directory.", "无法创建 gg 模块目录。"))
    return nil
  end
  for _, route in ipairs(routes_ok) do
    local temp = gg.mkstemp(".download", gg.modules_dir)
    local url = route.url(item.file)
    local ok, code
    if curl_available() then
      ok, code = gg.spawn({
        argv = {
          "curl", "--fail", "--location", "--silent", "--show-error",
          "--connect-timeout", "2", "--max-time", "10",
          "--max-filesize", tostring(MODULE_LIMIT),
          "--proto", "=https", "--proto-redir", "=https",
          "--output", temp, url,
        },
        capture = true, echo = false,
      })
    else
      ok, code = gg.spawn({
        argv = { "wget", "--quiet", "--timeout=10", "--output-document=" .. temp, url },
        capture = true, echo = false,
      })
    end
    local stat = gg.stat(temp)
    local source = stat and stat.size <= MODULE_LIMIT and gg.read(temp) or nil
    gg.rm(temp)
    if ok and code == 0 and source then
      local digest = gg.sha256(source)
      if digest == item.sha256 then
        local chunk, syntax_error = load(source, "@" .. item.file)
        if chunk then return source, route end
        ctx.warn(tr("Rejected invalid Lua from %s: %s", "拒绝来自 %s 的 Lua 语法错误：%s"),
                 route.label, tostring(syntax_error))
      else
        ctx.warn(tr("Rejected content from %s: SHA-256 mismatch.",
                    "拒绝来自 %s 的内容：SHA-256 不匹配。"), route.label)
      end
    else
      ctx.warn(tr("Download from %s failed; trying the next route.",
                  "从 %s 下载失败，尝试下一个路由。"), route.label)
    end
  end
  ctx.err(tr("No route returned the expected, valid module source.",
             "没有路由返回哈希正确且语法有效的模块源码。"))
  return nil
end

local function install_package(ctx, routes_ok, item, is_update)
  local target = gg.join_path(gg.modules_dir, item.name .. ".lua")
  local folder = gg.join_path(gg.modules_dir, item.name)
  local existing = gg.exists(target)
  if gg.is_dir(folder) then
    ctx.err(tr("A directory module already uses '%s'; remove or rename it first.",
               "已有目录模块占用 '%s'；请先移除或改名。"), item.name)
    return false
  end
  if is_update and not existing then
    ctx.err(tr("'%s' is not installed as a module file; use install instead.",
               "'%s' 不是已安装的单文件模块；请使用 install。"), item.name)
    return false
  end
  local current = existing and gg.read(target) or nil
  if current and gg.sha256(current) == item.sha256 then
    ctx.ok(tr("%s@%s is already current.", "%s@%s 已是最新版本。"), item.name, item.version)
    return true
  end
  local module = module_state(item.name)
  local replacing = existing or (module ~= nil)
  print_package(ctx, item)
  ctx.warn(tr("Lua modules run with your user permissions; review the source before running it.",
              "Lua 模块会以你的用户权限运行；运行前请检查源码。"))
  ctx.info(tr("Source: %s", "源码：%s"), RAW_ROOT .. item.file)
  local prompt
  if replacing then
    prompt = tr("Install %s@%s and replace/shadow the existing module?",
                "安装 %s@%s 并覆盖/遮蔽现有模块吗？"):format(item.name, item.version)
  else
    prompt = tr("Install %s@%s from the gg registry?",
                "从 gg registry 安装 %s@%s 吗？"):format(item.name, item.version)
  end
  if not ctx.confirm(prompt, 0) then
    ctx.warn(tr("Installation cancelled; no module was changed.", "已取消安装；没有更改模块。"))
    return false
  end
  local source, route = download_module(routes_ok, item, ctx)
  if not source then return false end

  local temp = gg.mkstemp(".install", gg.modules_dir)
  if not gg.write(temp, source) then
    gg.rm(temp)
    ctx.err(tr("Could not write the verified module to disk.", "无法写入已验证的模块。"))
    return false
  end
  local backup
  if existing then
    backup = gg.mkstemp(".backup", gg.modules_dir)
    gg.rm(backup)
    if not gg.move(target, backup) then
      gg.rm(temp)
      ctx.err(tr("Could not back up the existing module; it was not replaced.",
                 "无法备份现有模块；没有覆盖它。"))
      return false
    end
  end
  if not gg.move(temp, target) then
    gg.rm(temp)
    if backup then gg.move(backup, target) end
    ctx.err(tr("Could not atomically install the verified module.",
               "无法原子安装已验证的模块。"))
    return false
  end
  local provenance_dir = gg.join_path(gg.dir, "registry", "installed")
  gg.mkdir(provenance_dir)
  local record = {
    name = item.name,
    version = item.version,
    sha256 = item.sha256,
    registry_sha256 = gg.module_registry_hash,
    route = route.label,
  }
  local record_path = gg.join_path(provenance_dir, item.name .. ".json")
  if not gg.write(record_path, gg.json.encode(record)) then
    ctx.warn(tr("Module installed, but its provenance record could not be saved.",
                "模块已安装，但无法保存来源记录。"))
  end
  ctx.ok(tr("Installed %s@%s via %s.", "已通过 %s 安装 %s@%s。"),
         item.name, item.version, route.label)
  if backup then ctx.info(tr("Previous file backed up to %s", "旧文件已备份到 %s"), backup) end
  return true
end

local function run_installed(ctx, name)
  local argv = { gg.exe, name }
  for _, arg in ipairs(ctx.rest or {}) do argv[#argv + 1] = arg end
  local ok, code = ctx.run({ argv = argv })
  return ok and 0 or (tonumber(code) or 1)
end

local function latest_unique(catalog)
  local by_name = {}
  for _, item in ipairs(catalog.packages) do
    local old = by_name[item.name]
    if not old or newer(item.version, old.version) then by_name[item.name] = item end
  end
  local result = {}
  for _, item in pairs(by_name) do result[#result + 1] = item end
  table.sort(result, function(a, b) return a.name < b.name end)
  return result
end

function M.run(ctx)
  local action = tostring(ctx.args.action or "search"):lower()
  local raw_name = ctx.args.package
  if action == "help" then
    ctx.log("gg modules search [query]")
    ctx.log("gg modules info <name[@version]>")
    ctx.log("gg modules install <name[@version]> | update <name>")
    ctx.log("gg modules run <name[@version]> [args...]")
    ctx.log("gg modules test")
    return 0
  end
  if action == "run" then
    local name, version = split_selector(raw_name)
    if not name then ctx.err(tr("A valid package name is required.", "请提供有效的包名。")); return 1 end
    local installed = module_state(name)
    if installed and (not version or installed_registry_version(name) == version) then
      return run_installed(ctx, name)
    end
  end
  local routes_ok = load_catalog(ctx)
  if not routes_ok then return 1 end
  local catalog = routes_ok[1].catalog
  if action == "test" then
    ctx.log(tr("Tested GitHub routes (fastest first):", "GitHub 路由测速（按速度排序）："))
    for _, route in ipairs(routes_ok) do
      ctx.log("  %-26s %.3fs", route.label, route.seconds)
    end
    return 0
  elseif action == "search" or action == "list" or action == "catalog" then
    local query = tostring(raw_name or ""):lower()
    for _, extra in ipairs(ctx.rest or {}) do query = query .. " " .. tostring(extra):lower() end
    local count = 0
    for _, item in ipairs(latest_unique(catalog)) do
      local haystack = (item.name .. " " .. item.description):lower()
      if query == "" or haystack:find(query, 1, true) then
        ctx.log("%-20s %-10s %s", item.name, item.version, item.description)
        count = count + 1
      end
    end
    if count == 0 then ctx.warn(tr("No matching modules.", "没有匹配的模块。")) end
    return 0
  elseif action == "info" then
    local item, err = resolve_package(catalog, raw_name)
    if not item then ctx.err("%s", err); return 1 end
    print_package(ctx, item)
    ctx.log("Source: %s", RAW_ROOT .. item.file)
    return 0
  elseif action == "install" or action == "update" then
    local item, err = resolve_package(catalog, raw_name)
    if not item then ctx.err("%s", err); return 1 end
    return install_package(ctx, routes_ok, item, action == "update") and 0 or 1
  elseif action == "run" then
    local name, version = split_selector(raw_name)
    if not name then ctx.err(tr("A valid package name is required.", "请提供有效的包名。")); return 1 end
    local installed = module_state(name)
    if not installed or (version and installed_registry_version(name) ~= version) then
      local selector = name .. (version and ("@" .. version) or "")
      local item, err = resolve_package(catalog, selector)
      if not item then ctx.err("%s", err); return 1 end
      if not install_package(ctx, routes_ok, item, false) then return 1 end
    end
    return run_installed(ctx, name)
  end
  ctx.err(tr("Unknown registry action: %s", "未知 registry 操作：%s"), action)
  ctx.log("Run `gg modules help` for online module commands.")
  return 2
end

return M
