-- A restoration pack: the files live next to this script as <archive>.gfs/<path in the game>, and filelist.txt says which ones.
-- Real files would be, for instance:   characters-foits|characters/filia.foit      characters-art|characters/filia.art
--                                       characters-art-pt|characters/filia.art-pt    sprites|sprites/filia_1p.dds
-- (.foit / .art / .art-pt are taken here because a rule NAMES them.)  The game's own files are never written to.
ModInfo = { ModName = "Example-Restoration", Version = 1, Author = "you", Desc = "layout example" }
Mod = {}

function Mod.install()
  -- "\\mods\\<anything>\\" is the pack's own folder, whatever it is called
  local base = CCLib.work_dir .. "\\mods\\Example-Restoration\\"
  local list = io.open(base .. "filelist.txt", "rb")
  if not list then return false, "filelist.txt is missing" end
  local count = 0
  for line in list:lines() do
    line = (line:gsub("\r", ""))
    if line ~= "" and line:sub(1, 1) ~= "#" then
      local archive, entry = line:match("^([^|]+)|(.+)$")
      if archive then
        local path = base .. archive .. ".gfs\\" .. (entry:gsub("/", "\\"))
        local probe = io.open(path, "rb")
        if not probe then return false, "missing file: " .. path end
        probe:close()
        if not CCLib.gfsAddFile(archive, entry, path) then return false, "rejected: " .. entry end
        count = count + 1
      end
    end
  end
  list:close()
  if not CCLib.gfsCommitChanges() then return false, "commit failed" end
  return count > 0
end
