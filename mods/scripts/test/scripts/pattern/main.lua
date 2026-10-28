local text = string.rep("a", 20000)
on("update", function()
  string.find(text, ".-.-.-.-b")
end)
