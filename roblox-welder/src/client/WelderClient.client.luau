--!strict
--[[
	WelderClient (LocalScript, goes in StarterPlayer > StarterPlayerScripts)

	Runs on each player's computer:
	  * aiming and input while the Welder tool is held (mouse, touch, gamepad)
	  * an aim marker: green = metal, cyan = a seam it will join,
	    orange = too far, red = not metal
	  * the HUD: travel speed, weld quality, messages from the server
	  * the auto-darkening welding helmet (press H / gamepad Y to lift it)
	  * the glow of hot beads cooling down, and the flicker of every arc

	The server checks everything this script sends, so changing it can't be
	used to weld wood or weld from across the map.
]]

local CollectionService = game:GetService("CollectionService")
local ContextActionService = game:GetService("ContextActionService")
local Lighting = game:GetService("Lighting")
local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local RunService = game:GetService("RunService")
local UserInputService = game:GetService("UserInputService")

local WelderShared = require(ReplicatedStorage:WaitForChild("WelderShared"))
local Config = WelderShared.Config
local remote = ReplicatedStorage:WaitForChild(Config.RemoteName) :: RemoteEvent
local arcFolder = workspace:WaitForChild(Config.ArcFolderName)

local player = Players.LocalPlayer
local random = Random.new()

---------------------------------------------------------------------------
-- HUD
---------------------------------------------------------------------------
local gui = Instance.new("ScreenGui")
gui.Name = "WelderHud"
gui.ResetOnSpawn = false
gui.Enabled = false
gui.Parent = player:WaitForChild("PlayerGui")

local function label(parent: Instance, name: string, position: UDim2, size: UDim2, textSize: number): TextLabel
	local text = Instance.new("TextLabel")
	text.Name = name
	text.BackgroundTransparency = 1
	text.Position = position
	text.Size = size
	text.Font = Enum.Font.GothamMedium
	text.TextSize = textSize
	text.TextColor3 = Color3.fromRGB(235, 235, 240)
	text.TextXAlignment = Enum.TextXAlignment.Left
	text.TextTruncate = Enum.TextTruncate.AtEnd
	text.Text = ""
	text.Parent = parent
	return text
end

local panel = Instance.new("Frame")
panel.Name = "Panel"
panel.AnchorPoint = Vector2.new(0.5, 1)
panel.Position = UDim2.new(0.5, 0, 1, -96)
panel.Size = UDim2.fromOffset(380, 92)
panel.BackgroundColor3 = Color3.fromRGB(18, 18, 22)
panel.BackgroundTransparency = 0.3
panel.Parent = gui
local corner = Instance.new("UICorner")
corner.CornerRadius = UDim.new(0, 8)
corner.Parent = panel

local titleLabel = label(panel, "Title", UDim2.fromOffset(12, 6), UDim2.new(1, -24, 0, 18), 14)
local aimLabel = label(panel, "Aim", UDim2.fromOffset(12, 26), UDim2.new(1, -24, 0, 16), 13)
local speedLabel = label(panel, "Speed", UDim2.fromOffset(12, 44), UDim2.new(1, -24, 0, 16), 13)
local noteLabel = label(panel, "Note", UDim2.fromOffset(12, 72), UDim2.new(1, -24, 0, 16), 12)
noteLabel.TextColor3 = Color3.fromRGB(255, 200, 120)

local barBack = Instance.new("Frame")
barBack.Name = "QualityBar"
barBack.Position = UDim2.fromOffset(12, 63)
barBack.Size = UDim2.new(1, -24, 0, 6)
barBack.BackgroundColor3 = Color3.fromRGB(60, 60, 66)
barBack.BorderSizePixel = 0
barBack.Parent = panel
local barFill = Instance.new("Frame")
barFill.Name = "Fill"
barFill.Size = UDim2.fromScale(0, 1)
barFill.BackgroundColor3 = Color3.fromRGB(90, 200, 120)
barFill.BorderSizePixel = 0
barFill.Parent = barBack

-- Pop-up messages (joined, broke, rejected).
local messageLabel = Instance.new("TextLabel")
messageLabel.Name = "Message"
messageLabel.AnchorPoint = Vector2.new(0.5, 0)
messageLabel.Position = UDim2.new(0.5, 0, 0, 70)
messageLabel.Size = UDim2.fromOffset(520, 30)
messageLabel.BackgroundColor3 = Color3.fromRGB(18, 18, 22)
messageLabel.BackgroundTransparency = 0.25
messageLabel.Font = Enum.Font.GothamBold
messageLabel.TextSize = 16
messageLabel.TextColor3 = Color3.new(1, 1, 1)
messageLabel.Visible = false
messageLabel.Parent = gui
local messageCorner = Instance.new("UICorner")
messageCorner.CornerRadius = UDim.new(0, 8)
messageCorner.Parent = messageLabel

local messageUntil = 0
local function showMessage(text: string, color: Color3?)
	messageLabel.Text = text
	messageLabel.TextColor3 = color or Color3.new(1, 1, 1)
	messageLabel.Visible = true
	messageUntil = os.clock() + 2.5
end

---------------------------------------------------------------------------
-- Aim marker
---------------------------------------------------------------------------
local marker = Instance.new("Part")
marker.Name = "WelderAimMarker"
marker.Shape = Enum.PartType.Ball
marker.Size = Vector3.new(0.22, 0.22, 0.22)
marker.Material = Enum.Material.Neon
marker.Transparency = 0.35
marker.Anchored = true
marker.CanCollide = false
marker.CanQuery = false
marker.CanTouch = false
marker.CastShadow = false

local MARKER_METAL = Color3.fromRGB(80, 230, 110)
local MARKER_SEAM = Color3.fromRGB(70, 210, 255)
local MARKER_FAR = Color3.fromRGB(255, 160, 40)
local MARKER_BAD = Color3.fromRGB(255, 60, 60)

---------------------------------------------------------------------------
-- Welding helmet (client-only screen effects)
---------------------------------------------------------------------------
local lens = Instance.new("ColorCorrectionEffect")
lens.Name = "WelderHelmetLens"
lens.Enabled = false
lens.Parent = Lighting

local flash = Instance.new("ColorCorrectionEffect")
flash.Name = "WelderArcFlash"
flash.Enabled = false
flash.TintColor = Color3.fromRGB(215, 228, 255)
flash.Parent = Lighting

local helmetDown = true
local darkUntil = 0

-- Auto-darkening lens: lightly tinted (like shade 3) until an arc is seen,
-- then dark (like shade 10-11) within a frame, and back to light a moment
-- after the arc goes out.
local function setLens(dark: boolean)
	if dark then
		lens.Brightness = -0.45
		lens.Contrast = 0.35
		lens.Saturation = -0.6
		lens.TintColor = Color3.fromRGB(120, 205, 120)
	else
		lens.Brightness = -0.05
		lens.Contrast = 0
		lens.Saturation = -0.1
		lens.TintColor = Color3.fromRGB(205, 235, 205)
	end
end

---------------------------------------------------------------------------
-- Tool handling
---------------------------------------------------------------------------
local equippedTool: Tool? = nil
local toolConnections: { RBXScriptConnection } = {}
local triggerHeld = false
local lastSend = 0
local lastLocalReject = 0
local lastTouch: Vector2? = nil

local function updateTitle()
	titleLabel.Text = string.format("MIG WELDER    Helmet: %s (H)", if helmetDown then "DOWN" else "UP")
end

local function onHelmetAction(_: string, state: Enum.UserInputState): Enum.ContextActionResult
	if state == Enum.UserInputState.Begin then
		helmetDown = not helmetDown
		updateTitle()
	end
	return Enum.ContextActionResult.Sink
end

local function unequip()
	for _, connection in toolConnections do
		connection:Disconnect()
	end
	table.clear(toolConnections)
	if triggerHeld then
		remote:FireServer("Stop")
	end
	triggerHeld = false
	equippedTool = nil
	gui.Enabled = false
	marker.Parent = nil
	lens.Enabled = false
	ContextActionService:UnbindAction("WelderHelmet")
end

local function equip(tool: Tool)
	if equippedTool == tool then
		return
	end
	unequip()
	equippedTool = tool
	gui.Enabled = true
	marker.Parent = workspace
	updateTitle()
	speedLabel.Text = "Hold click on metal to weld"
	noteLabel.Text = ""
	barFill.Size = UDim2.fromScale(0, 1)
	ContextActionService:BindAction("WelderHelmet", onHelmetAction, true, Enum.KeyCode.H, Enum.KeyCode.ButtonY)
	ContextActionService:SetTitle("WelderHelmet", "Helmet")

	table.insert(toolConnections, tool.Activated:Connect(function()
		triggerHeld = true
	end))
	table.insert(toolConnections, tool.Deactivated:Connect(function()
		triggerHeld = false
		remote:FireServer("Stop")
	end))
end

local function watchCharacter(character: Model)
	local function check(child: Instance)
		if child:IsA("Tool") and child:GetAttribute("IsWelder") == true then
			equip(child)
		end
	end
	for _, child in character:GetChildren() do
		check(child)
	end
	character.ChildAdded:Connect(check)
	character.ChildRemoved:Connect(function(child)
		if child == equippedTool then
			unequip()
		end
	end)
end

if player.Character then
	watchCharacter(player.Character)
end
player.CharacterAdded:Connect(watchCharacter)
player.CharacterRemoving:Connect(function()
	unequip()
end)

UserInputService.InputBegan:Connect(function(input)
	if input.UserInputType == Enum.UserInputType.Touch then
		lastTouch = Vector2.new(input.Position.X, input.Position.Y)
	end
end)
UserInputService.InputChanged:Connect(function(input)
	if input.UserInputType == Enum.UserInputType.Touch then
		lastTouch = Vector2.new(input.Position.X, input.Position.Y)
	end
end)

-- Ray through the point the player is aiming at.
local function aimRay(camera: Camera): Ray
	local inputType = UserInputService:GetLastInputType()
	if inputType == Enum.UserInputType.Touch and lastTouch then
		-- Touch positions are screen positions (below the top bar).
		local touch = lastTouch :: Vector2
		return camera:ScreenPointToRay(touch.X, touch.Y)
	elseif inputType.Name:sub(1, 7) == "Gamepad" then
		local centre = camera.ViewportSize / 2
		return camera:ViewportPointToRay(centre.X, centre.Y)
	end
	-- The mouse location is a viewport position (includes the top bar).
	local mouse = UserInputService:GetMouseLocation()
	return camera:ViewportPointToRay(mouse.X, mouse.Y)
end

local function aim(): (BasePart?, Vector3, Vector3)
	local camera = workspace.CurrentCamera
	local ray = aimRay(camera)
	local params = RaycastParams.new()
	params.FilterType = Enum.RaycastFilterType.Exclude
	local exclude: { Instance } = { arcFolder, marker }
	if player.Character then
		table.insert(exclude, player.Character)
	end
	params.FilterDescendantsInstances = exclude
	local result = workspace:Raycast(ray.Origin, ray.Direction * 250, params)
	if result and result.Instance:IsA("BasePart") then
		return result.Instance :: BasePart, result.Position, result.Normal
	end
	return nil, ray.Origin + ray.Direction * 250, Vector3.yAxis
end

local seamTimer = 0
local seamText = ""
local seamPartners = 0

local function updateWelding(dt: number)
	local tool = equippedTool
	local character = player.Character
	local root = character and character:FindFirstChild("HumanoidRootPart")
	if not tool or not root or not root:IsA("BasePart") then
		return
	end

	local part, position, normal = aim()
	local isMetal, _, reason = WelderShared.checkMetal(part)
	local distance = (position - root.Position).Magnitude
	local inReach = distance <= Config.MaxReach

	-- Seam preview, a few times a second.
	seamTimer += dt
	if seamTimer > 0.1 then
		seamTimer = 0
		seamPartners = 0
		seamText = ""
		if part and isMetal and inReach then
			local exclude: { Instance } = { arcFolder, marker }
			for _, other in Players:GetPlayers() do
				if other.Character then
					table.insert(exclude, other.Character)
				end
			end
			local seam = WelderShared.findSeam(part, position, normal, exclude)
			seamPartners = #seam.partners
			if seamPartners > 0 then
				local names = {}
				for _, partner in seam.partners do
					table.insert(names, partner.Name)
				end
				seamText = (if seam.fillet then "  |  fillet with " else "  |  seam with ") .. table.concat(names, ", ")
			end
		end
	end

	marker.CFrame = CFrame.new(position + normal * 0.05)
	if part == nil then
		marker.Color = MARKER_BAD
		aimLabel.Text = "Aim at metal"
	elseif not isMetal then
		marker.Color = MARKER_BAD
		aimLabel.Text = string.format("%s: %s", part.Name, reason or "not weldable")
	elseif not inReach then
		marker.Color = MARKER_FAR
		aimLabel.Text = string.format("%s: too far (%.1f / %d studs)", part.Name, distance, Config.MaxReach)
	else
		marker.Color = if seamPartners > 0 then MARKER_SEAM else MARKER_METAL
		aimLabel.Text = string.format("%s (%s)%s", part.Name, part.Material.Name, seamText)
	end

	if not triggerHeld then
		return
	end
	local now = os.clock()
	if part and isMetal and inReach then
		if now - lastSend >= Config.SendRate then
			lastSend = now
			remote:FireServer("Point", part, position, normal)
		end
	elseif now - lastLocalReject > 0.8 then
		-- No arc: the circuit only closes through conductive metal.
		lastLocalReject = now
		if part and not isMetal then
			showMessage(reason or "Not weldable", Color3.fromRGB(255, 120, 120))
		elseif not inReach then
			showMessage("Too far away: get closer to the work", Color3.fromRGB(255, 180, 90))
		end
	end
end

remote.OnClientEvent:Connect(function(action: string, a: any, b: any, c: any)
	if action == "Status" then
		local status = a
		speedLabel.Text = string.format("Travel %.1f studs/s: %s", status.speed, status.speedLabel)
		local quality = math.clamp(status.quality, 0, 1)
		barFill.Size = UDim2.fromScale(quality, 1)
		barFill.BackgroundColor3 = Color3.fromRGB(230, 70, 60):Lerp(Color3.fromRGB(90, 200, 120), quality)
		noteLabel.Text = status.note or string.format("Weld quality %d%%", math.floor(quality * 100 + 0.5))
	elseif action == "Joined" then
		showMessage(string.format("Tacked: %s is now welded to %s", a, b), Color3.fromRGB(120, 230, 255))
	elseif action == "Broke" then
		showMessage(string.format("Weld broke (%s / %s): %s", a, b, c), Color3.fromRGB(255, 140, 90))
	elseif action == "Rejected" then
		showMessage(a, Color3.fromRGB(255, 120, 120))
	end
end)

---------------------------------------------------------------------------
-- Hot metal cooling down (all beads, including other players')
---------------------------------------------------------------------------
type Hot = { part: BasePart, deposited: number, finalColor: Color3, finalMaterial: Enum.Material }
local hot: { Hot } = {}

local function trackBead(instance: Instance)
	if not instance:IsA("BasePart") then
		return
	end
	local deposited = instance:GetAttribute("DepositTime")
	if typeof(deposited) ~= "number" then
		return
	end
	local age = workspace:GetServerTimeNow() - deposited
	if WelderShared.temperatureAt(age) < Config.ColdTemperature then
		return -- already cold (e.g. we joined the game after it was welded)
	end
	local finalColor = instance:GetAttribute("FinalColor")
	table.insert(hot, {
		part = instance,
		deposited = deposited,
		finalColor = if typeof(finalColor) == "Color3" then finalColor else instance.Color,
		finalMaterial = instance.Material,
	})
end

for _, bead in CollectionService:GetTagged(Config.BeadTag) do
	trackBead(bead)
end
CollectionService:GetInstanceAddedSignal(Config.BeadTag):Connect(trackBead)

local function updateHeat()
	local now = workspace:GetServerTimeNow()
	local i = 1
	while i <= #hot do
		local item = hot[i]
		local temperature = WelderShared.temperatureAt(now - item.deposited)
		if item.part.Parent == nil or temperature < Config.ColdTemperature then
			if item.part.Parent then
				item.part.Color = item.finalColor
				item.part.Material = item.finalMaterial
			end
			-- Swap-remove: order doesn't matter.
			hot[i] = hot[#hot]
			hot[#hot] = nil
		else
			local color, glowing = WelderShared.heatColor(temperature, item.finalColor)
			item.part.Color = color
			item.part.Material = if glowing then Enum.Material.Neon else item.finalMaterial
			i += 1
		end
	end
end

---------------------------------------------------------------------------
-- Arcs: flicker, helmet, arc flash
---------------------------------------------------------------------------
local function updateArcs()
	local camera = workspace.CurrentCamera
	local eye = camera.CFrame.Position
	local nearestVisible = math.huge

	for _, arc in CollectionService:GetTagged(Config.ArcTag) do
		if arc:IsA("BasePart") and arc:IsDescendantOf(workspace) then
			-- A welding arc flickers constantly.
			local light = arc:FindFirstChild("ArcLight")
			if light and light:IsA("PointLight") then
				light.Brightness = 5 + random:NextNumber() * 7
				light.Range = 13 + random:NextNumber() * 5
			end
			arc.Transparency = random:NextNumber() * 0.3
			local s = 0.22 + random:NextNumber() * 0.14
			arc.Size = Vector3.new(s, s, s)

			local _, onScreen = camera:WorldToViewportPoint(arc.Position)
			if onScreen then
				nearestVisible = math.min(nearestVisible, (arc.Position - eye).Magnitude)
			end
		end
	end

	local now = os.clock()
	local wearingHelmet = equippedTool ~= nil and helmetDown
	if wearingHelmet then
		if nearestVisible < 60 then
			darkUntil = now + 0.25
		end
		lens.Enabled = true
		setLens(now < darkUntil)
	else
		lens.Enabled = false
	end

	-- Looking at an arc without a helmet: a bright flash ("arc eye").
	local flashRange = 25
	if not wearingHelmet and nearestVisible < flashRange then
		local strength = (1 - nearestVisible / flashRange) * (if equippedTool then 1 else 0.5)
		flash.Enabled = true
		flash.Brightness = 0.6 * strength * (0.85 + random:NextNumber() * 0.3)
		flash.Contrast = -0.3 * strength
		if equippedTool then
			noteLabel.Text = "Arc flash! Put your helmet down (H)"
		end
	else
		flash.Enabled = false
	end
end

---------------------------------------------------------------------------
-- Frame loop
---------------------------------------------------------------------------
local heatTimer = 0
RunService.RenderStepped:Connect(function(dt)
	updateWelding(dt)
	updateArcs()

	heatTimer += dt
	if heatTimer >= 1 / 30 then
		heatTimer = 0
		updateHeat()
	end

	if messageLabel.Visible and os.clock() > messageUntil then
		messageLabel.Visible = false
	end
end)
