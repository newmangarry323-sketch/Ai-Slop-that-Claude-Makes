--!strict
--[[
	WelderShared (ModuleScript, goes in ReplicatedStorage)

	Settings and maths used by both the server script and the client script.
	Nothing in here creates or changes instances, so it is safe to require
	from either side.

	The numbers are "game scale": a real MIG welder moves at a few millimetres
	per second, which would be far too slow for a game. The *relationships*
	are the real ones (see README.md for the sources):
	  * too fast  -> narrow, tall, ropy bead with poor fusion (weak)
	  * too slow  -> wide, piled-up bead that overheats the metal
	  * long arc  -> flatter, wider bead, more spatter, porosity (weak)
	  * rust      -> porosity (weaker)
	  * thin foil -> burn-through (much weaker)
	  * steel stops glowing at the Draper point, about 525 C
]]

local WelderShared = {}

---------------------------------------------------------------------------
-- Settings. Change these to tune the welder.
---------------------------------------------------------------------------
local Config = {
	ToolName = "Welder",

	-- Only parts made of these materials can be welded. The number is a
	-- quality multiplier and the text is shown to the player.
	MetalMaterials = {
		[Enum.Material.Metal] = { factor = 1.0, note = nil },
		[Enum.Material.DiamondPlate] = { factor = 1.0, note = nil },
		[Enum.Material.CorrodedMetal] = { factor = 0.7, note = "Rusty metal: porosity, weaker weld" },
		[Enum.Material.Foil] = { factor = 0.35, note = "Foil is too thin: burn-through, weak weld" },
	} :: { [Enum.Material]: { factor: number, note: string? } },

	-- A part can override the material rule with a boolean attribute named
	-- "Weldable" (true = always weldable, false = never weldable).
	WeldableAttribute = "Weldable",

	-- Parts thinner than this (studs) are "sheet metal" and burn through.
	ThinMetalLimit = 0.15,
	ThinMetalFactor = 0.5,

	-- Reach: distance from the character to the arc (studs).
	IdealReach = 5, -- full quality up to here
	MaxReach = 9, -- no arc beyond this

	-- Travel speed (studs per second) that gives a good bead.
	MinGoodSpeed = 0.6,
	IdealSpeed = 1.4,
	MaxGoodSpeed = 2.6,

	-- Bead shape at the ideal speed and a short arc (studs).
	BeadWidth = 0.32,
	BeadHeight = 0.09,
	RippleSpacing = 0.07, -- distance between ripples ("stacked dimes")
	RippleThickness = 0.12, -- thickness of each ripple disc
	RippleTilt = math.rad(22), -- how far each ripple leans back
	MaxJump = 1.2, -- a bigger jump between samples starts a new bead
	DwellInterval = 0.3, -- holding still: puddle grows every N seconds
	MaxDwellGrowth = 1.8, -- puddle can grow to this times the bead width

	-- Joining parts.
	MaxGap = 0.25, -- biggest gap (studs) a bead can bridge
	SeamSnap = true, -- pull the bead onto the seam when one is close
	TackLength = 0.3, -- bead length (studs) on a seam before parts join
	ProbeHeight = 0.08, -- height above the surface used to find corners

	-- Strength of a finished joint. A joint breaks when the load on it
	-- exceeds these numbers (Roblox force units: mass * studs / s^2).
	StrengthPerStud = 20000, -- force rating per stud of good bead
	TorqueArmBase = 0.25, -- torque rating = force rating * (base + length * perStud)
	TorqueArmPerStud = 0.5,
	ImpactGrace = 1.5, -- accelerations up to this many g are ignored

	-- Heat. Newton's law of cooling: T(t) = Tair + (T0 - Tair) * e^(-k t)
	StartTemperature = 1450, -- deg C, freshly deposited bead
	AmbientTemperature = 20,
	CoolingRate = 0.125, -- k in 1/s; ~8 s until the glow disappears
	GlowTemperature = 525, -- Draper point: below this metal no longer glows
	ColdTemperature = 120, -- below this the bead has its final colour

	-- Colours of a cooled bead.
	BeadColor = Color3.fromRGB(132, 130, 127),
	OxideColor = Color3.fromRGB(78, 74, 92), -- blue-grey heat tint while cooling
	BeadMaterial = Enum.Material.Metal,

	-- Spatter (small balls of metal stuck near the bead).
	SpatterChance = 0.04, -- per ripple with a short arc
	SpatterChanceLongArc = 0.35, -- extra chance per ripple with the longest arc

	-- Optional arc sound. Paste the id of a looping sound from the Creator
	-- Store, e.g. "rbxassetid://1234567890". Left empty on purpose: this
	-- project does not ship any audio.
	ArcSoundId = "",

	-- Network.
	SendRate = 1 / 20, -- seconds between points sent by the client
	ArcTimeout = 0.35, -- arc goes out if no point arrives for this long

	-- Build a small practice bench with metal and non-metal parts.
	SpawnDemo = true,
	DemoPosition = Vector3.new(0, 0, -16),

	-- Names and tags shared by client and server.
	RemoteName = "WelderRemote",
	BeadTag = "WelderBead",
	ArcTag = "WelderArc",
	JointTag = "WelderJoint",
	ArcFolderName = "WelderArcs",
	BeadFolderName = "WeldBeads",
}
WelderShared.Config = Config

---------------------------------------------------------------------------
-- Small helpers
---------------------------------------------------------------------------
local function clamp(x: number, lo: number, hi: number): number
	if x < lo then
		return lo
	elseif x > hi then
		return hi
	end
	return x
end
WelderShared.clamp = clamp

local function lerp(a: number, b: number, t: number): number
	return a + (b - a) * t
end

-- True for parts that belong to a character (anything under a Humanoid model).
function WelderShared.isCharacterPart(part: Instance): boolean
	local model = part:FindFirstAncestorOfClass("Model")
	while model do
		if model:FindFirstChildOfClass("Humanoid") then
			return true
		end
		model = model:FindFirstAncestorOfClass("Model")
	end
	return false
end

-- Is this part metal? Returns (ok, qualityFactor, reasonOrNote).
function WelderShared.checkMetal(part: Instance?): (boolean, number, string?)
	if part == nil or not part:IsA("BasePart") then
		return false, 0, "Nothing to weld"
	end
	local basePart = part :: BasePart
	if basePart:HasTag(Config.BeadTag) then
		return false, 0, nil
	end
	if WelderShared.isCharacterPart(basePart) then
		return false, 0, "You can't weld people"
	end

	local override = basePart:GetAttribute(Config.WeldableAttribute)
	local info = Config.MetalMaterials[basePart.Material]
	if override == false then
		return false, 0, basePart.Name .. " can't be welded"
	end
	if info == nil and override ~= true then
		-- Enum names read well enough ("Wood", "Plastic", "Concrete"...).
		return false, 0, basePart.Material.Name .. " isn't metal, so no arc"
	end

	local factor = if info then info.factor else 1
	local note = if info then info.note else nil
	local size = basePart.Size
	local thickness = math.min(size.X, size.Y, size.Z)
	if thickness < Config.ThinMetalLimit then
		factor *= Config.ThinMetalFactor
		note = note or "Thin sheet: burn-through risk, weaker weld"
	end
	return true, factor, note
end

---------------------------------------------------------------------------
-- Weld quality
---------------------------------------------------------------------------

-- Quality from travel speed. Returns (quality 0..1, label).
function WelderShared.speedQuality(speed: number): (number, string)
	if speed > Config.MaxGoodSpeed then
		local over = (speed - Config.MaxGoodSpeed) / (Config.MaxGoodSpeed * 1.2)
		return clamp(1 - over, 0.15, 1), "Too fast: thin, ropy bead, poor fusion"
	elseif speed < Config.MinGoodSpeed then
		return clamp(0.6 + 0.4 * speed / Config.MinGoodSpeed, 0.6, 1), "Too slow: puddle too big, overheating"
	end
	return 1, "Good travel speed"
end

-- How "long" the arc is, 0 (ideal) .. 1 (maximum reach).
function WelderShared.arcLongness(distance: number): number
	return clamp((distance - Config.IdealReach) / (Config.MaxReach - Config.IdealReach), 0, 1)
end

-- Quality from arc length. Returns (quality, label or nil).
function WelderShared.reachQuality(distance: number): (number, string?)
	local longness = WelderShared.arcLongness(distance)
	if longness > 0 then
		return 1 - 0.5 * longness, "Arc too long: spatter and porosity"
	end
	return 1, nil
end

-- Bead width and height for a travel speed and arc length.
-- Slow travel puts more heat and metal in one place: wider and taller.
-- A long arc spreads the heat: wider and flatter.
function WelderShared.beadShape(speed: number, longness: number): (number, number)
	local heat = clamp(Config.IdealSpeed / math.max(speed, 0.05), 0.4, 2.5)
	local arcSpread = 1 + 0.4 * longness
	local width = Config.BeadWidth * heat ^ 0.45 * arcSpread
	local height = Config.BeadHeight * heat ^ 0.3 / arcSpread
	return width, height
end

-- A ripple is a thin disc (cylinder part) sunk into the metal so that only
-- a cap sticks out. For a cap of chord `width` and height `height`, the
-- circle has radius R = (w^2/4 + h^2) / (2h) and its centre sits R - h
-- below the surface. Returns (radius, depth).
function WelderShared.discForCap(width: number, height: number): (number, number)
	local h = math.max(height, 0.005)
	local radius = (width * width / 4 + h * h) / (2 * h)
	return radius, radius - h
end

-- Keep a flat bead's disc inside the plate. The part of the disc below the
-- surface reaches w^2 / (4h) deep, so a thin plate needs a taller (rounder)
-- bead, and if even a half-disc is too deep, a narrower one.
-- (Sheet thinner than Config.ThinMetalLimit is skipped by the caller: there
-- the bead pushing out of the back is burn-through, which is realistic.)
function WelderShared.fitCapToThickness(width: number, height: number, thickness: number): (number, number)
	local limit = thickness * 0.9
	if limit <= 0 then
		return width, height
	end
	local minHeight = width * width / (4 * limit)
	if minHeight <= width / 2 then
		return width, math.max(height, minHeight)
	end
	return 2 * limit, limit
end

-- Fillet (inside corner) ripple: radius and how far the centre sits behind
-- the corner. It reaches about 0.54 * width into each plate, so the width
-- is capped for thin plates in the same way.
function WelderShared.filletDisc(width: number, thickness: number?): (number, number)
	if thickness then
		width = math.min(width, thickness * 0.9 / 0.54)
	end
	return width * 0.5, width * 0.05
end

-- Thickness of a part measured along a direction (exact for a block when
-- the direction is along one of its axes).
function WelderShared.thicknessAlong(part: BasePart, direction: Vector3): number
	local d = part.CFrame:VectorToObjectSpace(direction)
	local size = part.Size
	return math.abs(d.X) * size.X + math.abs(d.Y) * size.Y + math.abs(d.Z) * size.Z
end

-- Strength of a joint from its bead length and average quality.
function WelderShared.jointStrength(length: number, quality: number): (number, number)
	local force = Config.StrengthPerStud * length * quality
	local torque = force * (Config.TorqueArmBase + length * Config.TorqueArmPerStud)
	return force, torque
end

---------------------------------------------------------------------------
-- Heat
---------------------------------------------------------------------------

-- Newton's law of cooling.
function WelderShared.temperatureAt(age: number): number
	local air = Config.AmbientTemperature
	return air + (Config.StartTemperature - air) * math.exp(-Config.CoolingRate * math.max(age, 0))
end

-- Approximate glow colours of hot steel (from blacksmith heat-colour charts).
local GLOW: { { number } } = {
	{ 525, 70, 0, 0 },
	{ 600, 120, 12, 0 },
	{ 700, 170, 30, 0 },
	{ 815, 215, 55, 5 },
	{ 900, 240, 95, 15 },
	{ 1000, 255, 140, 35 },
	{ 1100, 255, 185, 80 },
	{ 1200, 255, 220, 135 },
	{ 1350, 255, 245, 210 },
}

-- Colour of steel at a temperature. Returns (color, isGlowing).
-- `finalColor` is the colour the bead has once it is cold.
function WelderShared.heatColor(temperature: number, finalColor: Color3?): (Color3, boolean)
	local cold = finalColor or Config.BeadColor
	if temperature < Config.GlowTemperature then
		-- No visible glow: fade from the oxide tint to the final colour.
		local t = clamp(
			(Config.GlowTemperature - temperature) / (Config.GlowTemperature - Config.ColdTemperature),
			0,
			1
		)
		return Config.OxideColor:Lerp(cold, t), false
	end
	for i = 2, #GLOW do
		local a, b = GLOW[i - 1], GLOW[i]
		if temperature <= b[1] then
			local t = (temperature - a[1]) / (b[1] - a[1])
			return Color3.fromRGB(lerp(a[2], b[2], t), lerp(a[3], b[3], t), lerp(a[4], b[4], t)), true
		end
	end
	local top = GLOW[#GLOW]
	return Color3.fromRGB(top[2], top[3], top[4]), true
end

---------------------------------------------------------------------------
-- Geometry
---------------------------------------------------------------------------

-- Two unit vectors perpendicular to `n` and to each other.
function WelderShared.tangents(n: Vector3): (Vector3, Vector3)
	local helper = if math.abs(n.Y) < 0.9 then Vector3.yAxis else Vector3.xAxis
	local u = n:Cross(helper).Unit
	local v = n:Cross(u).Unit
	return u, v
end

-- Closest point on a part to `point`. Exact for blocks, an approximation
-- (the bounding box) for other shapes; a sphere is handled exactly.
function WelderShared.closestPoint(part: BasePart, point: Vector3): Vector3
	if part:IsA("Part") and (part :: Part).Shape == Enum.PartType.Ball then
		local radius = math.min(part.Size.X, part.Size.Y, part.Size.Z) / 2
		local offset = point - part.Position
		if offset.Magnitude <= radius then
			return point
		end
		return part.Position + offset.Unit * radius
	end
	local half = part.Size / 2
	local localPoint = part.CFrame:PointToObjectSpace(point)
	local clamped = Vector3.new(
		clamp(localPoint.X, -half.X, half.X),
		clamp(localPoint.Y, -half.Y, half.Y),
		clamp(localPoint.Z, -half.Z, half.Z)
	)
	return part.CFrame:PointToWorldSpace(clamped)
end

export type Seam = {
	point: Vector3, -- where the bead centre line goes (on the surface)
	normal: Vector3, -- direction the bead bulges out
	fillet: boolean, -- inside corner (fillet weld) instead of a flat bead
	partners: { BasePart }, -- other metal parts this bead touches
}

-- Work out where a bead laid at `hitPoint` on `base` goes and which other
-- metal parts it will join. `exclude` is a list of instances to ignore
-- (characters, effects).
function WelderShared.findSeam(base: BasePart, hitPoint: Vector3, hitNormal: Vector3, exclude: { Instance }): Seam
	local seam: Seam = { point = hitPoint, normal = hitNormal, fillet = false, partners = {} }
	local found: { [BasePart]: boolean } = {}

	local function addPartner(part: BasePart)
		if part ~= base and not found[part] and WelderShared.checkMetal(part) then
			found[part] = true
			table.insert(seam.partners, part)
		end
	end

	local rayParams = RaycastParams.new()
	rayParams.FilterType = Enum.RaycastFilterType.Exclude
	rayParams.FilterDescendantsInstances = exclude

	-- 1. Inside corners: look sideways from just above the surface. A wall
	--    standing on the plate (or the edge of a plate lying on it) makes a
	--    corner that gets a fillet weld.
	local reach = Config.MaxGap + Config.BeadWidth
	local origin = hitPoint + hitNormal * Config.ProbeHeight
	local u, v = WelderShared.tangents(hitNormal)
	local bestDistance = math.huge
	local bestWallNormal: Vector3? = nil
	for _, dir in { u, -u, v, -v, (u + v).Unit, (u - v).Unit, (-u + v).Unit, (-u - v).Unit } do
		local result = workspace:Raycast(origin, dir * reach, rayParams)
		if result and result.Instance ~= base and result.Instance:IsA("BasePart") then
			local wall = result.Instance :: BasePart
			-- The wall has to face back towards us, not be a sloped surface.
			if WelderShared.checkMetal(wall) and result.Normal:Dot(-dir) > 0.7 then
				addPartner(wall)
				-- Straight-line distance from the probe to the wall's plane.
				local wallDistance = result.Distance * dir:Dot(-result.Normal)
				if wallDistance < bestDistance then
					bestDistance = wallDistance
					bestWallNormal = result.Normal
					-- Corner line: on our surface, directly under the wall face.
					local flat = result.Normal - hitNormal * result.Normal:Dot(hitNormal)
					if flat.Magnitude > 0.01 then
						seam.point = hitPoint - flat.Unit * wallDistance
					end
				end
			end
		end
	end
	if bestWallNormal then
		seam.fillet = true
		seam.normal = (hitNormal + bestWallNormal).Unit
	end

	-- 2. Butt joints and anything else touching: parts within the gap.
	local overlap = OverlapParams.new()
	overlap.FilterType = Enum.RaycastFilterType.Exclude
	overlap.FilterDescendantsInstances = exclude
	local radius = Config.MaxGap + Config.BeadWidth / 2
	local nearestOffset: Vector3? = nil
	for _, part in workspace:GetPartBoundsInRadius(hitPoint, radius, overlap) do
		if part ~= base and not found[part] and WelderShared.checkMetal(part) then
			local closest = WelderShared.closestPoint(part, hitPoint)
			local offset = closest - hitPoint
			if offset.Magnitude <= radius then
				addPartner(part)
				-- Only the sideways part of the offset moves the bead.
				local sideways = offset - hitNormal * offset:Dot(hitNormal)
				if nearestOffset == nil or sideways.Magnitude < nearestOffset.Magnitude then
					nearestOffset = sideways
				end
			end
		end
	end
	if Config.SeamSnap and not seam.fillet and nearestOffset then
		if nearestOffset.Magnitude <= Config.BeadWidth * 0.75 then
			seam.point = hitPoint + nearestOffset
		end
	end

	return seam
end

return WelderShared
