-- ExampleCustom：开局播报（与内置 scripts 一并加载）
function OnGameStart()
  if type(ra2) ~= "table" or type(ra2.eva) ~= "function" then return end
  -- 仅在任务 LineId 风格不保证可读时仍安全：eva 对玩家 0
  ra2.eva(0, "ExampleCustom script OnGameStart")
end
