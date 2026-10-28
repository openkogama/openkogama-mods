local clicks = 0
local checked = false
local open = true

print(pcall(draw.rect, 0, 0, 10, 10) and "outside drawn" or "outside ok")
assert(server.send("test:hello", "from lua"))
ui.style({ WindowRounding = 6, FramePadding = { 6, 4 }, colors = { Button = "#2e8ce8" }, font = "segoeui.ttf", fontSize = 15 })
assert(not pcall(ui.style, { Nope = 1 }))
assert(not pcall(ui.style, { colors = { Nope = "#ffffff" } }))
assert(not pcall(ui.style, { font = "../evil.ttf" }))
assert(not pcall(ui.style, { WindowRounding = "round" }))
print("styles ok")
assert(not pcall(server.send, "bad name", "x"))

on("message", function(name, data)
  print("message " .. name .. " " .. data)
end)

on("key", function(key, down)
  print("key " .. key .. " " .. tostring(down))
end)

on("world", function()
  print("world")
end)

on("update", function()
  if checked then return end
  checked = true
  print(rawget(_G, "secret") == nil and "isolated" or "leaked")
  local width, height = screen.size()
  print("screen " .. width .. "x" .. height)
end)

on("draw", function()
  local width = screen.size()
  draw.rect(20, 20, 240, 60, "#14161cdd", { rounding = 10 })
  draw.rect(20, 20, 240, 60, rgb(47, 140, 232), { rounding = 10, thickness = 2 })
  draw.text("OpenKogama", 32, 26, { size = 22, font = "segoeuib.ttf" })
  draw.text("clicks " .. clicks, 32, 52, { size = 14, color = "#9fc7ff" })
  draw.line(20, 90, 260, 90, "#ffffff40", 1)
  draw.circle(width - 50, 50, 24, "#2f8ce8")
  draw.circle(width - 50, 50, 30, "#2f8ce880", { thickness = 3 })
  draw.text("Lua", width - 50, 40, { size = 18, align = "center" })
  assert(draw.measure("measure me", 18) > 0)
  draw.image("dot.png", 300, 20, 32, 16)
  if open then
    open = ui.window("Test", function()
      ui.text("Hello from " .. _VERSION)
      if ui.button("Click", { width = 120 }) then clicks = clicks + 1 end
      ui.sameline()
      ui.text("clicks: " .. clicks, { color = "#9fc7ff" })
      ui.checkbox("Enabled", true)
      ui.slider("Volume", 5, 0, 10)
      ui.slider("Speed", 0.5, 0, 1)
      ui.input("Name", "player", { hint = "your name" })
      ui.progress(0.4, { text = "40%" })
      ui.separator()
      ui.image("dot.png", 32, 16)
      ui.spacing()
      ui.with({ colors = { Text = "#ffcc00" }, fontSize = 18 }, function()
        ui.text("styled")
      end)
    end, { x = 40, y = 120, width = 300, closable = true })
    ui.window("Own", function()
      ui.text("own style")
    end, { x = 400, y = 120, style = { WindowBorderSize = 0, colors = { WindowBg = "#203040" } } })
  end
end)
