-- gg registry module: platform-info 1.0.0
-- Permission profile: none (reads only gg's already-available platform table).
local M = {}

M.title = "Platform info"
M.desc = "Print the operating system and architecture reported by gg."

function M.run(ctx)
  local p = gg.platform
  ctx.log("OS: %s", tostring(p.os or "unknown"))
  ctx.log("Architecture: %s", tostring(p.arch or "unknown"))
  ctx.log("CPU count: %s", tostring(p.cpus or "unknown"))
  return 0
end

return M
