--!strict
--[[
	Example 3: an angle grinder that removes welds

	Put this Script in ServerScriptService (with the welder installed).

	Every player gets a "Grinder" tool. Hold click with the disc against a
	weld and it grinds the ripples away, throwing sparks. Grinding a bead on
	a seam takes that much length off the joint, so the joint gets weaker;
	grind it all off and the parts come apart.

	What it shows:
	  * finding beads: they have the tag "WelderBead" (Config.BeadTag) and
	    live in a folder named "WeldBeads" inside the part they sit on
	  * changing a joint's strength with WelderShared.jointStrength, the
	    same formula the welder uses
	  * a tool driven entirely from the server (Tool.Activated fires on the
	    server too), so no LocalScript is needed. The catch: the server
	    can't see the mouse, so the grinder works on whatever is in front
	    of the disc, not on where you point.
]]

local CollectionService = game:GetService("CollectionService")
local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local RunService = game:GetService("RunService")
local StarterPack = game:GetService("StarterPack")

local WelderShared = require(ReplicatedStorage:WaitForChild("WelderShared"))
local Config = WelderShared.Config

local GRIND_RADIUS = 1.0 -- studs around the disc
local RIPPLES_PER_TICK = 3 -- how fast it grinds
local TICK = 0.1 -- seconds

---------------------------------------------------------------------------
-- The tool
---------------------------------------------------------------------------
local function buildGrinder(): Tool
	local tool = Instance.new("Tool")
	tool.Name = "Grinder"
	tool.ToolTip = "Angle grinder: hold click against a weld"
	tool.CanBeDropped = false

	local handle = Instance.new("Part")
	handle.Name = "Handle"
	handle.Size = Vector3.new(0.4, 0.4, 1.4)
	handle.Material = Enum.Material.SmoothPlastic
	handle.Color = Color3.fromRGB(30, 110, 60)
	handle.CanCollide = false
	handle.Massless = true
	handle.Parent = tool

	-- The disc sits at the front of the handle. SmoothPlastic so the
	-- welder never treats it as metal.
	local disc = Instance.new("Part")
	disc.Name = "Disc"
	disc.Shape = Enum.PartType.Cylinder
	disc.Size = Vector3.new(0.08, 0.9, 0.9)
	disc.Material = Enum.Material.SmoothPlastic
	disc.Color = Color3.fromRGB(70, 70, 75)
	disc.CanCollide = false
	disc.Massless = true
	disc.Parent = tool
	local weld = Instance.new("Weld")
	weld.Part0 = handle
	weld.Part1 = disc
	weld.C0 = CFrame.new(0, 0, -0.85)
	weld.Parent = handle

	local sparkPoint = Instance.new("Attachment")
	sparkPoint.Name = "SparkPoint"
	sparkPoint.Position = Vector3.new(0, -0.45, 0)
	sparkPoint.Parent = disc

	local sparks = Instance.new("ParticleEmitter")
	sparks.Name = "Sparks"
	sparks.Enabled = false
	sparks.Rate = 200
	sparks.Lifetime = NumberRange.new(0.2, 0.5)
	sparks.Speed = NumberRange.new(15, 30)
	sparks.SpreadAngle = Vector2.new(20, 20)
	sparks.Acceleration = Vector3.new(0, -workspace.Gravity * 0.3, 0)
	sparks.LightEmission = 1
	sparks.LightInfluence = 0
	sparks.Size = NumberSequence.new({ NumberSequenceKeypoint.new(0, 0.06), NumberSequenceKeypoint.new(1, 0) })
	sparks.Color = ColorSequence.new(Color3.fromRGB(255, 220, 140), Color3.fromRGB(255, 120, 30))
	sparks.Orientation = Enum.ParticleOrientation.VelocityParallel
	sparks.EmissionDirection = Enum.NormalId.Bottom
	sparks.Parent = sparkPoint

	return tool
end

local template = buildGrinder()
template.Parent = StarterPack
for _, player in Players:GetPlayers() do
	local backpack = player:FindFirstChildOfClass("Backpack")
	if backpack and not backpack:FindFirstChild("Grinder") then
		template:Clone().Parent = backpack
	end
end

---------------------------------------------------------------------------
-- Which tools are grinding right now
---------------------------------------------------------------------------
local grinding: { [Tool]: boolean } = {}

local function hookTool(tool: Tool)
	if tool:GetAttribute("GrinderHooked") then
		return
	end
	tool:SetAttribute("GrinderHooked", true)
	tool.Activated:Connect(function()
		grinding[tool] = true
	end)
	tool.Deactivated:Connect(function()
		grinding[tool] = nil
	end)
	tool.Unequipped:Connect(function()
		grinding[tool] = nil
	end)
	tool.Destroying:Connect(function()
		grinding[tool] = nil
	end)
end

local function watchCharacter(character: Model)
	character.ChildAdded:Connect(function(child)
		if child:IsA("Tool") and child.Name == "Grinder" then
			hookTool(child)
		end
	end)
end
Players.PlayerAdded:Connect(function(player)
	player.CharacterAdded:Connect(watchCharacter)
end)
for _, player in Players:GetPlayers() do
	player.CharacterAdded:Connect(watchCharacter)
	if player.Character then
		watchCharacter(player.Character)
	end
end

---------------------------------------------------------------------------
-- Grinding
---------------------------------------------------------------------------

-- The part a bead sits on: bead -> WeldBeads folder -> part.
local function beadBase(bead: BasePart): BasePart?
	local folder = bead.Parent
	local base = folder and folder.Parent
	if folder and folder.Name == Config.BeadFolderName and base and base:IsA("BasePart") then
		return base
	end
	return nil
end

-- Take one ripple's worth of length off the joint whose seam it was on.
local function weakenJointFor(ripple: BasePart, base: BasePart)
	local reach = Config.MaxGap + Config.BeadWidth
	for _, joint in CollectionService:GetTagged(Config.JointTag) do
		if joint:IsA("WeldConstraint") and (joint.Part0 == base or joint.Part1 == base) then
			local partner = if joint.Part0 == base then joint.Part1 else joint.Part0
			if partner then
				local closest = WelderShared.closestPoint(partner, ripple.Position)
				if (closest - ripple.Position).Magnitude <= reach then
					local length = (joint:GetAttribute("BeadLength") :: number?) or 0
					local quality = (joint:GetAttribute("Quality") :: number?) or 1
					length -= Config.RippleSpacing
					if length <= 0.05 then
						joint:Destroy() -- ground clean through: the parts are free
					else
						local force, torque = WelderShared.jointStrength(length, quality)
						joint:SetAttribute("BeadLength", length)
						joint:SetAttribute("ForceRating", force)
						joint:SetAttribute("TorqueRating", torque)
					end
					return
				end
			end
		end
	end
end

local function grindOnce(tool: Tool): boolean
	local disc = tool:FindFirstChild("Disc")
	if not disc or not disc:IsA("BasePart") then
		return false
	end
	local centre = disc.Position

	-- Nearest beads first.
	local nearby: { { bead: BasePart, distance: number } } = {}
	for _, bead in CollectionService:GetTagged(Config.BeadTag) do
		if bead:IsA("BasePart") then
			local distance = (bead.Position - centre).Magnitude
			if distance <= GRIND_RADIUS then
				table.insert(nearby, { bead = bead, distance = distance })
			end
		end
	end
	table.sort(nearby, function(x, y)
		return x.distance < y.distance
	end)

	for i = 1, math.min(RIPPLES_PER_TICK, #nearby) do
		local bead = nearby[i].bead
		local base = beadBase(bead)
		if base and bead.Name == "WeldRipple" then
			weakenJointFor(bead, base)
		end
		bead:Destroy()
	end
	return #nearby > 0
end

local timer = 0
RunService.Heartbeat:Connect(function(dt)
	timer += dt
	if timer < TICK then
		return
	end
	timer = 0
	for _, player in Players:GetPlayers() do
		local character = player.Character
		local tool = character and character:FindFirstChild("Grinder")
		if tool and tool:IsA("Tool") then
			local cutting = grinding[tool] == true and grindOnce(tool)
			local sparks = tool:FindFirstChild("Sparks", true)
			if sparks and sparks:IsA("ParticleEmitter") then
				sparks.Enabled = cutting
			end
		end
	end
end)

--[[
	Try this:
	  * Leave a bright, ground-flat patch where a bead was removed.
	  * Make grinding slower on good welds (use the joint's Quality).
	  * Use a RemoteEvent and the mouse position, like the welder does, so
	    the grinder works where you point instead of in front of the disc.
]]
