--!strict
--[[
	WelderServer (Script, goes in ServerScriptService)

	The server decides everything that matters:
	  * builds the Welder tool and gives it to every player
	  * checks every weld point the client sends (is it metal, is it in
	    reach, is the point really on that part's surface)
	  * lays the bead: overlapping disc "ripples" sunk into the metal
	  * joins two metal parts with a WeldConstraint once enough bead has
	    been laid along the seam between them (a tack weld first, then the
	    joint gets stronger as the bead gets longer)
	  * breaks joints that are overloaded (a weak weld holding a heavy part
	    out sideways, or a hard impact)

	Visual-only work (the glow of hot metal cooling, arc flicker, the welding
	helmet, the HUD) is done by WelderClient on each player's computer.
]]

local CollectionService = game:GetService("CollectionService")
local Debris = game:GetService("Debris")
local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local RunService = game:GetService("RunService")
local StarterPack = game:GetService("StarterPack")

local WelderShared = require(ReplicatedStorage:WaitForChild("WelderShared"))
local Config = WelderShared.Config

local random = Random.new()

---------------------------------------------------------------------------
-- Remote event and folders
---------------------------------------------------------------------------
local remote = Instance.new("RemoteEvent")
remote.Name = Config.RemoteName
remote.Parent = ReplicatedStorage

local arcFolder = Instance.new("Folder")
arcFolder.Name = Config.ArcFolderName
arcFolder.Parent = workspace

---------------------------------------------------------------------------
-- The tool
---------------------------------------------------------------------------
local function buildTool(): Tool
	local tool = Instance.new("Tool")
	tool.Name = Config.ToolName
	tool.ToolTip = "MIG welder: hold click on metal"
	tool.CanBeDropped = false
	tool.RequiresHandle = true
	tool:SetAttribute("IsWelder", true)

	local handle = Instance.new("Part")
	handle.Name = "Handle"
	handle.Size = Vector3.new(0.35, 0.5, 1.4)
	handle.Material = Enum.Material.SmoothPlastic
	handle.Color = Color3.fromRGB(35, 35, 38)
	handle.CanCollide = false
	handle.Massless = true
	handle.Parent = tool

	-- Copper nozzle on the front. SmoothPlastic on purpose so the gun is
	-- never mistaken for weldable metal.
	local nozzle = Instance.new("Part")
	nozzle.Name = "Nozzle"
	nozzle.Shape = Enum.PartType.Cylinder
	nozzle.Size = Vector3.new(0.5, 0.22, 0.22)
	nozzle.Material = Enum.Material.SmoothPlastic
	nozzle.Color = Color3.fromRGB(184, 115, 51)
	nozzle.CanCollide = false
	nozzle.Massless = true
	nozzle.Parent = tool

	local weld = Instance.new("Weld")
	weld.Part0 = handle
	weld.Part1 = nozzle
	-- The cylinder's long axis is its X axis; turn it to point along -Z.
	weld.C0 = CFrame.new(0, 0, -0.9) * CFrame.Angles(0, math.rad(90), 0)
	weld.Parent = handle

	return tool
end

local toolTemplate = buildTool()
toolTemplate.Parent = StarterPack

local function giveTool(player: Player)
	local backpack = player:FindFirstChildOfClass("Backpack")
	if not backpack then
		return
	end
	local character = player.Character
	if backpack:FindFirstChild(Config.ToolName) or (character and character:FindFirstChild(Config.ToolName)) then
		return
	end
	toolTemplate:Clone().Parent = backpack
end

-- Players who spawned before this script ran don't get StarterPack again
-- until they respawn, so hand them a tool now.
for _, player in Players:GetPlayers() do
	giveTool(player)
end

---------------------------------------------------------------------------
-- Helpers
---------------------------------------------------------------------------
local function characterExclusions(): { Instance }
	local list: { Instance } = { arcFolder }
	for _, player in Players:GetPlayers() do
		if player.Character then
			table.insert(list, player.Character)
		end
	end
	return list
end

local function getBeadFolder(base: BasePart): Folder
	local folder = base:FindFirstChild(Config.BeadFolderName)
	if folder and folder:IsA("Folder") then
		return folder
	end
	local newFolder = Instance.new("Folder")
	newFolder.Name = Config.BeadFolderName
	newFolder.Parent = base
	return newFolder
end

-- Attach a bead or spatter part to the metal it sits on.
local function stick(piece: BasePart, base: BasePart)
	piece.Anchored = false
	piece.CanCollide = false
	piece.CanTouch = false
	piece.CanQuery = false -- rays and overlap checks pass through beads
	piece.Massless = true
	piece.CastShadow = false
	piece.Parent = getBeadFolder(base)
	local weld = Instance.new("WeldConstraint")
	weld.Part0 = piece
	weld.Part1 = base
	weld.Parent = piece
end

-- Mark a piece as freshly deposited so clients show it glowing and cooling.
local function markHot(piece: BasePart, finalColor: Color3)
	piece:SetAttribute("DepositTime", workspace:GetServerTimeNow())
	piece:SetAttribute("FinalColor", finalColor)
	piece:AddTag(Config.BeadTag)
end

---------------------------------------------------------------------------
-- Laying the bead
---------------------------------------------------------------------------
local function makeRipple(
	base: BasePart,
	point: Vector3,
	normal: Vector3,
	travelDir: Vector3,
	width: number,
	height: number,
	quality: number,
	fillet: boolean,
	thickness: number? -- metal thickness to stay inside, nil = may burn through
)
	-- Uneven travel gives an uneven bead: worse welds wobble more.
	width *= 1 + (random:NextNumber() - 0.5) * (0.08 + 0.5 * (1 - quality))
	height *= 1 + (random:NextNumber() - 0.5) * (0.1 + 0.4 * (1 - quality))

	local radius, depth
	if fillet then
		-- Fills the inside corner; legs come out at roughly half the width.
		radius, depth = WelderShared.filletDisc(width, thickness)
	else
		if thickness then
			width, height = WelderShared.fitCapToThickness(width, height, thickness)
		end
		radius, depth = WelderShared.discForCap(width, height)
	end

	-- The disc's axis follows the direction of travel, leaned back a little
	-- so the ripples overlap like a row of stacked coins.
	local along = travelDir - normal * travelDir:Dot(normal)
	if along.Magnitude < 1e-3 then
		along = (WelderShared.tangents(normal))
	else
		along = along.Unit
	end
	local axis = (along * math.cos(Config.RippleTilt) + normal * math.sin(Config.RippleTilt)).Unit
	local up = (normal - axis * normal:Dot(axis)).Unit

	local ripple = Instance.new("Part")
	ripple.Name = "WeldRipple"
	ripple.Shape = Enum.PartType.Cylinder -- a cylinder's length runs along its X axis
	ripple.Size = Vector3.new(Config.RippleThickness, radius * 2, radius * 2)
	ripple.CFrame = CFrame.fromMatrix(point - normal * depth, axis, up)
	ripple.Material = Config.BeadMaterial
	ripple.TopSurface = Enum.SurfaceType.Smooth
	ripple.BottomSurface = Enum.SurfaceType.Smooth

	-- Cooled colour: slight variation, and poor welds come out duller/darker.
	local shade = 1 + (random:NextNumber() - 0.5) * 0.12
	local base3 = Config.BeadColor
	local color = Color3.new(
		math.min(base3.R * shade, 1),
		math.min(base3.G * shade, 1),
		math.min(base3.B * shade, 1)
	):Lerp(Color3.fromRGB(90, 86, 82), (1 - quality) * 0.5)
	ripple.Color = color

	stick(ripple, base)
	markHot(ripple, color)
end

local function makeSpatter(base: BasePart, point: Vector3, normal: Vector3)
	local u, v = WelderShared.tangents(normal)
	local angle = random:NextNumber(0, math.pi * 2)
	local distance = random:NextNumber(0.25, 1.1)
	local spot = point + (u * math.cos(angle) + v * math.sin(angle)) * distance

	local params = RaycastParams.new()
	params.FilterType = Enum.RaycastFilterType.Include
	params.FilterDescendantsInstances = { base }
	local result = workspace:Raycast(spot + normal * 0.3, -normal * 0.6, params)
	if not result then
		return -- landed off the edge of the part
	end

	local size = random:NextNumber(0.04, 0.09)
	local ball = Instance.new("Part")
	ball.Name = "Spatter"
	ball.Shape = Enum.PartType.Ball
	ball.Size = Vector3.new(size, size, size)
	ball.CFrame = CFrame.new(result.Position + result.Normal * size * 0.25)
	ball.Material = Config.BeadMaterial
	ball.Color = Config.BeadColor:Lerp(Color3.new(0.2, 0.2, 0.2), 0.3)
	stick(ball, base)
	markHot(ball, ball.Color)
end

---------------------------------------------------------------------------
-- Joints between parts
---------------------------------------------------------------------------
type Pending = { length: number, quality: number }
-- pending[a][b]: bead laid on the seam between a and b that hasn't joined
-- them yet. Weak keys so destroyed parts are forgotten.
local pending: { [BasePart]: { [BasePart]: Pending } } = setmetatable({}, { __mode = "k" }) :: any

local function findJoint(a: BasePart, b: BasePart): WeldConstraint?
	for _, holder in { a, b } do
		for _, child in holder:GetChildren() do
			if child:IsA("WeldConstraint") and child:HasTag(Config.JointTag) then
				if (child.Part0 == a and child.Part1 == b) or (child.Part0 == b and child.Part1 == a) then
					return child
				end
			end
		end
	end
	return nil
end

local function updateStrength(joint: WeldConstraint, length: number, quality: number)
	local force, torque = WelderShared.jointStrength(length, quality)
	joint:SetAttribute("BeadLength", length)
	joint:SetAttribute("Quality", quality)
	joint:SetAttribute("ForceRating", force)
	joint:SetAttribute("TorqueRating", torque)
end

local jointsChanged = true

local function addSeamLength(player: Player, base: BasePart, partner: BasePart, length: number, quality: number, point: Vector3)
	local joint = findJoint(base, partner)
	if joint then
		local part0 = joint.Part0 :: BasePart
		local oldLength = (joint:GetAttribute("BeadLength") :: number?) or 0
		local oldQuality = (joint:GetAttribute("Quality") :: number?) or quality
		local newLength = oldLength + length
		local newQuality = (oldQuality * oldLength + quality * length) / newLength
		-- Centre of the bead, kept in Part0's space so it moves with the part.
		local oldPoint = (joint:GetAttribute("LocalPoint") :: Vector3?) or part0.CFrame:PointToObjectSpace(point)
		local localPoint = oldPoint:Lerp(part0.CFrame:PointToObjectSpace(point), length / newLength)
		joint:SetAttribute("LocalPoint", localPoint)
		updateStrength(joint, newLength, newQuality)
		return
	end

	local row = pending[base]
	if row == nil then
		row = setmetatable({}, { __mode = "k" }) :: any
		pending[base] = row
	end
	-- The same seam might already be on record the other way round.
	local other = pending[partner]
	local entry = row[partner] or (other and other[base])
	if entry == nil then
		entry = { length = 0, quality = 0 }
		row[partner] = entry
	end
	entry.quality = (entry.quality * entry.length + quality * length) / (entry.length + length)
	entry.length += length

	if entry.length >= Config.TackLength then
		local newJoint = Instance.new("WeldConstraint")
		newJoint.Name = "WeldJoint"
		newJoint.Part0 = base
		newJoint.Part1 = partner
		newJoint:AddTag(Config.JointTag)
		newJoint:SetAttribute("LocalPoint", base.CFrame:PointToObjectSpace(point))
		updateStrength(newJoint, entry.length, entry.quality)
		newJoint.Parent = base
		row[partner] = nil
		if other then
			other[base] = nil
		end
		jointsChanged = true
		remote:FireClient(player, "Joined", base.Name, partner.Name)
	end
end

---------------------------------------------------------------------------
-- Breaking overloaded joints
---------------------------------------------------------------------------
local function sparkBurst(position: Vector3, count: number)
	-- Terrain sits at the world origin, so Position is a world position.
	local attachment = Instance.new("Attachment")
	attachment.Parent = workspace.Terrain
	attachment.Position = position
	local emitter = Instance.new("ParticleEmitter")
	emitter.Rate = 0
	emitter.Lifetime = NumberRange.new(0.3, 0.8)
	emitter.Speed = NumberRange.new(6, 16)
	emitter.SpreadAngle = Vector2.new(180, 180)
	emitter.Acceleration = Vector3.new(0, -workspace.Gravity * 0.35, 0)
	emitter.LightEmission = 1
	emitter.LightInfluence = 0
	emitter.Size = NumberSequence.new({ NumberSequenceKeypoint.new(0, 0.08), NumberSequenceKeypoint.new(1, 0) })
	emitter.Color = ColorSequence.new(Color3.fromRGB(255, 230, 160), Color3.fromRGB(255, 120, 30))
	emitter.Orientation = Enum.ParticleOrientation.VelocityParallel
	emitter.Parent = attachment
	emitter:Emit(count)
	Debris:AddItem(attachment, 2)
end

local function breakJoint(joint: WeldConstraint, why: string)
	local part0, part1 = joint.Part0, joint.Part1
	local localPoint = joint:GetAttribute("LocalPoint") :: Vector3?
	if part0 and localPoint then
		sparkBurst(part0.CFrame:PointToWorldSpace(localPoint), 20)
	end
	joint:Destroy()
	jointsChanged = true
	remote:FireAllClients("Broke", part0 and part0.Name or "?", part1 and part1.Name or "?", why)
end

type JointInfo = {
	joint: WeldConstraint,
	mass0: number,
	mass1: number,
	anchored0: boolean,
	anchored1: boolean,
	cyclic: boolean,
	side0: { BasePart },
	side1: { BasePart },
	lastVelocity: Vector3?,
}
local jointInfo: { JointInfo } = {}

local function liveJoints(): { WeldConstraint }
	local list = {}
	for _, item in CollectionService:GetTagged(Config.JointTag) do
		if item:IsA("WeldConstraint") and item.Enabled and item.Part0 and item.Part1 and item:IsDescendantOf(workspace) then
			table.insert(list, item)
		end
	end
	return list
end

-- Parts reachable from `start` through welder joints, not crossing `skip`.
local function collectSide(start: BasePart, skip: WeldConstraint, links: { [BasePart]: { { joint: WeldConstraint, other: BasePart } } }): { BasePart }
	local seen: { [BasePart]: boolean } = { [start] = true }
	local order: { BasePart } = { start }
	local i = 1
	while i <= #order do
		local part = order[i]
		for _, link in links[part] or {} do
			if link.joint ~= skip and not seen[link.other] then
				seen[link.other] = true
				table.insert(order, link.other)
			end
		end
		i += 1
	end
	return order
end

local function sideMass(side: { BasePart }): (number, boolean)
	local mass, anchored = 0, false
	for _, part in side do
		if part.Anchored then
			anchored = true
		end
		if not part.Massless then
			mass += part:GetMass()
		end
	end
	return mass, anchored
end

local function rebuildJointInfo()
	local joints = liveJoints()
	local links: { [BasePart]: { { joint: WeldConstraint, other: BasePart } } } = {}
	for _, joint in joints do
		local a, b = joint.Part0 :: BasePart, joint.Part1 :: BasePart
		links[a] = links[a] or {}
		links[b] = links[b] or {}
		table.insert(links[a], { joint = joint, other = b })
		table.insert(links[b], { joint = joint, other = a })
	end

	local old: { [WeldConstraint]: Vector3? } = {}
	for _, info in jointInfo do
		old[info.joint] = info.lastVelocity
	end

	jointInfo = {}
	for _, joint in joints do
		local a, b = joint.Part0 :: BasePart, joint.Part1 :: BasePart
		local side0 = collectSide(a, joint, links)
		local cyclic = table.find(side0, b) ~= nil
		local side1 = if cyclic then side0 else collectSide(b, joint, links)
		local mass0, anchored0 = sideMass(side0)
		local mass1, anchored1 = sideMass(side1)
		table.insert(jointInfo, {
			joint = joint,
			mass0 = mass0,
			mass1 = mass1,
			anchored0 = anchored0,
			anchored1 = anchored1,
			cyclic = cyclic,
			side0 = side0,
			side1 = side1,
			lastVelocity = old[joint],
		})
	end
end

-- Is anything other than the joint's own parts holding this side up
-- (a table, the floor, a support block)?
local function isSupported(side: { BasePart }, ignore: { Instance }): boolean
	local params = OverlapParams.new()
	params.FilterType = Enum.RaycastFilterType.Exclude
	params.FilterDescendantsInstances = ignore
	for _, part in side do
		for _, touching in workspace:GetPartBoundsInBox(part.CFrame, part.Size + Vector3.new(0.1, 0.1, 0.1), params) do
			if touching.CanCollide then
				return true
			end
		end
	end
	return false
end

-- Static load: one side is held by an anchored part, the other side hangs
-- off the joint. The joint has to carry its weight (force) and the weight
-- times its sideways distance from the weld (torque, like a shelf bracket).
local function checkStaticLoad(info: JointInfo)
	if info.cyclic or info.anchored0 == info.anchored1 then
		return
	end
	local joint = info.joint
	local loadSide = if info.anchored0 then info.side1 else info.side0
	local ignore: { Instance } = characterExclusions()
	for _, part in info.side0 do
		table.insert(ignore, part)
	end
	for _, part in info.side1 do
		table.insert(ignore, part)
	end
	if isSupported(loadSide, ignore) then
		return
	end

	local mass, weighted = 0, Vector3.zero
	for _, part in loadSide do
		if not part.Massless then
			local m = part:GetMass()
			mass += m
			weighted += part.Position * m
		end
	end
	if mass <= 0 then
		return
	end
	local centre = weighted / mass
	local part0 = joint.Part0 :: BasePart
	local localPoint = (joint:GetAttribute("LocalPoint") :: Vector3?) or Vector3.zero
	local weldPoint = part0.CFrame:PointToWorldSpace(localPoint)
	local lever = centre - weldPoint
	local sideways = Vector3.new(lever.X, 0, lever.Z).Magnitude -- gravity pulls along -Y

	local weight = mass * workspace.Gravity
	local torque = weight * sideways
	local forceRating = (joint:GetAttribute("ForceRating") :: number?) or 0
	local torqueRating = (joint:GetAttribute("TorqueRating") :: number?) or 0
	if weight > forceRating then
		breakJoint(joint, "too much weight for the weld")
	elseif torque > torqueRating then
		breakJoint(joint, "the weld snapped under leverage")
	end
end

-- Impacts: a sudden change of speed (beyond gravity) puts a force on the
-- joint of roughly (mass of the lighter side) x (acceleration).
local function checkImpact(info: JointInfo, dt: number)
	local joint = info.joint
	if info.anchored0 or info.anchored1 or not joint.Parent then
		info.lastVelocity = nil
		return
	end
	local part1 = joint.Part1
	if not part1 then
		return
	end
	local velocity = part1.AssemblyLinearVelocity
	local last = info.lastVelocity
	info.lastVelocity = velocity
	if last == nil or dt <= 0 then
		return
	end
	local g = workspace.Gravity
	local acceleration = (velocity - last) / dt + Vector3.new(0, g, 0) -- remove gravity
	local excess = math.max(0, acceleration.Magnitude - g * Config.ImpactGrace)
	local share = if info.cyclic then 0.5 else 1
	local force = math.min(info.mass0, info.mass1) * excess * share
	local forceRating = (joint:GetAttribute("ForceRating") :: number?) or 0
	if force > forceRating then
		breakJoint(joint, "the weld cracked on impact")
	end
end

---------------------------------------------------------------------------
-- Welding sessions (one per player)
---------------------------------------------------------------------------
type Session = {
	arc: Part?,
	lastPoint: Vector3?,
	lastNormal: Vector3,
	lastPart: BasePart?,
	lastTime: number,
	lastReceive: number,
	lastStatus: number,
	lastReject: number,
	travelDir: Vector3,
	carry: number,
	speed: number,
	dwell: number,
	dwellGrowth: number,
}
local sessions: { [Player]: Session } = {}

local function getSession(player: Player): Session
	local session = sessions[player]
	if session == nil then
		session = {
			arc = nil,
			lastPoint = nil,
			lastNormal = Vector3.yAxis,
			lastPart = nil,
			lastTime = 0,
			lastReceive = 0,
			lastStatus = 0,
			lastReject = 0,
			travelDir = Vector3.xAxis,
			carry = 0,
			speed = 0,
			dwell = 0,
			dwellGrowth = 1,
		}
		sessions[player] = session
	end
	return session :: Session
end

local function makeArc(player: Player): Part
	local arc = Instance.new("Part")
	arc.Name = "Arc_" .. player.Name
	arc.Shape = Enum.PartType.Ball
	arc.Size = Vector3.new(0.28, 0.28, 0.28)
	arc.Material = Enum.Material.Neon
	arc.Color = Color3.fromRGB(205, 225, 255)
	arc.Anchored = true
	arc.CanCollide = false
	arc.CanTouch = false
	arc.CanQuery = false
	arc.CastShadow = false
	arc:SetAttribute("OwnerUserId", player.UserId)

	local emit = Instance.new("Attachment")
	emit.Name = "Emit"
	emit.Parent = arc

	local sparks = Instance.new("ParticleEmitter")
	sparks.Name = "Sparks"
	sparks.Rate = 140
	sparks.Lifetime = NumberRange.new(0.25, 0.7)
	sparks.Speed = NumberRange.new(8, 22)
	sparks.SpreadAngle = Vector2.new(75, 75)
	sparks.Acceleration = Vector3.new(0, -workspace.Gravity * 0.35, 0)
	sparks.Drag = 1.5
	sparks.LightEmission = 1
	sparks.LightInfluence = 0
	sparks.Size = NumberSequence.new({ NumberSequenceKeypoint.new(0, 0.07), NumberSequenceKeypoint.new(1, 0) })
	sparks.Color = ColorSequence.new({
		ColorSequenceKeypoint.new(0, Color3.fromRGB(255, 250, 220)),
		ColorSequenceKeypoint.new(0.4, Color3.fromRGB(255, 200, 90)),
		ColorSequenceKeypoint.new(1, Color3.fromRGB(255, 110, 20)),
	})
	sparks.Orientation = Enum.ParticleOrientation.VelocityParallel
	sparks.EmissionDirection = Enum.NormalId.Top
	sparks.Parent = emit

	local smoke = Instance.new("ParticleEmitter")
	smoke.Name = "Smoke"
	smoke.Rate = 12
	smoke.Lifetime = NumberRange.new(1.2, 2.2)
	smoke.Speed = NumberRange.new(1, 2.5)
	smoke.SpreadAngle = Vector2.new(25, 25)
	smoke.Acceleration = Vector3.new(0, 2, 0)
	smoke.Size = NumberSequence.new({ NumberSequenceKeypoint.new(0, 0.3), NumberSequenceKeypoint.new(1, 1.6) })
	smoke.Transparency = NumberSequence.new({ NumberSequenceKeypoint.new(0, 0.55), NumberSequenceKeypoint.new(1, 1) })
	smoke.Color = ColorSequence.new(Color3.fromRGB(150, 150, 155))
	smoke.Rotation = NumberRange.new(0, 360)
	smoke.RotSpeed = NumberRange.new(-40, 40)
	smoke.EmissionDirection = Enum.NormalId.Top
	smoke.Parent = emit

	local light = Instance.new("PointLight")
	light.Name = "ArcLight"
	light.Color = Color3.fromRGB(190, 215, 255)
	light.Brightness = 8
	light.Range = 16
	light.Shadows = true
	light.Parent = arc

	if Config.ArcSoundId ~= "" then
		local sound = Instance.new("Sound")
		sound.Name = "ArcSound"
		sound.SoundId = Config.ArcSoundId
		sound.Looped = true
		sound.Volume = 0.6
		sound.RollOffMaxDistance = 60
		sound.Parent = arc
		sound:Play()
	end

	arc:AddTag(Config.ArcTag)
	arc.Parent = arcFolder
	return arc
end

local function endBead(session: Session)
	if session.arc then
		session.arc:Destroy()
		session.arc = nil
	end
	session.lastPoint = nil
	session.lastPart = nil
	session.carry = 0
	session.dwell = 0
	session.dwellGrowth = 1
end

local function reject(player: Player, session: Session, reason: string?)
	endBead(session)
	local now = os.clock()
	if reason and now - session.lastReject > 0.5 then
		session.lastReject = now
		remote:FireClient(player, "Rejected", reason)
	end
end

local function getWelder(player: Player): (Tool?, BasePart?)
	local character = player.Character
	if not character then
		return nil, nil
	end
	local humanoid = character:FindFirstChildOfClass("Humanoid")
	if not humanoid or humanoid.Health <= 0 then
		return nil, nil
	end
	local tool = character:FindFirstChildOfClass("Tool")
	if not tool or tool:GetAttribute("IsWelder") ~= true then
		return nil, nil
	end
	local root = character:FindFirstChild("HumanoidRootPart")
	if not root or not root:IsA("BasePart") then
		return nil, nil
	end
	return tool, root
end

local function onPoint(player: Player, part: BasePart, claimedPosition: Vector3, claimedNormal: Vector3)
	local session = getSession(player)
	local now = os.clock()
	if now - session.lastReceive < Config.SendRate * 0.5 then
		return -- faster than the client is meant to send
	end
	session.lastReceive = now

	local tool, root = getWelder(player)
	if not tool or not root then
		return reject(player, session, nil)
	end
	if not part:IsDescendantOf(workspace) then
		return reject(player, session, nil)
	end

	local isMetal, materialFactor, note = WelderShared.checkMetal(part)
	if not isMetal then
		return reject(player, session, note)
	end

	local distance = (claimedPosition - root.Position).Magnitude
	if distance > Config.MaxReach + 1 then
		return reject(player, session, "Too far away: get closer to the work")
	end

	-- Don't trust the client's numbers: find the real surface point.
	local surfaceParams = RaycastParams.new()
	surfaceParams.FilterType = Enum.RaycastFilterType.Include
	surfaceParams.FilterDescendantsInstances = { part }
	local normal = if claimedNormal.Magnitude > 0.5 then claimedNormal.Unit else Vector3.yAxis
	local surface = workspace:Raycast(claimedPosition + normal * 0.5, -normal, surfaceParams)
	if not surface or surface.Instance ~= part then
		return reject(player, session, nil)
	end
	local hitPoint, hitNormal = surface.Position, surface.Normal

	local seam = WelderShared.findSeam(part, hitPoint, hitNormal, characterExclusions())
	local longness = WelderShared.arcLongness(distance)
	local reachQ, reachNote = WelderShared.reachQuality(distance)

	-- How deep the bead may reach into the metal. Thin sheet gets nil: the
	-- bead is allowed to push out of the back (burn-through).
	local thickness: number? = WelderShared.thicknessAlong(part, hitNormal)
	if seam.fillet then
		for _, partner in seam.partners do
			local size = partner.Size
			thickness = math.min(thickness :: number, size.X, size.Y, size.Z)
		end
	end
	if (thickness :: number) < Config.ThinMetalLimit then
		thickness = nil
	end

	-- Strike or move the arc.
	if not session.arc then
		session.arc = makeArc(player)
	end
	local arc = session.arc :: Part
	local u = WelderShared.tangents(seam.normal)
	arc.CFrame = CFrame.fromMatrix(seam.point + seam.normal * 0.08, u, seam.normal)

	local continuing = session.lastPoint ~= nil
		and session.lastPart == part
		and (seam.point - (session.lastPoint :: Vector3)).Magnitude <= Config.MaxJump
		and now - session.lastTime < Config.ArcTimeout

	local speedQ, speedLabel = WelderShared.speedQuality(session.speed)

	if not continuing then
		-- New bead: the first ripple goes down where the arc struck.
		session.carry = 0
		session.dwell = 0
		session.dwellGrowth = 1
		session.speed = Config.IdealSpeed * 0.5
		speedQ, speedLabel = WelderShared.speedQuality(session.speed)
		local quality = speedQ * reachQ * materialFactor
		local width, height = WelderShared.beadShape(session.speed, longness)
		makeRipple(part, seam.point, seam.normal, session.travelDir, width, height, quality, seam.fillet, thickness)
	else
		local lastPoint = session.lastPoint :: Vector3
		local dt = now - session.lastTime
		local delta = seam.point - lastPoint
		local dist = delta.Magnitude
		local instantSpeed = dist / math.max(dt, 1 / 60)
		session.speed += (instantSpeed - session.speed) * 0.35
		if dist > 1e-3 then
			session.travelDir = delta.Unit
		end

		speedQ, speedLabel = WelderShared.speedQuality(session.speed)
		local quality = speedQ * reachQ * materialFactor
		local width, height = WelderShared.beadShape(session.speed, longness)
		local step = Config.RippleSpacing

		-- Ripples every `step` studs along the path since the last sample.
		local s = step - session.carry
		local placed = 0
		while s <= dist and placed < 60 do
			local alpha = s / dist
			local point = lastPoint:Lerp(seam.point, alpha)
			local n = session.lastNormal:Lerp(seam.normal, alpha).Unit
			makeRipple(part, point, n, session.travelDir, width, height, quality, seam.fillet, thickness)
			for _, partner in seam.partners do
				addSeamLength(player, part, partner, step, quality, point)
			end
			local spatterChance = Config.SpatterChance + Config.SpatterChanceLongArc * longness
			if random:NextNumber() < spatterChance then
				makeSpatter(part, point, n)
			end
			placed += 1
			s += step
		end
		session.carry = dist - (s - step)

		-- Holding still: the puddle keeps growing (and overheating).
		if dist < step * 0.5 then
			session.dwell += dt
			if session.dwell >= Config.DwellInterval then
				session.dwell = 0
				session.dwellGrowth = math.min(session.dwellGrowth + 0.15, Config.MaxDwellGrowth)
				local g = session.dwellGrowth
				makeRipple(part, seam.point, seam.normal, session.travelDir, width * g, height * math.sqrt(g), quality * 0.8, seam.fillet, thickness)
			end
		else
			session.dwell = 0
			session.dwellGrowth = 1
		end
	end

	session.lastPoint = seam.point
	session.lastNormal = seam.normal
	session.lastPart = part
	session.lastTime = now

	if now - session.lastStatus > 0.2 then
		session.lastStatus = now
		local partnerNames = {}
		for _, partner in seam.partners do
			table.insert(partnerNames, partner.Name)
		end
		remote:FireClient(player, "Status", {
			speed = session.speed,
			speedLabel = speedLabel,
			quality = speedQ * reachQ * materialFactor,
			note = note or reachNote,
			fillet = seam.fillet,
			partners = partnerNames,
		})
	end
end

remote.OnServerEvent:Connect(function(player: Player, action: unknown, a: unknown, b: unknown, c: unknown)
	if action == "Point" then
		-- Check types: anything can arrive from a client.
		if typeof(a) ~= "Instance" or typeof(b) ~= "Vector3" or typeof(c) ~= "Vector3" then
			return
		end
		local part = a :: Instance
		local position = b :: Vector3
		local normal = c :: Vector3
		if not part:IsA("BasePart") then
			return
		end
		if position ~= position or normal ~= normal then
			return -- NaN
		end
		onPoint(player, part, position, normal)
	elseif action == "Stop" then
		local session = sessions[player]
		if session then
			endBead(session)
		end
	end
end)

Players.PlayerRemoving:Connect(function(player)
	local session = sessions[player]
	if session then
		endBead(session)
		sessions[player] = nil
	end
end)

---------------------------------------------------------------------------
-- Main loop
---------------------------------------------------------------------------
local scanTimer = 0
RunService.Heartbeat:Connect(function(dt)
	local now = os.clock()
	for _, session in sessions do
		if session.arc and now - session.lastTime > Config.ArcTimeout then
			endBead(session)
		end
	end

	scanTimer += dt
	if scanTimer >= 0.25 or jointsChanged then
		scanTimer = 0
		jointsChanged = false
		rebuildJointInfo()
		for _, info in jointInfo do
			if info.joint.Parent then
				checkStaticLoad(info)
			end
		end
	end
	for _, info in jointInfo do
		if info.joint.Parent then
			checkImpact(info, dt)
		end
	end
end)

---------------------------------------------------------------------------
-- Practice bench
---------------------------------------------------------------------------
local function buildBench(): Model
	local bench = Instance.new("Model")
	bench.Name = "WeldingPracticeBench"
	local origin = Config.DemoPosition

	local function block(name: string, size: Vector3, offset: Vector3, material: Enum.Material, color: Color3, anchored: boolean): Part
		local part = Instance.new("Part")
		part.Name = name
		part.Size = size
		part.CFrame = CFrame.new(origin + offset)
		part.Material = material
		part.Color = color
		part.Anchored = anchored
		part.TopSurface = Enum.SurfaceType.Smooth
		part.BottomSurface = Enum.SurfaceType.Smooth
		part.Parent = bench
		return part
	end

	local wood = Color3.fromRGB(124, 92, 70)
	local steel = Color3.fromRGB(140, 142, 146)

	-- Table: wood, so it can't be welded.
	block("Table Top", Vector3.new(10, 0.6, 4), Vector3.new(0, 2.7, 0), Enum.Material.Wood, wood, true)
	for _, x in { -4.6, 4.6 } do
		for _, z in { -1.6, 1.6 } do
			block("Table Leg", Vector3.new(0.6, 2.4, 0.6), Vector3.new(x, 1.2, z), Enum.Material.Wood, wood, true)
		end
	end

	-- Butt joint: two plates edge to edge.
	block("Steel Plate A", Vector3.new(2, 0.2, 1.4), Vector3.new(-4, 3.1, -0.8), Enum.Material.Metal, steel, false)
	block("Steel Plate B", Vector3.new(2, 0.2, 1.4), Vector3.new(-2, 3.1, -0.8), Enum.Material.Metal, steel, false)
	-- T joint: an upright standing on a base plate (fillet welds).
	block("Steel Base", Vector3.new(2.4, 0.2, 1.4), Vector3.new(1, 3.1, -0.8), Enum.Material.DiamondPlate, steel, false)
	block("Steel Upright", Vector3.new(2.4, 0.8, 0.3), Vector3.new(1, 3.6, -0.8), Enum.Material.Metal, steel, false)
	block("Steel Cube", Vector3.new(0.8, 0.8, 0.8), Vector3.new(3.8, 3.4, -0.8), Enum.Material.Metal, steel, false)
	-- Metals that weld badly, and things that aren't metal at all.
	block("Rusty Bar", Vector3.new(2, 0.4, 0.4), Vector3.new(-3.5, 3.2, 1), Enum.Material.CorrodedMetal, Color3.fromRGB(120, 72, 50), false)
	block("Foil Sheet", Vector3.new(1.5, 0.05, 1.2), Vector3.new(-1, 3.025, 1), Enum.Material.Foil, Color3.fromRGB(200, 200, 205), false)
	block("Wood Plank", Vector3.new(2, 0.3, 0.8), Vector3.new(1.5, 3.15, 1), Enum.Material.Wood, wood, false)
	block("Plastic Block", Vector3.new(0.8, 0.8, 0.8), Vector3.new(3.8, 3.4, 1), Enum.Material.Plastic, Color3.fromRGB(200, 60, 60), false)

	-- Strength test: weld the beam to the post, then remove the support.
	-- A tack weld snaps; a long, good weld holds the beam out sideways.
	block("Steel Post", Vector3.new(1, 6, 1), Vector3.new(8, 3, 0), Enum.Material.Metal, steel, true)
	block("Steel Beam", Vector3.new(6, 0.6, 0.6), Vector3.new(11.5, 4.3, 0), Enum.Material.Metal, steel, false)
	local support = block("Beam Support", Vector3.new(0.6, 4, 0.6), Vector3.new(13.5, 2, 0), Enum.Material.Wood, wood, true)
	local removePrompt = Instance.new("ProximityPrompt")
	removePrompt.ActionText = "Remove support"
	removePrompt.ObjectText = "Beam support"
	removePrompt.HoldDuration = 0.5
	removePrompt.MaxActivationDistance = 12
	removePrompt.RequiresLineOfSight = false
	removePrompt.Parent = support
	removePrompt.Triggered:Connect(function()
		support:Destroy()
	end)

	local sign = block("Reset Bench", Vector3.new(1.2, 1.2, 0.2), Vector3.new(-6, 3.6, 0), Enum.Material.SmoothPlastic, Color3.fromRGB(200, 40, 40), true)
	local resetPrompt = Instance.new("ProximityPrompt")
	resetPrompt.ActionText = "Reset bench"
	resetPrompt.ObjectText = "Welding practice"
	resetPrompt.HoldDuration = 1
	resetPrompt.RequiresLineOfSight = false
	resetPrompt.Parent = sign

	bench.Parent = workspace
	return bench
end

if Config.SpawnDemo then
	local function spawnBench()
		local bench = buildBench()
		local sign = bench:FindFirstChild("Reset Bench")
		local prompt = sign and sign:FindFirstChildOfClass("ProximityPrompt")
		if prompt then
			prompt.Triggered:Connect(function()
				bench:Destroy()
				jointsChanged = true
				spawnBench()
			end)
		end
	end
	spawnBench()
end
