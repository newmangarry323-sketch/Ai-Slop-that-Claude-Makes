--!strict
--[[
	Example 2: reacting when welds are made, get stronger, or break

	Put this Script in ServerScriptService (with the welder installed).

	Every joint the welder makes is a WeldConstraint with the tag
	"WelderJoint" (Config.JointTag) and these attributes:
	  BeadLength    studs of bead on the seam
	  Quality       average quality, 0..1
	  ForceRating   load it can carry before it breaks
	  TorqueRating  leverage it can carry before it breaks
	  WeldedBy      UserId of the player who tacked it
	  LocalPoint    centre of the bead, in Part0's space
	So other scripts can follow welds with CollectionService and attribute
	signals, without changing the welder.

	This example adds two leaderboard columns: "Welds" (joints you made)
	and "Best" (strongest joint you made, as a force rating).
]]

local CollectionService = game:GetService("CollectionService")
local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")

local WelderShared = require(ReplicatedStorage:WaitForChild("WelderShared"))
local Config = WelderShared.Config

-- Leaderboard: Roblox shows IntValues/NumberValues inside a folder named
-- "leaderstats" in the player list.
local function getStat(player: Player, name: string): IntValue
	local folder = player:FindFirstChild("leaderstats")
	if not folder then
		folder = Instance.new("Folder")
		folder.Name = "leaderstats"
		folder.Parent = player
	end
	local stat = (folder :: Instance):FindFirstChild(name)
	if not stat then
		local newStat = Instance.new("IntValue")
		newStat.Name = name
		newStat.Parent = folder
		stat = newStat
	end
	return stat :: IntValue
end

Players.PlayerAdded:Connect(function(player)
	getStat(player, "Welds")
	getStat(player, "Best")
end)
for _, player in Players:GetPlayers() do
	getStat(player, "Welds")
	getStat(player, "Best")
end

local function describe(joint: Instance): string
	if not joint:IsA("WeldConstraint") then
		return "?"
	end
	local a, b = joint.Part0, joint.Part1
	return string.format("%s + %s", if a then a.Name else "?", if b then b.Name else "?")
end

local function onJoint(joint: Instance)
	if not joint:IsA("WeldConstraint") then
		return
	end
	local userId = joint:GetAttribute("WeldedBy")
	local player = if typeof(userId) == "number" then Players:GetPlayerByUserId(userId) else nil
	print(string.format("[Example 2] New weld: %s (by %s)", describe(joint), player and player.Name or "unknown"))

	if player then
		getStat(player, "Welds").Value += 1
	end

	-- The joint gets stronger as more bead goes on. Attribute signals let us
	-- watch that without polling.
	joint:GetAttributeChangedSignal("ForceRating"):Connect(function()
		local force = joint:GetAttribute("ForceRating")
		if player and typeof(force) == "number" then
			local best = getStat(player, "Best")
			best.Value = math.max(best.Value, math.floor(force))
		end
	end)
end

for _, joint in CollectionService:GetTagged(Config.JointTag) do
	onJoint(joint)
end
CollectionService:GetInstanceAddedSignal(Config.JointTag):Connect(onJoint)

-- Removed = broken, ground off, or its parts were destroyed (e.g. the bench
-- was reset). The instance still has its attributes when this fires.
CollectionService:GetInstanceRemovedSignal(Config.JointTag):Connect(function(joint)
	local length = joint:GetAttribute("BeadLength")
	print(string.format(
		"[Example 2] Weld gone: %s (it had %.2f studs of bead)",
		describe(joint),
		if typeof(length) == "number" then length else 0
	))
end)

--[[
	Try this:
	  * Give a badge or coins for a weld with Quality above 0.9 and more
	    than 2 studs of bead.
	  * Show a weld's strength above it: a BillboardGui on an Attachment at
	    Part0.CFrame:PointToWorldSpace(LocalPoint).
]]
