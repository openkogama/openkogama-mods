local count = 0
every(0.05, function()
  count = count + 1
end)
after(0.25, function()
  print(count >= 4 and "timers ok" or ("timers " .. count))
end)
cancel(after(0.1, function()
  print("cancel did not work")
end))
