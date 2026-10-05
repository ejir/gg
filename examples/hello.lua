-- gg module: hello — the smallest useful gg module.
-- 最简单的 gg 模块示例：参数、run() 和自定义 TUI。

local M = {}

M.title = "hello · 示例模块"
M.desc = "demonstrates params, run(ctx) and a custom TUI / 演示参数、run() 与自定义界面"

M.params = {
  { name = "name", pos = 1, type = "string", default = "world",
    label = "名字 / name", help = "要问候的对象 (who to greet)" },
  { name = "excited", short = "e", type = "bool", default = false,
    label = "感叹号 / excited", help = "多加几个感叹号" },
  { name = "times", short = "n", type = "int", default = 1,
    label = "次数 / times", help = "重复几次" },
}

function M.run(ctx)
  local name = ctx.args.name or "world"
  local n = ctx.args.times or 1
  local bang = ctx.args.excited and "!!!" or "."
  for i = 1, n do
    ctx.log("hello %s%s", name, bang)
  end
  if ctx.args.excited then
    ctx.log("today is %s", os.date("%Y-%m-%d %H:%M:%S"))
  end
  return 0
end

-- Optional: without M.tui gg shows an auto-generated form, which is often
-- exactly what you want.  This one is hand written to show the widgets.
function M.tui(ctx)
  while true do
    local sel = ctx.tui.menu({
      title = M.title,
      items = {
        "run(" .. tostring(ctx.args.name) .. ")",
        "ask for a name and run",
        "show the gg api cheat sheet",
        "quit",
      },
      footer = "↑↓ 移动  ⏎ 选择  esc 返回",
    })
    if sel == 2 then
      local who = ctx.ask("hello", "名字 / name: ", ctx.args.name or "world")
      if who then
        ctx.args.name = who
        ctx.args.excited = true
      end
    elseif sel == 3 then
      ctx.tui.message("gg api", table.concat({
        "ctx.args / ctx.rest    解析后的参数",
        "ctx.run{argv={...}}    前台运行命令",
        "ctx.capture('cmd')     捕获输出",
        "ctx.confirm/ask/select 简单提问",
        "ctx.tui.menu/form/...  全屏组件",
        "完整列表: gg help lua",
      }, "\n"))
    elseif sel == 4 or sel == nil then
      return 0
    end
    if sel == 1 or (ctx.args.excited and sel == 2) then
      return M.run(ctx)
    end
  end
end

return M
