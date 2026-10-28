local clicks = 0

print("hud ready on " .. _VERSION)

on("world", function()
  print("world loaded")
end)

on("key", function(key, down)
  if down then print("pressed " .. key) end
end)

on("draw", function()
  local width, height = screen.size()
  draw.rect(16, 16, 280, 76, "#101318d8", { rounding = 12 })
  draw.image("logo.png", 26, 26, 56, 56)
  draw.text("OpenKogama scripts", 92, 26, { size = 22, font = "segoeuib.ttf" })
  draw.text("clicks: " .. clicks, 92, 56, { size = 16, color = "#9fc7ff", font = "segoeui.ttf" })
  local x, y, z = player.position()
  if x then
    draw.text(string.format("%.1f   %.1f   %.1f", x, y, z), width / 2, height - 40, { size = 18, align = "center" })
  end
  ui.window("Lua", function()
    ui.text("Hello, " .. (player.name() or "player"))
    if ui.button("Click me", { width = 200 }) then
      clicks = clicks + 1
      print("clicked " .. clicks)
    end
    ui.progress((clicks % 5) / 5, { text = clicks .. " / 5" })
  end, { x = 16, y = 110, width = 232, fixed = true })
  ui.window("Styled", function()
    ui.text("A window with its own style")
    ui.progress(0.6, { text = "60%" })
  end, { x = 16, y = 220, width = 232, fixed = true, style = {
    font = "segoeui.ttf", fontSize = 16, WindowRounding = 8, FrameRounding = 6, WindowPadding = { 12, 10 },
    colors = { WindowBg = "#101318e6", TitleBg = "#1c2230", TitleBgActive = "#1c2230", PlotHistogram = "#2e8ce8" },
  } })
end)
