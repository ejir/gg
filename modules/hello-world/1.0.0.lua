-- gg registry module: hello-world 1.0.0
-- Permission profile: none (no process, network, filesystem, or environment access).
local M = {}

M.title = "Hello world"
M.desc = "A tiny registry example that greets a name."
M.params = {
  { name = "name", pos = 1, type = "string", default = "world",
    label = "Name", help = "Who should receive the greeting?" },
}

function M.run(ctx)
  ctx.log("Hello, %s!", tostring(ctx.args.name or "world"))
  return 0
end

return M
