--!strict
--[[
	Example 1: choosing what can be welded

	Put this Script in ServerScriptService (with the welder installed).

	The welder decides "is this metal?" in WelderShared.checkMetal:
	  1. a part with the attribute Weldable = false is never weldable
	  2. a part with Weldable = true is always weldable
	  3. otherwise its Material must be in Config.MetalMaterials
	This example uses rules 1 and 2 so you never have to edit the welder.
	It builds three parts 20 studs left of the practice bench and prints
	what the welder thinks of each one in the Output window.
]]

local CollectionService = game:GetService("CollectionService")
local ReplicatedStorage = game:GetService("ReplicatedStorage")

local WelderShared = require(ReplicatedStorage:WaitForChild("WelderShared"))
local Config = WelderShared.Config

local ORIGIN = Config.DemoPosition + Vector3.new(-20, 0, 0)

local function makePart(name: string, size: Vector3, offset: Vector3, material: Enum.Material, color: Color3): Part
	local part = Instance.new("Part")
	part.Name = name
	part.Size = size
	part.CFrame = CFrame.new(ORIGIN + offset)
	part.Material = material
	part.Color = color
	part.Anchored = true
	part.Parent = workspace
	return part
end

-- 1. Metal, but it must never be welded: a gas bottle. Welding a pressurised
--    cylinder is dangerous in real life, so the arc refuses here too.
local bottle = makePart("Gas Bottle", Vector3.new(1.2, 3, 1.2), Vector3.new(0, 1.5, 0), Enum.Material.Metal, Color3.fromRGB(40, 90, 160))
bottle:SetAttribute(Config.WeldableAttribute, false)

-- 2. Painted steel: the material is SmoothPlastic (it looks painted), but
--    underneath it's steel, so it's allowed.
local painted = makePart("Painted Steel Beam", Vector3.new(4, 0.6, 0.6), Vector3.new(3, 0.3, 0), Enum.Material.SmoothPlastic, Color3.fromRGB(230, 180, 30))
painted:SetAttribute(Config.WeldableAttribute, true)

-- 3. A whole group at once: every part tagged "Steel" becomes weldable,
--    including parts tagged later (for example, ones a player builds).
--    Add the tag in Studio with the Tag Editor (View > Tags), or in code:
local plate = makePart("Tagged Plate", Vector3.new(2, 0.2, 2), Vector3.new(3, 0.7, 2), Enum.Material.Slate, Color3.fromRGB(90, 90, 95))
plate:AddTag("Steel")

local function markSteel(instance: Instance)
	if instance:IsA("BasePart") then
		instance:SetAttribute(Config.WeldableAttribute, true)
	end
end
for _, instance in CollectionService:GetTagged("Steel") do
	markSteel(instance)
end
CollectionService:GetInstanceAddedSignal("Steel"):Connect(markSteel)

-- See the rule working: this is the exact check the welder runs.
for _, part in { bottle, painted, plate } do
	local ok, factor, note = WelderShared.checkMetal(part)
	print(string.format("[Example 1] %-20s weldable=%s quality x%.2f %s", part.Name, tostring(ok), factor, note or ""))
end

--[[
	Try this:
	  * Make Foil never weldable: in WelderShared, delete the Foil line from
	    Config.MetalMaterials. (Changing Config from a Script only changes the
	    server's copy; the client would still show a green marker. That's why
	    attributes, which replicate, are used above.)
	  * Give each part a NumberValue for its own quality factor and use it in
	    checkMetal.
]]
