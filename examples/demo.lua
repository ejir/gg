-- gg module: demo — a tour of the gg api and the TUI widgets.
-- gg demo：把 gg 的能力演示一遍。

local M = {}

M.title = "gg demo · 功能演示"
M.desc  = "tui 组件、进度条、json、编辑器、文件工具的示范"

M.params = {
  { name = "topic", pos = 1, type = "string",
    label = "主题 / topic", help = "platform | progress | json | editor | files" },
}

local function platform_demo(ctx)
  local p = gg.platform
  local lines = {
    "os        " .. tostring(p.os),
    "arch      " .. tostring(p.arch),
    "cpus      " .. tostring(p.cpus),
    "interactive " .. tostring(p.interactive),
    "root      " .. tostring(p.root),
    "lang      " .. tostring(p.lang),
    "home      " .. tostring(p.home),
    "gg dir    " .. tostring(gg.dir),
    "exe       " .. tostring(gg.exe),
    "",
    "JSON round trip:",
    gg.json.encode({ hello = "世界", list = { 1, 2, 3 }, ok = true }),
  }
  ctx.tui.message("platform", table.concat(lines, "\n"))
end

local function progress_demo(ctx)
  local bar = ctx.tui.progress({ title = "进度条 / progress", subtitle = "demo" })
  local total = 40
  for i = 0, total do
    bar:set(i * 100 / total, ("处理中 %d/%d"):format(i, total))
    bar:log(("step %02d: %s"):format(i, i % 5 == 0 and "关键步骤 ✔" or "…"))
    gg.sleep(0.03)
  end
  bar:done(true, "完成 / finished")
end

local function stream_demo(ctx)
  -- 实时读取子进程输出：aria2c、apt、curl 都适合这么干
  local bar = ctx.tui.progress({ title = "流式输出 / streaming", subtitle = "on_line" })
  local n = 0
  local ok, code = ctx.spawn({
    argv = { "sh", "-c", "for i in 1 2 3 4 5 6 7 8; do echo line $i; sleep 0.1; done" },
    shell = false,
    on_line = function(line)
      n = n + 1
      bar:set(n * 100 / 8, line)
      bar:log("got: " .. line)
    end,
  })
  bar:done(ok, ("exit=%s, %d lines"):format(tostring(code), n))
end

local function json_demo(ctx)
  local data = { name = "gg", tags = { "ape", "lua", "tui" },
                 nested = { n = 42, pi = 3.14, yes = true } }
  local encoded = gg.json.encode(data)
  local back, err = gg.json.decode(encoded)
  ctx.tui.message("json", table.concat({
    "encoded:", encoded, "",
    "decoded name: " .. tostring(back and back.name),
    "decoded tags: " .. tostring(back and table.concat(back.tags, ", ")),
    "error: " .. tostring(err),
  }, "\n"))
end

local function editor_demo(ctx)
  local text = table.concat({
    "-- 内置编辑器 / built-in editor",
    "local t = {}",
    "for i = 1, 3 do",
    "  t[#t + 1] = i * i",
    "end",
    "print(table.concat(t, ', '))",
  }, "\n")
  local edited = ctx.tui.textbox({ title = "editor demo", filename = "scratch.lua", text = text })
  if not edited then return end
  local path = gg.fs.tempname(".lua")
  gg.write(path, edited)
  ctx.log("写入 / wrote %s (%d bytes)", path, #edited)
  local ls = gg.capture("ls -l " .. gg.str.quote(path))
  ctx.log(ls)
end

local function files_demo(ctx)
  local dir = gg.join_path(gg.home, ".gg")
  local names = gg.list(dir)
  local lines = { "~/.gg 内容 / contents:" }
  for _, name in ipairs(names) do
    local st = gg.stat(gg.join_path(dir, name))
    lines[#lines + 1] = ("  %-24s %s"):format(name, st and st.is_dir and "<dir>" or tostring(st and st.size))
  end
  lines[#lines + 1] = ""
  lines[#lines + 1] = "which(curl) = " .. tostring(gg.which("curl"))
  lines[#lines + 1] = "which(aria2c) = " .. tostring(gg.which("aria2c"))
  lines[#lines + 1] = "which(apt) = " .. tostring(gg.which("apt"))
  ctx.tui.message("files", table.concat(lines, "\n"))
end

local DEMOS = {
  { name = "platform", label = "平台信息 / platform info", fn = platform_demo },
  { name = "progress", label = "进度条 / progress bar", fn = progress_demo },
  { name = "stream", label = "流式读输出 / stream a child", fn = stream_demo },
  { name = "json", label = "JSON / json", fn = json_demo },
  { name = "editor", label = "内置编辑器 / editor", fn = editor_demo },
  { name = "files", label = "文件工具 / files", fn = files_demo },
}

function M.run(ctx)
  for _, d in ipairs(DEMOS) do
    if d.name == ctx.args.topic then return d.fn(ctx) or 0 end
  end
  ctx.log("可用演示 / available demos: %s", table.concat({ "platform", "progress",
            "stream", "json", "editor", "files" }, ", "))
  ctx.log("例如 / for example: gg demo progress")
  return 0
end

function M.tui(ctx)
  local items = {}
  for _, d in ipairs(DEMOS) do items[#items + 1] = d.label end
  items[#items + 1] = "退出 / quit"
  local sel = ctx.tui.menu({ title = M.title, items = items })
  if not sel or sel == #DEMOS + 1 then return 0 end
  return DEMOS[sel].fn(ctx) or 0
end

return M
