--!strict
--[[
	Example 5: welding in code, and a drop test

	Put this Script in ServerScriptService (with the welder installed).

	Sometimes you want things already welded when the game starts: a
	trailer, a gate, a bridge. `weldInCode` below makes a joint exactly
	like the welder does (same tag, same attributes, same strength
	formula) and lays a cold bead along the seam so it looks welded. The
	welder's load checks then treat it like any other weld, and the
	grinder from Example 3 can grind it off.

	The drop test: two identical pairs of steel blocks hang in the air.
	One pair is held by a single tack weld, the other by full welds along
	both edges. Hold E on "Drop" and both fall onto a concrete pad. The
	welder estimates the impact force on each joint (mass x sudden
	deceleration), so the tack cracks and the full weld should hold.
	(The force depends on how the physics engine stops the blocks in the
	landing frames, so the exact numbers vary from drop to drop.)
]]

local ReplicatedStorage = game:GetService("ReplicatedStorage")

local WelderShared = require(ReplicatedStorage:WaitForChild("WelderShared"))
local Config = WelderShared.Config

local ORIGIN = Config.DemoPosition + Vector3.new(-20, 0, -18)
local DROP_HEIGHT = 20

---------------------------------------------------------------------------
-- Welding in code
---------------------------------------------------------------------------

-- Lay a cold bead from `from` to `to` on `base`, bulging out along `normal`.
-- Same ripple shape as the welder (see discForCap in WelderShared).
local function layBead(base: BasePart, from: Vector3, to: Vector3, normal: Vector3)
	local folder = base:FindFirstChild(Config.BeadFolderName)
	if not folder then
		folder = Instance.new("Folder")
		folder.Name = Config.BeadFolderName
		folder.Parent = base
	end
	local direction = (to - from).Unit
	local radius, depth = WelderShared.discForCap(Config.BeadWidth, Config.BeadHeight)
	local length = (to - from).Magnitude
	local count = math.max(1, math.floor(length / Config.RippleSpacing))
	for i = 0, count do
		local point = from:Lerp(to, i / count)
		local ripple = Instance.new("Part")
		ripple.Name = "WeldRipple"
		ripple.Shape = Enum.PartType.Cylinder
		ripple.Size = Vector3.new(Config.RippleThickness, radius * 2, radius * 2)
		ripple.CFrame = CFrame.fromMatrix(point - normal * depth, direction, normal)
		ripple.Material = Config.BeadMaterial
		ripple.Color = Config.BeadColor
		ripple.Anchored = false
		ripple.CanCollide = false
		ripple.CanTouch = false
		ripple.CanQuery = false
		ripple.Massless = true
		ripple.CastShadow = false
		-- Tagged so tools like the grinder find it. No DepositTime
		-- attribute, so clients show it cold instead of glowing.
		ripple:AddTag(Config.BeadTag)
		ripple.Parent = folder
		local weld = Instance.new("WeldConstraint")
		weld.Part0 = ripple
		weld.Part1 = base
		weld.Parent = ripple
	end
end

-- Join `a` and `b` with a welder joint. `seams` is a list of bead lines
-- ({from, to, normal}) on `a`; their total length sets the strength.
type Seam = { from: Vector3, to: Vector3, normal: Vector3 }
local function weldInCode(a: BasePart, b: BasePart, seams: { Seam }, quality: number): WeldConstraint
	local length, centre = 0, Vector3.zero
	for _, seam in seams do
		local l = (seam.to - seam.from).Magnitude
		length += l
		centre += (seam.from + seam.to) / 2 * l
		layBead(a, seam.from, seam.to, seam.normal)
	end
	centre /= math.max(length, 1e-6)

	local force, torque = WelderShared.jointStrength(length, quality)
	local joint = Instance.new("WeldConstraint")
	joint.Name = "WeldJoint"
	joint.Part0 = a
	joint.Part1 = b
	joint:SetAttribute("BeadLength", length)
	joint:SetAttribute("Quality", quality)
	joint:SetAttribute("ForceRating", force)
	joint:SetAttribute("TorqueRating", torque)
	joint:SetAttribute("LocalPoint", a.CFrame:PointToObjectSpace(centre))
	joint:AddTag(Config.JointTag)
	joint.Parent = a
	return joint
end

---------------------------------------------------------------------------
-- The drop test
---------------------------------------------------------------------------
local function block(parent: Instance, name: string, size: Vector3, position: Vector3, material: Enum.Material, anchored: boolean): Part
	local part = Instance.new("Part")
	part.Name = name
	part.Size = size
	part.CFrame = CFrame.new(position)
	part.Material = material
	part.Color = if material == Enum.Material.Metal then Color3.fromRGB(140, 142, 146) else Color3.fromRGB(160, 160, 155)
	part.Anchored = anchored
	part.TopSurface = Enum.SurfaceType.Smooth
	part.BottomSurface = Enum.SurfaceType.Smooth
	part.Parent = parent
	return part
end

local function label(part: BasePart, text: string): TextLabel
	local gui = Instance.new("BillboardGui")
	gui.Size = UDim2.fromOffset(220, 50)
	gui.StudsOffsetWorldSpace = Vector3.new(0, 3, 0)
	gui.AlwaysOnTop = true
	gui.Parent = part
	local textLabel = Instance.new("TextLabel")
	textLabel.Size = UDim2.fromScale(1, 1)
	textLabel.BackgroundTransparency = 0.3
	textLabel.BackgroundColor3 = Color3.fromRGB(20, 20, 25)
	textLabel.TextColor3 = Color3.new(1, 1, 1)
	textLabel.Font = Enum.Font.GothamBold
	textLabel.TextScaled = true
	textLabel.Text = text
	textLabel.Parent = gui
	return textLabel
end

local function build()
	local model = Instance.new("Model")
	model.Name = "DropTest"

	-- Concrete so it's not weldable and the specimens have something to hit.
	block(model, "Concrete Pad", Vector3.new(10, 1, 6), ORIGIN + Vector3.new(0, 0.5, 0), Enum.Material.Concrete, true)

	-- A specimen: a 2 x 0.5 x 1 block with another one on top.
	local function specimen(name: string, x: number): (Part, Part)
		local y = DROP_HEIGHT
		local bottom = block(model, name .. " Bottom", Vector3.new(2, 0.5, 1), ORIGIN + Vector3.new(x, y, 0), Enum.Material.Metal, true)
		local top = block(model, name .. " Top", Vector3.new(2, 0.5, 1), ORIGIN + Vector3.new(x, y + 0.5, 0), Enum.Material.Metal, true)
		return bottom, top
	end

	-- The seam where the top block sits on the bottom block, on the front
	-- (zSide = 1) or back (zSide = -1) face. The faces are flush, so the
	-- bead bulges straight out of the face.
	local function edge(bottom: Part, zSide: number, fromX: number, toX: number): Seam
		local cf = bottom.CFrame
		local corner = Vector3.new(0, bottom.Size.Y / 2, zSide * bottom.Size.Z / 2)
		return {
			from = cf:PointToWorldSpace(corner + Vector3.new(fromX, 0, 0)),
			to = cf:PointToWorldSpace(corner + Vector3.new(toX, 0, 0)),
			normal = cf:VectorToWorldSpace(Vector3.zAxis * zSide),
		}
	end

	local tackBottom, tackTop = specimen("Tack", -2.5)
	local tackJoint = weldInCode(tackBottom, tackTop, { edge(tackBottom, 1, -0.15, 0.15) }, 1)
	local tackLabel = label(tackTop, "Tack weld (0.3 studs)")

	local fullBottom, fullTop = specimen("Full", 2.5)
	local fullJoint = weldInCode(fullBottom, fullTop, { edge(fullBottom, 1, -1, 1), edge(fullBottom, -1, -1, 1) }, 0.95)
	local fullLabel = label(fullTop, "Full welds (4 studs)")

	local button = block(model, "Drop", Vector3.new(0.8, 1, 0.8), ORIGIN + Vector3.new(0, 1.5, 4), Enum.Material.Neon, true)
	button.Color = Color3.fromRGB(220, 160, 40)
	local prompt = Instance.new("ProximityPrompt")
	prompt.ActionText = "Drop"
	prompt.ObjectText = "Drop test"
	prompt.HoldDuration = 0.5
	prompt.RequiresLineOfSight = false
	prompt.Parent = button

	model.Parent = workspace

	prompt.Triggered:Connect(function()
		prompt.Enabled = false
		for _, part in { tackBottom, tackTop, fullBottom, fullTop } do
			part.Anchored = false
		end
		task.wait(3)
		local function result(joint: WeldConstraint, textLabel: TextLabel, name: string)
			local ok = joint.Parent ~= nil
			textLabel.Text = string.format("%s: %s", name, if ok then "HELD" else "CRACKED")
			print(string.format("[Example 5] %s %s", name, if ok then "held" else "cracked on impact"))
		end
		result(tackJoint, tackLabel, "Tack weld")
		result(fullJoint, fullLabel, "Full welds")
		task.wait(6)
		model:Destroy()
		build()
	end)
end

build()

--[[
	Try this:
	  * Build a small trailer with weldInCode, then drive it into a wall.
	  * Raise DROP_HEIGHT until even the full weld cracks, then work out the
	    force from the numbers in checkImpact (WelderServer) to see why.
	  * Weld with quality 0.3 instead of 0.95 and compare.
]]
