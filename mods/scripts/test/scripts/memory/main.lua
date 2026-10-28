on("update", function()
  local hoard = {}
  for i = 1, 1000 do
    hoard[i] = string.rep("x", 1 << 20) .. i
  end
end)
