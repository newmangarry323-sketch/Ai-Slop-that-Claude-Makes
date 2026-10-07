--!strict
--[[
	RetroPCClient (LocalScript, goes in StarterPlayer > StarterPlayerScripts)

	Everything on the PC's screen. When you use a PC the server tells this
	script, and it:
	  1. shows a CRT monitor filling your screen and boots it (BIOS memory
	     count, then a splash screen)
	  2. shows a RetroOS 95 desktop: icons, a Start menu, a taskbar with a
	     clock, and windows you can drag around
	  3. lets you dial up ("Dialing... Verifying user name and password...
	     Logging on to network... Connected at 28,800 bps")
	  4. runs the WebWalker browser on Blockopedia, the wiki. Pages load bit
	     by bit at modem speed, with a status bar and a throbber.

	The screen is laid out as an 800 x 600 display (SVGA) and scaled up to
	fit your window with a UIScale, so all positions below are in "screen
	pixels" on that display.

	This script never decides who may edit. It asks the server, and the
	server checks the player's name before saving anything.
]]

local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local SoundService = game:GetService("SoundService")
local UserInputService = game:GetService("UserInputService")
local Workspace = game:GetService("Workspace")

local Shared = require(ReplicatedStorage:WaitForChild("RetroPCShared"))
local Config = Shared.Config

local remote = ReplicatedStorage:WaitForChild(Config.RemoteFunctionName) :: RemoteFunction
local openEvent = ReplicatedStorage:WaitForChild(Config.OpenEventName) :: RemoteEvent
local player = Players.LocalPlayer

---------------------------------------------------------------------------
-- Look
---------------------------------------------------------------------------
local SCREEN = Vector2.new(800, 600)
local TASKBAR = 28

-- The classic 16-colour-era desktop palette.
local COLORS = {
	face = Color3.fromRGB(192, 192, 192),
	light = Color3.fromRGB(223, 223, 223),
	white = Color3.fromRGB(255, 255, 255),
	shadow = Color3.fromRGB(128, 128, 128),
	dark = Color3.fromRGB(0, 0, 0),
	desktop = Color3.fromRGB(0, 128, 128),
	title = Color3.fromRGB(0, 0, 128),
	titleInactive = Color3.fromRGB(128, 128, 128),
	text = Color3.fromRGB(0, 0, 0),
	link = Color3.fromRGB(0, 0, 238),
	visited = Color3.fromRGB(85, 26, 139),
	bezel = Color3.fromRGB(222, 214, 190),
	bios = Color3.fromRGB(192, 192, 192),
}

local FONT = Enum.Font.Arial
local BOLD = Enum.Font.ArialBold
local MONO = Enum.Font.Code
local SERIF = Enum.Font.Garamond

local HOST = Config.SiteHost

---------------------------------------------------------------------------
-- Small helpers for building the UI
---------------------------------------------------------------------------
-- new("Frame", { Size = ..., ... }, parent): creates an instance and sets
-- its properties in one go.
local function new(className: string, props: { [string]: any }, parent: Instance?): any
	local inst: any = Instance.new(className :: any)
	for key, value in props do
		inst[key] = value
	end
	if parent then
		inst.Parent = parent
	end
	return inst
end

-- 3D edges: two 1-pixel lines on each side, light on the top and left and
-- dark on the bottom and right (raised), or the other way round (sunken).
-- Returns a function that switches the style, used for button presses.
-- Don't use on a frame with a UIListLayout (the lines would be laid out).
local BEVELS: { [string]: { Color3 } } = {
	raised = { COLORS.white, COLORS.dark, COLORS.light, COLORS.shadow },
	sunken = { COLORS.shadow, COLORS.white, COLORS.dark, COLORS.light },
}

local function bevel(frame: GuiObject, style: string): (string) -> ()
	local function line(position: UDim2, size: UDim2): Frame
		return new("Frame", { Position = position, Size = size, BorderSizePixel = 0, ZIndex = 20 }, frame)
	end
	local outerTop = line(UDim2.new(0, 0, 0, 0), UDim2.new(1, 0, 0, 1))
	local outerLeft = line(UDim2.new(0, 0, 0, 0), UDim2.new(0, 1, 1, 0))
	local outerBottom = line(UDim2.new(0, 0, 1, -1), UDim2.new(1, 0, 0, 1))
	local outerRight = line(UDim2.new(1, -1, 0, 0), UDim2.new(0, 1, 1, 0))
	local innerTop = line(UDim2.new(0, 1, 0, 1), UDim2.new(1, -2, 0, 1))
	local innerLeft = line(UDim2.new(0, 1, 0, 1), UDim2.new(0, 1, 1, -2))
	local innerBottom = line(UDim2.new(0, 1, 1, -2), UDim2.new(1, -2, 0, 1))
	local innerRight = line(UDim2.new(1, -2, 0, 1), UDim2.new(0, 1, 1, -2))
	local function set(s: string)
		local c = BEVELS[s]
		outerTop.BackgroundColor3 = c[1]
		outerLeft.BackgroundColor3 = c[1]
		outerBottom.BackgroundColor3 = c[2]
		outerRight.BackgroundColor3 = c[2]
		innerTop.BackgroundColor3 = c[3]
		innerLeft.BackgroundColor3 = c[3]
		innerBottom.BackgroundColor3 = c[4]
		innerRight.BackgroundColor3 = c[4]
	end
	set(style)
	return set
end

-- A grey push button that sinks in while held.
local function button(parent: Instance, text: string, size: UDim2, position: UDim2?, onClick: () -> ()): TextButton
	local b: TextButton = new("TextButton", {
		Size = size,
		Position = position or UDim2.new(),
		BackgroundColor3 = COLORS.face,
		BorderSizePixel = 0,
		AutoButtonColor = false,
		Text = text,
		Font = FONT,
		TextSize = 13,
		TextColor3 = COLORS.text,
	}, parent)
	local set = bevel(b, "raised")
	b.MouseButton1Down:Connect(function()
		set("sunken")
	end)
	b.MouseButton1Up:Connect(function()
		set("raised")
	end)
	b.MouseLeave:Connect(function()
		set("raised")
	end)
	b.Activated:Connect(function()
		if not b:GetAttribute("Disabled") then
			onClick()
		end
	end)
	return b
end

local function setEnabled(b: TextButton, enabled: boolean)
	b:SetAttribute("Disabled", not enabled)
	b.TextColor3 = if enabled then COLORS.text else COLORS.shadow
end

-- A white text field with a thin dark edge.
local function field(parent: Instance?, props: { [string]: any }): TextBox
	local defaults: { [string]: any } = {
		BackgroundColor3 = COLORS.white,
		BorderSizePixel = 0,
		Font = FONT,
		TextSize = 13,
		TextColor3 = COLORS.text,
		TextXAlignment = Enum.TextXAlignment.Left,
		ClearTextOnFocus = false,
		Text = "",
	}
	for key, value in props do
		defaults[key] = value
	end
	local box: TextBox = new("TextBox", defaults, parent)
	new("UIStroke", { Color = COLORS.shadow, Thickness = 1, ApplyStrokeMode = Enum.ApplyStrokeMode.Border }, box)
	new("UIPadding", { PaddingLeft = UDim.new(0, 4), PaddingRight = UDim.new(0, 4) }, box)
	return box
end

local function textLabel(parent: Instance, text: string, position: UDim2, size: UDim2, props: { [string]: any }?): TextLabel
	local all: { [string]: any } = {
		Position = position,
		Size = size,
		BackgroundTransparency = 1,
		Text = text,
		Font = FONT,
		TextSize = 13,
		TextColor3 = COLORS.text,
		TextXAlignment = Enum.TextXAlignment.Left,
	}
	if props then
		for key, value in props do
			all[key] = value
		end
	end
	return new("TextLabel", all, parent)
end

-- Calls the server. Always returns a table with `ok`.
local function request(action: string, ...: any): { [string]: any }
	local ok, result = pcall(remote.InvokeServer, remote, action, ...)
	if ok and type(result) == "table" then
		return result
	end
	return { ok = false, error = "No reply from the server." }
end

local function formatDate(unix: any): string
	if type(unix) ~= "number" then
		return "an unknown date"
	end
	return DateTime.fromUnixTimestamp(unix):FormatLocalTime("D MMMM YYYY", "en-us")
end

---------------------------------------------------------------------------
-- The monitor
---------------------------------------------------------------------------
local gui: ScreenGui = new("ScreenGui", {
	Name = "RetroPC",
	ResetOnSpawn = false,
	IgnoreGuiInset = true,
	DisplayOrder = 50,
	Enabled = false,
	ZIndexBehavior = Enum.ZIndexBehavior.Sibling,
}, player:WaitForChild("PlayerGui"))

-- Dims the room. Modal = true frees the mouse in first person.
new("TextButton", {
	Size = UDim2.fromScale(1, 1),
	BackgroundColor3 = COLORS.dark,
	BackgroundTransparency = 0.35,
	Text = "",
	AutoButtonColor = false,
	Modal = true,
}, gui)

local BEZEL_X, BEZEL_TOP, BEZEL_BOTTOM = 40, 36, 74
local monitor: Frame = new("Frame", {
	AnchorPoint = Vector2.new(0.5, 0.5),
	Position = UDim2.fromScale(0.5, 0.5),
	Size = UDim2.fromOffset(SCREEN.X + BEZEL_X * 2, SCREEN.Y + BEZEL_TOP + BEZEL_BOTTOM),
	BackgroundColor3 = COLORS.bezel,
	BorderSizePixel = 0,
	ZIndex = 2, -- above the dimmed room
}, gui)
new("UICorner", { CornerRadius = UDim.new(0, 22) }, monitor)
local uiScale: UIScale = new("UIScale", {}, monitor)

local glassEdge = new("Frame", {
	Position = UDim2.fromOffset(BEZEL_X - 8, BEZEL_TOP - 8),
	Size = UDim2.fromOffset(SCREEN.X + 16, SCREEN.Y + 16),
	BackgroundColor3 = Color3.fromRGB(150, 144, 126),
	BorderSizePixel = 0,
}, monitor)
new("UICorner", { CornerRadius = UDim.new(0, 12) }, glassEdge)

local screen: Frame = new("Frame", {
	Position = UDim2.fromOffset(BEZEL_X, BEZEL_TOP),
	Size = UDim2.fromOffset(SCREEN.X, SCREEN.Y),
	BackgroundColor3 = COLORS.dark,
	BorderSizePixel = 0,
	ClipsDescendants = true,
}, monitor)

textLabel(monitor, "RetroView 15", UDim2.fromOffset(BEZEL_X, SCREEN.Y + BEZEL_TOP + 24), UDim2.fromOffset(200, 24), {
	Font = BOLD,
	TextSize = 18,
	TextColor3 = Color3.fromRGB(130, 122, 104),
})
textLabel(monitor, "POWER", UDim2.fromOffset(SCREEN.X - 60, SCREEN.Y + BEZEL_TOP + 24), UDim2.fromOffset(60, 24), {
	TextSize = 12,
	TextColor3 = Color3.fromRGB(130, 122, 104),
	TextXAlignment = Enum.TextXAlignment.Right,
})
local powerButton: TextButton = new("TextButton", {
	Position = UDim2.fromOffset(SCREEN.X + 8, SCREEN.Y + BEZEL_TOP + 20),
	Size = UDim2.fromOffset(32, 32),
	BackgroundColor3 = Color3.fromRGB(200, 192, 168),
	Text = "",
	AutoButtonColor = true,
}, monitor)
new("UICorner", { CornerRadius = UDim.new(1, 0) }, powerButton)
new("UIStroke", { Color = Color3.fromRGB(150, 144, 126), Thickness = 2 }, powerButton)
local powerLight = new("Frame", {
	Position = UDim2.fromOffset(SCREEN.X - 4, SCREEN.Y + BEZEL_TOP + 33),
	Size = UDim2.fromOffset(6, 6),
	BackgroundColor3 = Color3.fromRGB(60, 255, 90),
	BorderSizePixel = 0,
}, monitor)
new("UICorner", { CornerRadius = UDim.new(1, 0) }, powerLight)

local function updateScale()
	local camera = Workspace.CurrentCamera
	if camera then
		local viewport = camera.ViewportSize
		uiScale.Scale = math.min(viewport.X / (SCREEN.X + BEZEL_X * 2 + 20), viewport.Y / (SCREEN.Y + BEZEL_TOP + BEZEL_BOTTOM + 20))
	end
end

---------------------------------------------------------------------------
-- Session: everything that waits checks it is still the same session, so
-- turning the PC off stops every sequence that's running.
---------------------------------------------------------------------------
local isOpen = false
local session = 0
local currentModel: Model? = nil

local function alive(s: number): boolean
	return isOpen and session == s
end

local function sleep(seconds: number, s: number): boolean
	task.wait(seconds)
	return alive(s)
end

---------------------------------------------------------------------------
-- Boot screens
---------------------------------------------------------------------------
local bootLayer: TextButton = new("TextButton", {
	Size = UDim2.fromScale(1, 1),
	BackgroundColor3 = COLORS.dark,
	BorderSizePixel = 0,
	Text = "",
	AutoButtonColor = false,
	Visible = false,
	ZIndex = 50,
}, screen)
local bootText = textLabel(bootLayer, "", UDim2.fromOffset(16, 12), UDim2.new(1, -32, 1, -24), {
	Font = MONO,
	TextSize = 16,
	TextColor3 = COLORS.bios,
	TextYAlignment = Enum.TextYAlignment.Top,
})
local bootHint = textLabel(bootLayer, "Click to skip", UDim2.new(0, 16, 1, -28), UDim2.new(1, -32, 0, 20), {
	Font = MONO,
	TextSize = 14,
	TextColor3 = COLORS.shadow,
})

local splash: Frame = new("Frame", { Size = UDim2.fromScale(1, 1), BorderSizePixel = 0, Visible = false }, bootLayer)
new("UIGradient", {
	Rotation = 90,
	Color = ColorSequence.new(Color3.fromRGB(70, 130, 220), Color3.fromRGB(170, 210, 250)),
}, splash)
textLabel(splash, Config.OSName, UDim2.fromScale(0, 0.32), UDim2.new(1, 0, 0, 80), {
	Font = SERIF,
	TextSize = 72,
	TextColor3 = COLORS.white,
	TextXAlignment = Enum.TextXAlignment.Center,
	TextStrokeTransparency = 0.6,
})
textLabel(splash, "Starting up...", UDim2.fromScale(0, 0.52), UDim2.new(1, 0, 0, 30), {
	Font = FONT,
	TextSize = 18,
	TextColor3 = COLORS.white,
	TextXAlignment = Enum.TextXAlignment.Center,
})
local splashBar: Frame = new("Frame", {
	Position = UDim2.new(0, 0, 1, -18),
	Size = UDim2.new(1, 0, 0, 18),
	BackgroundColor3 = COLORS.white,
	BorderSizePixel = 0,
}, splash)
local splashGradient: UIGradient = new("UIGradient", {
	Color = ColorSequence.new({
		ColorSequenceKeypoint.new(0, Color3.fromRGB(0, 0, 128)),
		ColorSequenceKeypoint.new(0.5, Color3.fromRGB(80, 160, 255)),
		ColorSequenceKeypoint.new(1, Color3.fromRGB(0, 0, 128)),
	}),
}, splashBar)

-- Shown after "Shut Down": the famous orange-on-black message.
local offLayer: Frame = new("Frame", {
	Size = UDim2.fromScale(1, 1),
	BackgroundColor3 = COLORS.dark,
	BorderSizePixel = 0,
	Visible = false,
	ZIndex = 60,
}, screen)
textLabel(offLayer, "It's now safe to turn off\nyour computer.", UDim2.fromScale(0, 0.4), UDim2.new(1, 0, 0, 80), {
	Font = FONT,
	TextSize = 32,
	TextColor3 = Color3.fromRGB(255, 140, 0),
	TextXAlignment = Enum.TextXAlignment.Center,
})

local bootedBefore = false
local skipBoot = false
bootLayer.Activated:Connect(function()
	skipBoot = true
end)

local function runBoot(s: number): boolean
	skipBoot = false
	bootLayer.Visible = true
	bootText.Text = ""
	splash.Visible = false
	bootHint.Visible = true

	local function pause(seconds: number): boolean
		if skipBoot then
			return alive(s)
		end
		return sleep(seconds, s)
	end

	if not bootedBefore then
		local lines: { string } = {}
		local function show()
			bootText.Text = table.concat(lines, "\n")
		end
		local function add(line: string, seconds: number): boolean
			table.insert(lines, line)
			show()
			return pause(seconds)
		end

		local function run(steps: { { any } }): boolean
			for _, step in steps do
				if not add(step[1], step[2]) then
					return false
				end
			end
			return true
		end

		if not run({
			{ "RetroBIOS v4.51  (C) 1984-1996 Retro Software, Inc.", 0.3 },
			{ "", 0 },
			{ "486DX4-100 CPU at 100 MHz", 0.3 },
			{ "", 0 },
		}) then
			return false
		end
		-- The memory count rewrites its own line as it goes up.
		table.insert(lines, "")
		for kb = 1024, 16384, 1024 do
			lines[#lines] = string.format("Memory Test: %6dK", kb)
			show()
			if not pause(0.07) then
				return false
			end
		end
		lines[#lines] ..= " OK"
		if not run({
			{ "", 0.3 },
			{ "Detecting IDE Primary Master ... 540MB Hard Disk", 0.35 },
			{ "Detecting IDE Primary Slave  ... 4x CD-ROM", 0.35 },
			{ `Detecting COM2 ............... {Shared.commas(Config.ModemBitsPerSecond)} bps Fax/Modem`, 0.5 },
			{ "", 0 },
			{ `Starting {Config.OSName}...`, 0.8 },
		}) then
			return false
		end
	end

	bootText.Text = ""
	bootHint.Visible = false
	splash.Visible = true
	local duration = if bootedBefore then 0.8 else 2.4
	local start = os.clock()
	while os.clock() - start < duration and not skipBoot do
		splashGradient.Offset = Vector2.new(((os.clock() - start) * 0.9) % 2 - 1, 0)
		if not sleep(0.03, s) then
			return false
		end
	end
	splash.Visible = false
	bootLayer.Visible = false
	bootedBefore = true
	return alive(s)
end

---------------------------------------------------------------------------
-- Desktop, taskbar and Start menu
---------------------------------------------------------------------------
local desktop: Frame = new("Frame", {
	Size = UDim2.fromScale(1, 1),
	BackgroundColor3 = COLORS.desktop,
	BorderSizePixel = 0,
	Visible = false,
}, screen)
local iconLayer: Frame = new("Frame", { Size = UDim2.new(0, 90, 1, -TASKBAR), BackgroundTransparency = 1 }, desktop)
local windowLayer: Frame = new("Frame", {
	Size = UDim2.new(1, 0, 1, -TASKBAR),
	BackgroundTransparency = 1,
	ZIndex = 2,
}, desktop)

local taskbar: Frame = new("Frame", {
	Position = UDim2.new(0, 0, 1, -TASKBAR),
	Size = UDim2.new(1, 0, 0, TASKBAR),
	BackgroundColor3 = COLORS.face,
	BorderSizePixel = 0,
	ZIndex = 5,
}, desktop)
bevel(taskbar, "raised")

local taskButtons: Frame = new("Frame", {
	Position = UDim2.fromOffset(66, 3),
	Size = UDim2.new(1, -66 - 112, 0, 22),
	BackgroundTransparency = 1,
}, taskbar)
new("UIListLayout", {
	FillDirection = Enum.FillDirection.Horizontal,
	Padding = UDim.new(0, 3),
	SortOrder = Enum.SortOrder.LayoutOrder,
}, taskButtons)

local tray: Frame = new("Frame", {
	Position = UDim2.new(1, -106, 0, 3),
	Size = UDim2.fromOffset(102, 22),
	BackgroundColor3 = COLORS.face,
	BorderSizePixel = 0,
}, taskbar)
bevel(tray, "sunken")
local clockLabel = textLabel(tray, "", UDim2.fromOffset(34, 0), UDim2.new(1, -40, 1, 0), {
	TextXAlignment = Enum.TextXAlignment.Right,
	TextSize = 12,
})
-- Two tiny screens: the "connected" icon, its lights flicker with traffic.
local trayIcon: Frame = new("Frame", {
	Position = UDim2.fromOffset(6, 5),
	Size = UDim2.fromOffset(22, 12),
	BackgroundTransparency = 1,
	Visible = false,
}, tray)
local trayLights: { Frame } = {}
for i = 0, 1 do
	local light: Frame = new("Frame", {
		Position = UDim2.fromOffset(i * 12, 0),
		Size = UDim2.fromOffset(10, 9),
		BackgroundColor3 = Color3.fromRGB(0, 128, 128),
		BorderSizePixel = 0,
	}, trayIcon)
	new("UIStroke", { Color = COLORS.dark, Thickness = 1 }, light)
	table.insert(trayLights, light)
end

-- Start menu. The catcher is an invisible button over everything else
-- that closes the menu when you click away from it.
local startMenuCatcher: TextButton = new("TextButton", {
	Size = UDim2.fromScale(1, 1),
	BackgroundTransparency = 1,
	Text = "",
	Visible = false,
	ZIndex = 6,
}, desktop)
local START_ITEMS = 5
local startMenu: Frame = new("Frame", {
	Position = UDim2.new(0, 2, 1, -TASKBAR - (START_ITEMS * 26 + 12)),
	Size = UDim2.fromOffset(190, START_ITEMS * 26 + 12),
	BackgroundColor3 = COLORS.face,
	BorderSizePixel = 0,
	Visible = false,
	ZIndex = 7,
}, desktop)
bevel(startMenu, "raised")
local startBanner = new("Frame", {
	Position = UDim2.fromOffset(3, 3),
	Size = UDim2.new(0, 24, 1, -6),
	BackgroundColor3 = COLORS.shadow,
	BorderSizePixel = 0,
}, startMenu)
textLabel(startBanner, Config.OSName, UDim2.fromScale(0.5, 0.5), UDim2.fromOffset(START_ITEMS * 26 + 6, 24), {
	AnchorPoint = Vector2.new(0.5, 0.5),
	Rotation = -90,
	Font = BOLD,
	TextSize = 16,
	TextColor3 = COLORS.light,
	TextXAlignment = Enum.TextXAlignment.Left,
})

local function setStartMenu(visible: boolean)
	startMenu.Visible = visible
	startMenuCatcher.Visible = visible
end
startMenuCatcher.Activated:Connect(function()
	setStartMenu(false)
end)

local startItemCount = 0
local function startItem(text: string, onClick: () -> ()): TextButton
	local y = 6 + startItemCount * 26
	startItemCount += 1
	local item: TextButton = new("TextButton", {
		Position = UDim2.fromOffset(30, y),
		Size = UDim2.new(1, -33, 0, 26),
		BackgroundColor3 = COLORS.face,
		BorderSizePixel = 0,
		AutoButtonColor = false,
		Text = "   " .. text,
		Font = FONT,
		TextSize = 14,
		TextColor3 = COLORS.text,
		TextXAlignment = Enum.TextXAlignment.Left,
	}, startMenu)
	item.MouseEnter:Connect(function()
		item.BackgroundColor3 = COLORS.title
		item.TextColor3 = COLORS.white
	end)
	item.MouseLeave:Connect(function()
		item.BackgroundColor3 = COLORS.face
		item.TextColor3 = COLORS.text
	end)
	item.Activated:Connect(function()
		setStartMenu(false)
		item.BackgroundColor3 = COLORS.face
		item.TextColor3 = COLORS.text
		onClick()
	end)
	return item
end

local startButton = button(taskbar, "", UDim2.fromOffset(60, 22), UDim2.fromOffset(3, 3), function()
	setStartMenu(not startMenu.Visible)
end)
-- A little four-colour flag, then "Start".
for i, color in { Color3.fromRGB(255, 0, 0), Color3.fromRGB(0, 170, 0), Color3.fromRGB(0, 0, 255), Color3.fromRGB(255, 200, 0) } do
	new("Frame", {
		Position = UDim2.fromOffset(6 + ((i - 1) % 2) * 6, 5 + math.floor((i - 1) / 2) * 6),
		Size = UDim2.fromOffset(5, 5),
		BackgroundColor3 = color,
		BorderSizePixel = 0,
	}, startButton)
end
textLabel(startButton, "Start", UDim2.fromOffset(21, 0), UDim2.new(1, -21, 1, 0), { Font = BOLD, TextSize = 14 })

---------------------------------------------------------------------------
-- Windows
---------------------------------------------------------------------------
type Window = {
	id: string,
	frame: Frame,
	content: Frame,
	titleBar: Frame,
	titleLabel: TextLabel,
	taskButton: TextButton?,
	onClose: (() -> ())?,
}

local windows: { [string]: Window } = {}
local topZ = 1
local focused: Window? = nil
local drag: { window: Window, start: Vector3, origin: UDim2 }? = nil

local function focusWindow(win: Window)
	topZ += 1
	win.frame.ZIndex = topZ
	win.frame.Visible = true
	local previous = focused
	if previous and previous ~= win and previous.frame.Parent then
		previous.titleBar.BackgroundColor3 = COLORS.titleInactive
	end
	win.titleBar.BackgroundColor3 = COLORS.title
	focused = win
end

local function closeWindow(id: string)
	local win = windows[id]
	if not win then
		return
	end
	windows[id] = nil
	if focused == win then
		focused = nil
	end
	if drag and drag.window == win then
		drag = nil
	end
	local onClose = win.onClose
	if onClose then
		onClose()
	end
	win.frame:Destroy()
	if win.taskButton then
		win.taskButton:Destroy()
	end
end

-- Opens a window, or returns the one already open with this id.
-- The second value is true if the window is new.
local function openWindow(id: string, title: string, size: Vector2, position: Vector2?, inTaskbar: boolean): (Window, boolean)
	local existing = windows[id]
	if existing then
		focusWindow(existing)
		return existing, false
	end

	local pos = position or Vector2.new(math.floor((SCREEN.X - size.X) / 2), math.floor((SCREEN.Y - TASKBAR - size.Y) / 2))
	local frame: Frame = new("Frame", {
		Name = id,
		Position = UDim2.fromOffset(pos.X, pos.Y),
		Size = UDim2.fromOffset(size.X, size.Y),
		BackgroundColor3 = COLORS.face,
		BorderSizePixel = 0,
		Active = true,
	}, windowLayer)
	bevel(frame, "raised")

	local titleBar: Frame = new("Frame", {
		Position = UDim2.fromOffset(3, 3),
		Size = UDim2.new(1, -6, 0, 18),
		BackgroundColor3 = COLORS.title,
		BorderSizePixel = 0,
		Active = true,
	}, frame)
	local titleLabel = textLabel(titleBar, title, UDim2.fromOffset(4, 0), UDim2.new(1, -44, 1, 0), {
		Font = BOLD,
		TextSize = 13,
		TextColor3 = COLORS.white,
		TextTruncate = Enum.TextTruncate.AtEnd,
	})
	local content: Frame = new("Frame", {
		Position = UDim2.fromOffset(4, 23),
		Size = UDim2.new(1, -8, 1, -27),
		BackgroundTransparency = 1,
	}, frame)

	local win: Window = {
		id = id,
		frame = frame,
		content = content,
		titleBar = titleBar,
		titleLabel = titleLabel,
		taskButton = nil,
		onClose = nil,
	}
	windows[id] = win

	local close = button(titleBar, "X", UDim2.fromOffset(16, 14), UDim2.new(1, -18, 0, 2), function()
		closeWindow(id)
	end)
	close.Font = BOLD
	close.TextSize = 11

	if inTaskbar then
		local minimize = button(titleBar, "_", UDim2.fromOffset(16, 14), UDim2.new(1, -36, 0, 2), function()
			frame.Visible = false
		end)
		minimize.Font = BOLD
		minimize.TextSize = 11
		local taskButton = button(taskButtons, title, UDim2.fromOffset(150, 22), nil, function()
			if focused == win and frame.Visible then
				frame.Visible = false
			else
				focusWindow(win)
			end
		end)
		taskButton.TextTruncate = Enum.TextTruncate.AtEnd
		taskButton.TextXAlignment = Enum.TextXAlignment.Left
		new("UIPadding", { PaddingLeft = UDim.new(0, 6) }, taskButton)
		taskButton.LayoutOrder = topZ
		win.taskButton = taskButton
	end

	frame.InputBegan:Connect(function(input: InputObject)
		if input.UserInputType == Enum.UserInputType.MouseButton1 or input.UserInputType == Enum.UserInputType.Touch then
			focusWindow(win)
		end
	end)
	titleBar.InputBegan:Connect(function(input: InputObject)
		if input.UserInputType == Enum.UserInputType.MouseButton1 or input.UserInputType == Enum.UserInputType.Touch then
			drag = { window = win, start = input.Position, origin = frame.Position }
		end
	end)

	focusWindow(win)
	return win, true
end

-- Dragging: mouse moves are in real screen pixels, so divide by the scale.
UserInputService.InputChanged:Connect(function(input: InputObject)
	local d = drag
	if not d then
		return
	end
	if input.UserInputType ~= Enum.UserInputType.MouseMovement and input.UserInputType ~= Enum.UserInputType.Touch then
		return
	end
	local delta = (input.Position - d.start) / uiScale.Scale
	local size = d.window.frame.AbsoluteSize / uiScale.Scale
	local x = math.clamp(d.origin.X.Offset + delta.X, 60 - size.X, SCREEN.X - 60)
	local y = math.clamp(d.origin.Y.Offset + delta.Y, 0, SCREEN.Y - TASKBAR - 20)
	d.window.frame.Position = UDim2.fromOffset(math.floor(x), math.floor(y))
end)
UserInputService.InputEnded:Connect(function(input: InputObject)
	if input.UserInputType == Enum.UserInputType.MouseButton1 or input.UserInputType == Enum.UserInputType.Touch then
		drag = nil
	end
end)

-- A message box with an icon and buttons.
local messageCount = 0
local function messageBox(title: string, text: string, icon: "error" | "warning" | "info")
	messageCount += 1
	local id = "message" .. messageCount
	local win = openWindow(id, title, Vector2.new(360, 150), nil, false)
	local styles: { [string]: { back: Color3, fore: Color3, symbol: string } } = {
		error = { back = Color3.fromRGB(220, 0, 0), fore = COLORS.white, symbol = "X" },
		warning = { back = Color3.fromRGB(255, 220, 0), fore = COLORS.dark, symbol = "!" },
		info = { back = COLORS.white, fore = Color3.fromRGB(0, 0, 200), symbol = "i" },
	}
	local style = styles[icon]
	local badge = textLabel(win.content, style.symbol, UDim2.fromOffset(12, 14), UDim2.fromOffset(34, 34), {
		BackgroundTransparency = 0,
		BackgroundColor3 = style.back,
		TextColor3 = style.fore,
		Font = BOLD,
		TextSize = 22,
		TextXAlignment = Enum.TextXAlignment.Center,
	})
	new("UICorner", { CornerRadius = UDim.new(1, 0) }, badge)
	new("UIStroke", { Color = COLORS.dark, Thickness = 1 }, badge)
	textLabel(win.content, text, UDim2.fromOffset(60, 10), UDim2.new(1, -70, 0, 72), {
		TextWrapped = true,
		TextYAlignment = Enum.TextYAlignment.Top,
	})
	button(win.content, "OK", UDim2.fromOffset(80, 24), UDim2.new(0.5, -40, 1, -32), function()
		closeWindow(id)
	end)
end

---------------------------------------------------------------------------
-- Desktop icons (double-click to open, like the real thing)
---------------------------------------------------------------------------
local iconLabels: { TextLabel } = {}
local iconCount = 0

local function desktopIcon(text: string, draw: (Frame) -> (), onOpen: () -> ())
	local b: TextButton = new("TextButton", {
		Position = UDim2.fromOffset(10, 12 + iconCount * 84),
		Size = UDim2.fromOffset(74, 72),
		BackgroundTransparency = 1,
		Text = "",
		AutoButtonColor = false,
	}, iconLayer)
	iconCount += 1
	local picture: Frame = new("Frame", {
		Position = UDim2.fromOffset(21, 2),
		Size = UDim2.fromOffset(32, 32),
		BackgroundTransparency = 1,
	}, b)
	draw(picture)
	local label = textLabel(b, text, UDim2.fromOffset(2, 38), UDim2.fromOffset(70, 30), {
		TextColor3 = COLORS.white,
		TextSize = 12,
		TextWrapped = true,
		TextXAlignment = Enum.TextXAlignment.Center,
		TextYAlignment = Enum.TextYAlignment.Top,
		BackgroundColor3 = COLORS.title,
	})
	table.insert(iconLabels, label)

	local lastClick = 0
	b.Activated:Connect(function()
		for _, other in iconLabels do
			other.BackgroundTransparency = 1
		end
		label.BackgroundTransparency = 0
		local now = os.clock()
		if now - lastClick < 0.45 then
			lastClick = 0
			onOpen()
		else
			lastClick = now
		end
	end)
end

local function drawGlobe(frame: Frame)
	local globe = textLabel(frame, "B", UDim2.fromOffset(2, 2), UDim2.fromOffset(28, 28), {
		BackgroundTransparency = 0,
		BackgroundColor3 = Color3.fromRGB(0, 0, 170),
		TextColor3 = COLORS.white,
		Font = SERIF,
		TextSize = 24,
		TextXAlignment = Enum.TextXAlignment.Center,
	})
	new("UICorner", { CornerRadius = UDim.new(1, 0) }, globe)
	new("UIStroke", { Color = COLORS.dark, Thickness = 1 }, globe)
end

local function drawComputers(frame: Frame)
	for i, offset in { Vector2.new(0, 0), Vector2.new(12, 10) } do
		local case: Frame = new("Frame", {
			Position = UDim2.fromOffset(offset.X, offset.Y),
			Size = UDim2.fromOffset(20, 16),
			BackgroundColor3 = COLORS.bezel,
			BorderSizePixel = 0,
			ZIndex = i,
		}, frame)
		new("UIStroke", { Color = COLORS.dark, Thickness = 1 }, case)
		new("Frame", {
			Position = UDim2.fromOffset(3, 3),
			Size = UDim2.fromOffset(14, 9),
			BackgroundColor3 = COLORS.desktop,
			BorderSizePixel = 0,
		}, case)
	end
end

local function drawNotepad(frame: Frame)
	local page: Frame = new("Frame", {
		Position = UDim2.fromOffset(5, 2),
		Size = UDim2.fromOffset(22, 28),
		BackgroundColor3 = COLORS.white,
		BorderSizePixel = 0,
	}, frame)
	new("UIStroke", { Color = COLORS.dark, Thickness = 1 }, page)
	new("Frame", { Size = UDim2.new(1, 0, 0, 4), BackgroundColor3 = Color3.fromRGB(0, 0, 200), BorderSizePixel = 0 }, page)
	for i = 1, 4 do
		new("Frame", {
			Position = UDim2.fromOffset(3, 6 + i * 4),
			Size = UDim2.fromOffset(16, 1),
			BackgroundColor3 = COLORS.shadow,
			BorderSizePixel = 0,
		}, page)
	end
end

---------------------------------------------------------------------------
-- Dial-up networking
---------------------------------------------------------------------------
local connected = false
local connecting = false
local connectedAt = 0
local dialToken = 0
local afterConnect: (() -> ())? = nil
local dialSound: Sound? = nil
local dialStatus: TextLabel? = nil
local dialNoise: TextLabel? = nil
local dialDuration: TextLabel? = nil

local refreshDialUp: () -> ()

local function stopDialSound()
	if dialSound then
		dialSound:Destroy()
		dialSound = nil
	end
end

local function cancelDial()
	dialToken += 1
	connecting = false
	stopDialSound()
	refreshDialUp()
end

local function hangUp()
	dialToken += 1
	connected = false
	connecting = false
	afterConnect = nil
	stopDialSound()
	trayIcon.Visible = false
	refreshDialUp()
end

local function dial()
	if connecting or connected then
		return
	end
	connecting = true
	dialToken += 1
	local token = dialToken
	local s = session
	refreshDialUp()

	if Config.DialUpSoundId ~= "" then
		-- Made by this LocalScript, so only this player hears it.
		dialSound = new("Sound", { SoundId = Config.DialUpSoundId, Volume = 0.6 }, SoundService)
		if dialSound then
			dialSound:Play()
		end
	end

	-- The steps the dialog shows, and when (as a fraction of DialSeconds).
	local steps = {
		{ at = 0, status = "Dialing...", noise = "(dial tone... beep boop beep boop beep)" },
		{
			at = 0.35,
			status = "Verifying user name and password...",
			noise = "(SKREEEEE-ONK... bee-dee-dee... kshhhhhhhhhh)",
		},
		{ at = 0.75, status = "Logging on to network...", noise = "(kshhhhh... click.)" },
		{ at = 1, status = "", noise = "" },
	}
	for i = 1, #steps - 1 do
		local status, noise = dialStatus, dialNoise
		if status then
			status.Text = "Status: " .. steps[i].status
		end
		if noise then
			noise.Text = steps[i].noise
		end
		if not sleep((steps[i + 1].at - steps[i].at) * Config.DialSeconds, s) or token ~= dialToken then
			return
		end
	end

	connecting = false
	connected = true
	connectedAt = os.clock()
	stopDialSound()
	trayIcon.Visible = true
	refreshDialUp()

	local callback = afterConnect
	afterConnect = nil
	if callback then
		local win = windows["dialup"]
		if win then
			win.frame.Visible = false -- tuck the dialog away, like it did
		end
		callback()
	end
end

-- Draws the Dial-Up window for the current state.
refreshDialUp = function()
	local win = windows["dialup"]
	dialStatus, dialNoise, dialDuration = nil, nil, nil
	if not win then
		return
	end
	win.content:ClearAllChildren()
	local c = win.content
	local pic: Frame = new("Frame", { Position = UDim2.fromOffset(12, 8), Size = UDim2.fromOffset(32, 32), BackgroundTransparency = 1 }, c)
	drawComputers(pic)

	if connected then
		win.titleLabel.Text = "Connected to My Connection"
		textLabel(c, `Connected at {Shared.commas(Config.ModemBitsPerSecond)} bps`, UDim2.fromOffset(60, 12), UDim2.fromOffset(250, 18), { Font = BOLD })
		dialDuration = textLabel(c, "Duration: 000:00:00", UDim2.fromOffset(60, 36), UDim2.fromOffset(250, 18))
		textLabel(c, `You are online. Open {Config.WikiName} from the desktop.`, UDim2.fromOffset(60, 70), UDim2.fromOffset(250, 40), {
			TextWrapped = true,
			TextYAlignment = Enum.TextYAlignment.Top,
		})
		button(c, "OK", UDim2.fromOffset(84, 24), UDim2.new(0.5, -94, 1, -34), function()
			win.frame.Visible = false
		end)
		button(c, "Disconnect", UDim2.fromOffset(84, 24), UDim2.new(0.5, 10, 1, -34), hangUp)
	elseif connecting then
		win.titleLabel.Text = "Connecting to My Connection"
		textLabel(c, "Connecting to My Connection...", UDim2.fromOffset(60, 12), UDim2.fromOffset(250, 18), { Font = BOLD })
		dialStatus = textLabel(c, "Status: Dialing...", UDim2.fromOffset(60, 40), UDim2.fromOffset(250, 18))
		dialNoise = textLabel(c, "", UDim2.fromOffset(60, 66), UDim2.fromOffset(250, 40), {
			Font = SERIF,
			TextSize = 15,
			TextColor3 = COLORS.shadow,
			TextWrapped = true,
			TextYAlignment = Enum.TextYAlignment.Top,
		})
		button(c, "Cancel", UDim2.fromOffset(84, 24), UDim2.new(0.5, -42, 1, -34), cancelDial)
	else
		win.titleLabel.Text = "Connect To"
		textLabel(c, "My Connection", UDim2.fromOffset(60, 14), UDim2.fromOffset(250, 18), { Font = BOLD, TextSize = 14 })
		local rows = {
			{ "User name:", player.Name },
			{ "Password:", "********" },
			{ "Phone number:", Config.PhoneNumber },
			{ "Dialing from:", "Default Location" },
		}
		for i, row in rows do
			local y = 48 + (i - 1) * 28
			textLabel(c, row[1], UDim2.fromOffset(12, y), UDim2.fromOffset(96, 20))
			field(c, {
				Position = UDim2.fromOffset(112, y),
				Size = UDim2.fromOffset(196, 20),
				Text = row[2],
				TextEditable = false,
			})
		end
		button(c, "Connect", UDim2.fromOffset(84, 24), UDim2.new(0.5, -94, 1, -34), function()
			task.spawn(dial)
		end)
		button(c, "Cancel", UDim2.fromOffset(84, 24), UDim2.new(0.5, 10, 1, -34), function()
			afterConnect = nil
			closeWindow("dialup")
		end)
	end
end

local function openDialUp()
	local win, isNew = openWindow("dialup", "Connect To", Vector2.new(330, 240), nil, true)
	if isNew then
		refreshDialUp()
	end
	focusWindow(win)
end

---------------------------------------------------------------------------
-- The browser
---------------------------------------------------------------------------
type Page = { kind: string, arg: string? }

local isEditor = false
local savedToDataStore = true
local history: { Page } = {}
local historyIndex = 0
local navToken = 0
local loading = false

local indexTitles: { string } = {}
local indexRecent: { any } = {}
local titleSet: { [string]: string } = {} -- lower case -> real title
local indexAt = -math.huge

type BrowserParts = {
	window: Window,
	scroll: ScrollingFrame,
	address: TextBox,
	status: TextLabel,
	progress: Frame,
	throbberDot: Frame,
	back: TextButton,
	forward: TextButton,
	newButton: TextButton,
}
local browser: BrowserParts? = nil

local navigate: (Page, string?) -> ()

local function setStatus(text: string)
	local b = browser
	if b then
		b.status.Text = text
	end
end

local function setProgress(fraction: number)
	local b = browser
	if b then
		b.progress.Size = UDim2.fromScale(math.clamp(fraction, 0, 1), 1)
	end
end

local function refreshIndex(force: boolean): boolean
	if not force and os.clock() - indexAt < 10 then
		return true
	end
	local result = request("index")
	if not result.ok then
		return false
	end
	indexTitles = result.titles or {}
	indexRecent = result.recent or {}
	table.clear(titleSet)
	for _, title in indexTitles do
		titleSet[string.lower(title)] = title
	end
	indexAt = os.clock()
	return true
end

local function exists(title: string): boolean
	return titleSet[string.lower(title)] ~= nil
end

local function urlFor(page: Page): string
	local arg = page.arg or ""
	local path = if page.kind == "article"
		then "/wiki/" .. string.gsub(arg, " ", "_")
		elseif page.kind == "search" then "/search?q=" .. string.gsub(arg, " ", "+")
		elseif page.kind == "all" then "/wiki/All_Pages"
		elseif page.kind == "help" then "/wiki/Help"
		elseif page.kind == "edit" then "/edit/" .. string.gsub(arg, " ", "_")
		elseif page.kind == "new" then "/edit/new"
		else "/wiki/Main_Page"
	return "http://" .. HOST .. path
end

-- Works out a page from whatever was typed in the Location box.
local function pageFromAddress(text: string): Page
	local s = Shared.trim(text)
	s = string.gsub(s, "^[Hh][Tt][Tt][Pp][Ss]?://", "")
	if string.lower(string.sub(s, 1, #HOST)) == string.lower(HOST) then
		s = string.sub(s, #HOST + 1)
	end
	local wiki = string.match(s, "^/wiki/(.*)$")
	local query = string.match(s, "^/search%?q=(.*)$")
	local edit = string.match(s, "^/edit/(.*)$")
	if query then
		return { kind = "search", arg = (string.gsub(query, "%+", " ")) }
	elseif edit then
		if edit == "new" then
			return { kind = "new" }
		end
		return { kind = "edit", arg = (string.gsub(edit, "_", " ")) }
	end
	local name = string.lower(Shared.trim((string.gsub(wiki or s, "[_/]", " "))))
	if name == "" or name == "main page" then
		return { kind = "main" }
	elseif name == "all pages" then
		return { kind = "all" }
	elseif name == "help" then
		return { kind = "help" }
	elseif name == "random" then
		return { kind = "random" }
	elseif titleSet[name] then
		return { kind = "article", arg = titleSet[name] }
	elseif wiki then
		return { kind = "article", arg = Shared.trim((string.gsub(wiki, "_", " "))) }
	end
	return { kind = "search", arg = Shared.trim(text) }
end

-- Page building. Each piece is added in order, hidden, and shown once
-- enough "bytes" have arrived over the modem.
local pageItems: { { inst: GuiObject, bytes: number } } = {}
local layoutCount = 0

local function clearPage()
	local b = browser
	if not b then
		return
	end
	for _, child in b.scroll:GetChildren() do
		if child:IsA("GuiObject") then
			child:Destroy()
		end
	end
	table.clear(pageItems)
	layoutCount = 0
	b.scroll.CanvasPosition = Vector2.zero
end

local function add(inst: GuiObject, bytes: number): GuiObject
	local b = browser
	layoutCount += 1
	inst.LayoutOrder = layoutCount
	inst.Visible = false
	if b then
		inst.Parent = b.scroll
	end
	table.insert(pageItems, { inst = inst, bytes = math.max(bytes, 40) })
	return inst
end

-- Building blocks. `rich` is rich text and must already be escaped.
local function mkText(rich: string, size: number?, font: Enum.Font?, color: Color3?): TextLabel
	return new("TextLabel", {
		BackgroundTransparency = 1,
		RichText = true,
		Text = rich,
		Font = font or SERIF,
		TextSize = size or 18,
		TextColor3 = color or COLORS.text,
		TextWrapped = true,
		TextXAlignment = Enum.TextXAlignment.Left,
		TextYAlignment = Enum.TextYAlignment.Top,
		Size = UDim2.fromScale(1, 0),
		AutomaticSize = Enum.AutomaticSize.Y,
	})
end

local function mkLink(text: string, onClick: () -> (), color: Color3?, size: number?): TextButton
	local link: TextButton = new("TextButton", {
		BackgroundTransparency = 1,
		RichText = true,
		Text = "<u>" .. Shared.escapeRichText(text) .. "</u>",
		Font = SERIF,
		TextSize = size or 18,
		TextColor3 = color or COLORS.link,
		TextXAlignment = Enum.TextXAlignment.Left,
		AutoButtonColor = false,
		Size = UDim2.fromOffset(0, 0),
		AutomaticSize = Enum.AutomaticSize.XY,
	})
	link.Activated:Connect(onClick)
	return link
end

local function mkRule(): Frame
	local rule: Frame = new("Frame", { Size = UDim2.new(1, 0, 0, 2), BackgroundColor3 = COLORS.shadow, BorderSizePixel = 0 })
	new("Frame", { Position = UDim2.fromOffset(0, 1), Size = UDim2.new(1, 0, 0, 1), BackgroundColor3 = COLORS.white, BorderSizePixel = 0 }, rule)
	return rule
end

-- A row of things side by side (links, labels, buttons).
local function mkRow(children: { GuiObject }, indent: number?): Frame
	local row: Frame = new("Frame", {
		BackgroundTransparency = 1,
		Size = UDim2.fromScale(1, 0),
		AutomaticSize = Enum.AutomaticSize.Y,
	})
	new("UIListLayout", {
		FillDirection = Enum.FillDirection.Horizontal,
		Padding = UDim.new(0, 8),
		SortOrder = Enum.SortOrder.LayoutOrder,
		VerticalAlignment = Enum.VerticalAlignment.Center,
	}, row)
	if indent then
		new("UIPadding", { PaddingLeft = UDim.new(0, indent) }, row)
	end
	for i, child in children do
		child.LayoutOrder = i
		child.Parent = row
	end
	return row
end

-- Bullet point: a dot and wrapped text beside it.
local function mkBullet(rich: string): Frame
	local holder: Frame = new("Frame", {
		BackgroundTransparency = 1,
		Size = UDim2.fromScale(1, 0),
		AutomaticSize = Enum.AutomaticSize.Y,
	})
	textLabel(holder, "•", UDim2.fromOffset(14, 0), UDim2.fromOffset(12, 20), { Font = SERIF, TextSize = 18 })
	local text = mkText(rich)
	text.Position = UDim2.fromOffset(30, 0)
	text.Size = UDim2.new(1, -30, 0, 0)
	text.Parent = holder
	return holder
end

local function mkButton(text: string, onClick: () -> ()): TextButton
	return button(nil :: any, text, UDim2.fromOffset(#text * 7 + 26, 24), nil, onClick)
end

local function heading(text: string, size: number?)
	add(mkText(Shared.escapeRichText(text), size or 26), #text)
	add(mkRule(), 0)
end

-- Blockopedia's banner with a search box.
local function siteHeader()
	local bar: Frame = new("Frame", { BackgroundTransparency = 1, Size = UDim2.new(1, 0, 0, 46) })
	textLabel(bar, Config.WikiName, UDim2.fromOffset(0, 0), UDim2.new(1, -280, 0, 30), { Font = SERIF, TextSize = 30 })
	textLabel(bar, Config.WikiName .. ", " .. Config.WikiSlogan, UDim2.fromOffset(2, 30), UDim2.new(1, -280, 0, 14), {
		Font = SERIF,
		TextSize = 13,
		TextColor3 = COLORS.shadow,
	})
	local search = field(bar, {
		Position = UDim2.new(1, -262, 0, 8),
		Size = UDim2.fromOffset(180, 22),
		PlaceholderText = "Search " .. Config.WikiName,
	})
	local function go()
		if Shared.trim(search.Text) ~= "" then
			navigate({ kind = "search", arg = Shared.trim(search.Text) })
		end
	end
	search.FocusLost:Connect(function(enterPressed: boolean)
		if enterPressed then
			go()
		end
	end)
	button(bar, "Search", UDim2.fromOffset(70, 22), UDim2.new(1, -74, 0, 8), go)
	add(bar, 300)
	add(mkRule(), 0)
end

local function footer(text: string)
	add(mkRule(), 0)
	add(mkText(Shared.escapeRichText(text), 14, SERIF, COLORS.shadow), #text)
end

local function articleLink(title: string): TextButton
	return mkLink(title, function()
		navigate({ kind = "article", arg = title })
	end, if exists(title) then COLORS.link else Color3.fromRGB(204, 0, 0))
end

-- Turns parsed blocks into page pieces. `put` adds each one (to the page,
-- or to the preview box in the editor).
local function renderBlocks(parsed: Shared.ParsedArticle, put: (GuiObject, number) -> ())
	for _, block in parsed.blocks do
		if block.kind == "heading" then
			put(mkText(block.text, 24), #block.text)
			put(mkRule(), 0)
		elseif block.kind == "subheading" then
			put(mkText("<b>" .. block.text .. "</b>", 20), #block.text)
		elseif block.kind == "bullet" then
			put(mkBullet(block.text), #block.text)
		elseif block.kind == "rule" then
			put(mkRule(), 0)
		else
			put(mkText(block.text), #block.text)
		end
	end
end

-- Shows the page over Config.PageBytesPerSecond, with the status bar
-- counting up like a real transfer.
local function reveal(token: number)
	local total = 0
	for _, item in pageItems do
		total += item.bytes
	end
	local duration = Shared.loadSeconds(total)
	local start = os.clock()
	local shown, cumulative = 0, 0
	while true do
		if token ~= navToken or not isOpen then
			return
		end
		local t = math.min((os.clock() - start) / duration, 1)
		local arrived = total * t
		while shown < #pageItems and cumulative + pageItems[shown + 1].bytes <= arrived + 1 do
			shown += 1
			cumulative += pageItems[shown].bytes
			pageItems[shown].inst.Visible = true
		end
		setStatus(
			string.format(
				"Transferring data from %s: %s of %s (%.1f KB/sec)",
				HOST,
				Shared.formatBytes(arrived),
				Shared.formatBytes(total),
				total / duration / 1024
			)
		)
		setProgress(t)
		if t >= 1 then
			break
		end
		task.wait(0.05)
	end
	for _, item in pageItems do
		item.inst.Visible = true
	end
	setStatus("Document: Done")
	setProgress(0)
	loading = false
end

local function waitNav(seconds: number, token: number): boolean
	task.wait(seconds)
	return token == navToken and isOpen
end

local function errorPage(title: string, text: string)
	add(mkText(Shared.escapeRichText(title), 28, BOLD), #title)
	add(mkText(text, 18), #text)
end

---------------------------------------------------------------------------
-- Pages
---------------------------------------------------------------------------
local function renderMain()
	siteHeader()
	add(mkText("Welcome to " .. Config.WikiName .. ",", 30), 40)
	add(mkText(`the free encyclopedia that anyone can read. <b>{#indexTitles}</b> articles so far.`, 18), 60)
	add(mkText(" ", 8), 0)
	heading("Recently changed", 22)
	if #indexRecent == 0 then
		add(mkText(if isEditor then "No articles yet. Press <b>New</b> on the toolbar to write the first one." else "No articles yet.", 18), 60)
	end
	for _, item in indexRecent do
		if type(item) == "table" and type(item.title) == "string" then
			local meta = textLabel(nil :: any, "", UDim2.new(), UDim2.new(), {
				Text = `- {formatDate(item.updated)}, by {tostring(item.author)}`,
				Font = SERIF,
				TextSize = 15,
				TextColor3 = COLORS.shadow,
				AutomaticSize = Enum.AutomaticSize.XY,
			})
			add(mkRow({ articleLink(item.title), meta }, 14), 80)
		end
	end
	add(mkText(" ", 8), 0)
	heading("Browse", 22)
	add(
		mkRow({
			mkLink("All pages", function()
				navigate({ kind = "all" })
			end),
			mkLink("Random article", function()
				navigate({ kind = "random" })
			end),
			mkLink("Help with editing", function()
				navigate({ kind = "help" })
			end),
		}, 14),
		60
	)
	add(mkText(" ", 8), 0)
	footer(`Best viewed with {Config.BrowserName} 2.0 at 800 x 600. Hosted at {HOST}.`)
end

local function renderMissing(title: string)
	siteHeader()
	heading(title)
	add(mkText(`{Config.WikiName} does not have an article with this exact name.`, 18), 60)
	add(
		mkRow({
			mkLink(`Search for "{title}"`, function()
				navigate({ kind = "search", arg = title })
			end),
		}),
		40
	)
	if isEditor then
		add(
			mkButton("Start this article", function()
				navigate({ kind = "new", arg = title })
			end),
			40
		)
	end
end

local function renderArticle(token: number, title: string)
	local result = request("get", title)
	refreshIndex(false)
	if token ~= navToken then
		return
	end
	if not result.ok then
		siteHeader()
		errorPage("Server error", Shared.escapeRichText(result.error or "Unknown error."))
		return
	end
	local entry = result.entry
	if type(entry) ~= "table" then
		renderMissing(title)
		return
	end

	siteHeader()
	add(mkText(Shared.escapeRichText(entry.title), 30), #entry.title)
	add(mkText(`<i>From {Config.WikiName}, {Config.WikiSlogan}</i>`, 14, SERIF, COLORS.shadow), 40)
	add(mkRule(), 0)
	if isEditor then
		add(
			mkButton("Edit this page", function()
				navigate({ kind = "edit", arg = entry.title })
			end),
			40
		)
	end

	local parsed = Shared.parseArticle(entry.body, exists)
	renderBlocks(parsed, function(inst: GuiObject, bytes: number)
		add(inst, bytes)
	end)

	if #parsed.links > 0 then
		add(mkText(" ", 8), 0)
		add(mkText("<b>Links on this page</b>", 18), 30)
		for _, link in parsed.links do
			add(mkRow({ articleLink(link) }, 14), #link)
		end
	end
	add(mkText(" ", 8), 0)
	footer(`This page was last edited on {formatDate(entry.updated)} by {tostring(entry.author)} (revision {tostring(entry.revision)}).`)
end

local function renderAll()
	siteHeader()
	heading("All pages")
	if #indexTitles == 0 then
		add(mkText("There are no articles yet.", 18), 30)
	end
	local letter = ""
	for _, title in indexTitles do
		local first = string.upper(string.sub(title, 1, 1))
		if first ~= letter then
			letter = first
			add(mkText(`<b>{Shared.escapeRichText(letter)}</b>`, 20), 10)
		end
		add(mkRow({ articleLink(title) }, 14), #title)
	end
end

local function renderSearch(token: number, query: string)
	local result = request("search", query)
	if token ~= navToken then
		return
	end
	siteHeader()
	heading("Search results")
	if not result.ok then
		add(mkText(Shared.escapeRichText(result.error or "Search failed."), 18), 30)
		return
	end
	local results: { string } = result.results or {}
	add(mkText(`{#results} result(s) for <b>{Shared.escapeRichText(query)}</b>`, 18), 40)
	for _, title in results do
		add(mkRow({ articleLink(title) }, 14), #title)
	end
	local clean = Shared.normalizeTitle(query)
	if isEditor and clean and not exists(clean) then
		add(mkText(" ", 8), 0)
		add(
			mkButton(`Create the page "{clean}"`, function()
				navigate({ kind = "new", arg = clean })
			end),
			40
		)
	end
end

local function renderHelp()
	siteHeader()
	heading("Help: editing")
	add(mkText(`Only the site editor can write pages: {Shared.escapeRichText(table.concat(Config.EditorUsernames, ", "))}.`, 18), 60)
	add(mkText("Type this in the editor to get the effect on the right:", 18), 60)
	local rows = {
		{ "'''bold'''", "<b>bold</b>" },
		{ "''italic''", "<i>italic</i>" },
		{ "[[Other page]]", `<font color="#0000EE"><u>Other page</u></font>` },
		{ "[[Other page|click me]]", `<font color="#0000EE"><u>click me</u></font>` },
		{ "== Heading ==", "a section heading" },
		{ "=== Sub heading ===", "a smaller heading" },
		{ "* item", "a bullet point" },
		{ "----", "a horizontal line" },
		{ "(blank line)", "starts a new paragraph" },
	}
	for _, row in rows do
		local code = textLabel(nil :: any, row[1], UDim2.new(), UDim2.fromOffset(220, 22), { Font = MONO, TextSize = 15 })
		local result = mkText(row[2])
		result.Size = UDim2.fromOffset(300, 0)
		add(mkRow({ code, result }, 14), 40)
	end
	add(mkText(" ", 8), 0)
	add(
		mkText(
			`Links to pages that don't exist yet are <font color="#CC0000">red</font>. Titles can use letters, digits, spaces and - ( ) . , ' ! and up to {Config.TitleMaxLength} characters.`,
			18
		),
		100
	)
end

local function renderForbidden()
	add(mkText("403 Forbidden", 30, BOLD), 20)
	add(mkText("You don't have permission to edit pages on this server.", 18), 60)
	add(mkRule(), 0)
	add(mkText(`<i>RetroHTTPd/1.0 Server at {HOST} Port 80</i>`, 15), 40)
end

local function renderEditor(token: number, mode: "create" | "edit", title: string?)
	if not isEditor then
		renderForbidden()
		return
	end

	local startBody = ""
	local startTitle = title or ""
	if mode == "edit" then
		local result = request("get", title or "")
		if token ~= navToken then
			return
		end
		local entry = result.entry
		if not result.ok or type(entry) ~= "table" then
			siteHeader()
			errorPage("Can't edit", Shared.escapeRichText(result.error or "That page doesn't exist."))
			return
		end
		startBody = entry.body
		startTitle = entry.title
	end

	siteHeader()
	heading(if mode == "edit" then "Editing " .. startTitle else "Creating a new article")

	add(mkText("<b>Title</b>", 16, FONT), 10)
	local titleBox = field(nil, {
		Size = UDim2.new(1, 0, 0, 24),
		Text = startTitle,
		TextEditable = mode == "create",
		BackgroundColor3 = if mode == "create" then COLORS.white else COLORS.light,
		PlaceholderText = "Page title",
	})
	add(titleBox, 20)

	add(mkText("<b>Article</b>   (see the Help page for '''bold''', [[links]] and == headings ==)", 16, FONT), 30)
	local bodyBox = field(nil, {
		Size = UDim2.new(1, 0, 0, 240), -- grows taller as you type
		AutomaticSize = Enum.AutomaticSize.Y,
		Text = startBody,
		MultiLine = true,
		TextWrapped = true,
		TextYAlignment = Enum.TextYAlignment.Top,
		Font = MONO,
		TextSize = 14,
		PlaceholderText = "Write the article here...",
	})
	add(bodyBox, 100)
	local counter = mkText("", 13, FONT, COLORS.shadow)
	add(counter, 10)
	local function updateCounter()
		counter.Text = `{#bodyBox.Text} / {Config.BodyMaxLength} characters`
		counter.TextColor3 = if #bodyBox.Text > Config.BodyMaxLength then Color3.fromRGB(204, 0, 0) else COLORS.shadow
	end
	bodyBox:GetPropertyChangedSignal("Text"):Connect(updateCounter)
	updateCounter()

	local preview: Frame = new("Frame", {
		BackgroundColor3 = Color3.fromRGB(255, 255, 225),
		BorderSizePixel = 0,
		Size = UDim2.fromScale(1, 0),
		AutomaticSize = Enum.AutomaticSize.Y,
		Visible = false,
	})
	new("UIStroke", { Color = COLORS.shadow, Thickness = 1 }, preview)
	new("UIPadding", {
		PaddingLeft = UDim.new(0, 8),
		PaddingRight = UDim.new(0, 8),
		PaddingTop = UDim.new(0, 6),
		PaddingBottom = UDim.new(0, 6),
	}, preview)
	new("UIListLayout", { Padding = UDim.new(0, 4), SortOrder = Enum.SortOrder.LayoutOrder }, preview)

	local saving = false
	local saveButton: TextButton
	local function showPreview()
		for _, child in preview:GetChildren() do
			if child:IsA("GuiObject") then
				child:Destroy()
			end
		end
		local order = 0
		local function put(inst: GuiObject, _bytes: number)
			order += 1
			inst.LayoutOrder = order
			inst.Parent = preview
		end
		put(mkText("<i>Preview (not saved yet)</i>", 14, FONT, COLORS.shadow), 0)
		local body = Shared.cleanBody(bodyBox.Text)
		if body then
			renderBlocks(Shared.parseArticle(body, exists), put)
		end
		preview.Visible = true
	end
	local function save()
		if saving then
			return
		end
		-- Check here first for quick feedback. The server checks again.
		local cleanTitle, titleError = Shared.normalizeTitle(titleBox.Text)
		if not cleanTitle then
			messageBox(Config.WikiName, titleError or "Bad title.", "warning")
			return
		end
		local cleanBody, bodyError = Shared.cleanBody(bodyBox.Text)
		if not cleanBody then
			messageBox(Config.WikiName, bodyError or "Bad article.", "warning")
			return
		end
		saving = true
		setEnabled(saveButton, false)
		setStatus("Sending data to " .. HOST .. "...")
		local result = request("save", cleanTitle, cleanBody, mode)
		saving = false
		if not isOpen then
			return
		end
		setEnabled(saveButton, true)
		if result.ok and type(result.entry) == "table" then
			indexAt = -math.huge
			setStatus("Page saved.")
			navigate({ kind = "article", arg = result.entry.title }, "replace")
		else
			setStatus("Save failed.")
			messageBox(Config.WikiName, result.error or "The save failed.", "error")
		end
	end
	saveButton = mkButton("Save page", save)
	local previewButton = mkButton("Show preview", showPreview)
	local cancelButton = mkButton("Cancel", function()
		if historyIndex > 1 then
			historyIndex -= 1
			navigate(history[historyIndex], "back")
		else
			navigate({ kind = "main" })
		end
	end)
	add(mkRow({ saveButton, previewButton, cancelButton }), 30)
	add(preview, 10)
	if not savedToDataStore then
		add(
			mkText(
				"<i>Note: the DataStore isn't available, so saved pages only last until this server shuts down. In Studio, turn on Game Settings > Security > Enable Studio Access to API Services.</i>",
				14,
				FONT,
				Color3.fromRGB(204, 0, 0)
			),
			60
		)
	end
end

---------------------------------------------------------------------------
-- Navigation
---------------------------------------------------------------------------
local function updateNavButtons()
	local b = browser
	if b then
		setEnabled(b.back, historyIndex > 1)
		setEnabled(b.forward, historyIndex < #history)
		b.newButton.Visible = isEditor
	end
end

local function loadPage(page: Page, token: number)
	clearPage()
	loading = true
	setProgress(0)

	if not connected then
		setStatus("Looking up host: " .. HOST .. "...")
		if not waitNav(0.8, token) then
			return
		end
		errorPage(
			"Unable to locate the server",
			`{Config.BrowserName} can't find the server <b>{HOST}</b>. You are not connected to the Internet.`
		)
		add(
			mkButton("Dial up now", function()
				afterConnect = function()
					navigate(page, "back")
				end
				openDialUp()
				task.spawn(dial)
			end),
			40
		)
		reveal(token)
		setStatus("Not connected.")
		return
	end

	setStatus("Looking up host: " .. HOST .. "...")
	if not waitNav(0.25, token) then
		return
	end
	setStatus("Contacting host: " .. HOST .. "...")
	if page.kind ~= "article" then
		refreshIndex(page.kind == "main" or page.kind == "all")
	end
	if token ~= navToken then
		return
	end
	setStatus("Host contacted. Waiting for reply...")

	if page.kind == "article" then
		renderArticle(token, page.arg or "")
	elseif page.kind == "all" then
		renderAll()
	elseif page.kind == "search" then
		renderSearch(token, page.arg or "")
	elseif page.kind == "help" then
		renderHelp()
	elseif page.kind == "edit" then
		renderEditor(token, "edit", page.arg)
	elseif page.kind == "new" then
		renderEditor(token, "create", page.arg)
	else
		renderMain()
	end
	if token ~= navToken then
		return
	end
	reveal(token)
end

-- how: nil = a new page (added to history), "back" = moving through
-- history, "replace" = swap the current history entry (after saving).
navigate = function(page: Page, how: string?)
	local b = browser
	if not b then
		return
	end
	if page.kind == "random" then
		-- Ask the server for a title, then go there instead.
		task.spawn(function()
			local result = request("random")
			if result.ok and type(result.title) == "string" then
				navigate({ kind = "article", arg = result.title }, how)
			else
				navigate({ kind = "main" }, how)
			end
		end)
		return
	end

	navToken += 1
	local token = navToken
	if how == "replace" and historyIndex > 0 then
		history[historyIndex] = page
	elseif how ~= "back" then
		while #history > historyIndex do
			table.remove(history)
		end
		table.insert(history, page)
		historyIndex = #history
	end
	b.address.Text = urlFor(page)
	updateNavButtons()
	task.spawn(loadPage, page, token)
end

local function buildBrowser(win: Window)
	local c = win.content

	-- Menu bar (just for looks).
	local menu = textLabel(c, "File   Edit   View   Go   Bookmarks   Options   Help", UDim2.fromOffset(4, 0), UDim2.new(1, -8, 0, 18), {
		TextSize = 13,
	})
	menu.Name = "Menu"

	-- Toolbar.
	local toolbar: Frame = new("Frame", {
		Position = UDim2.fromOffset(0, 20),
		Size = UDim2.new(1, 0, 0, 40),
		BackgroundColor3 = COLORS.face,
		BorderSizePixel = 0,
	}, c)
	bevel(toolbar, "raised")
	local x = 4
	local function tool(text: string, onClick: () -> ()): TextButton
		local b = button(toolbar, text, UDim2.fromOffset(62, 32), UDim2.fromOffset(x, 4), onClick)
		x += 64
		return b
	end
	local back = tool("Back", function()
		if historyIndex > 1 then
			historyIndex -= 1
			navigate(history[historyIndex], "back")
		end
	end)
	local forward = tool("Forward", function()
		if historyIndex < #history then
			historyIndex += 1
			navigate(history[historyIndex], "back")
		end
	end)
	tool("Home", function()
		navigate({ kind = "main" })
	end)
	tool("Reload", function()
		if historyIndex > 0 then
			indexAt = -math.huge
			navigate(history[historyIndex], "back")
		end
	end)
	tool("All Pages", function()
		navigate({ kind = "all" })
	end)
	tool("Random", function()
		navigate({ kind = "random" })
	end)
	tool("Help", function()
		navigate({ kind = "help" })
	end)
	tool("Stop", function()
		navToken += 1
		loading = false
		setStatus("Transfer interrupted!")
		setProgress(0)
		for _, item in pageItems do
			item.inst.Visible = true
		end
	end)
	local newButton = tool("New", function()
		navigate({ kind = "new" })
	end)
	newButton.Font = BOLD

	-- The throbber: a dot circles the logo while a page is loading.
	local throbber: Frame = new("Frame", {
		Position = UDim2.new(1, -40, 0, 3),
		Size = UDim2.fromOffset(34, 34),
		BackgroundColor3 = COLORS.dark,
		BorderSizePixel = 0,
	}, toolbar)
	textLabel(throbber, "B", UDim2.fromScale(0, 0), UDim2.fromScale(1, 1), {
		Font = SERIF,
		TextSize = 24,
		TextColor3 = COLORS.white,
		TextXAlignment = Enum.TextXAlignment.Center,
	})
	local dot: Frame = new("Frame", {
		AnchorPoint = Vector2.new(0.5, 0.5),
		Position = UDim2.fromScale(0.5, 0.15),
		Size = UDim2.fromOffset(4, 4),
		BackgroundColor3 = Color3.fromRGB(255, 255, 120),
		BorderSizePixel = 0,
	}, throbber)

	-- Location bar.
	textLabel(c, "Location:", UDim2.fromOffset(4, 64), UDim2.fromOffset(62, 22), { Font = BOLD })
	local address = field(c, { Position = UDim2.fromOffset(70, 64), Size = UDim2.new(1, -120, 0, 22) })
	local function goToAddress()
		navigate(pageFromAddress(address.Text))
	end
	address.FocusLost:Connect(function(enterPressed: boolean)
		if enterPressed then
			goToAddress()
		end
	end)
	button(c, "Go", UDim2.fromOffset(42, 22), UDim2.new(1, -44, 0, 64), goToAddress)

	-- The page: sunken, with the classic grey background.
	local pageFrame: Frame = new("Frame", {
		Position = UDim2.fromOffset(0, 90),
		Size = UDim2.new(1, 0, 1, -114),
		BackgroundColor3 = COLORS.face,
		BorderSizePixel = 0,
	}, c)
	bevel(pageFrame, "sunken")
	local scroll: ScrollingFrame = new("ScrollingFrame", {
		Position = UDim2.fromOffset(2, 2),
		Size = UDim2.new(1, -4, 1, -4),
		BackgroundTransparency = 1,
		BorderSizePixel = 0,
		CanvasSize = UDim2.new(),
		AutomaticCanvasSize = Enum.AutomaticSize.Y,
		ScrollBarThickness = 14,
		ScrollBarImageColor3 = COLORS.shadow,
		ScrollingDirection = Enum.ScrollingDirection.Y,
	}, pageFrame)
	new("UIListLayout", { Padding = UDim.new(0, 6), SortOrder = Enum.SortOrder.LayoutOrder }, scroll)
	new("UIPadding", {
		PaddingLeft = UDim.new(0, 14),
		PaddingRight = UDim.new(0, 24),
		PaddingTop = UDim.new(0, 10),
		PaddingBottom = UDim.new(0, 16),
	}, scroll)

	-- Status bar.
	local statusField: Frame = new("Frame", {
		Position = UDim2.new(0, 0, 1, -22),
		Size = UDim2.new(1, -130, 0, 20),
		BackgroundColor3 = COLORS.face,
		BorderSizePixel = 0,
	}, c)
	bevel(statusField, "sunken")
	local status = textLabel(statusField, "", UDim2.fromOffset(6, 0), UDim2.new(1, -10, 1, 0), {
		TextSize = 12,
		TextTruncate = Enum.TextTruncate.AtEnd,
	})
	local progressField: Frame = new("Frame", {
		Position = UDim2.new(1, -126, 1, -22),
		Size = UDim2.fromOffset(126, 20),
		BackgroundColor3 = COLORS.face,
		BorderSizePixel = 0,
	}, c)
	bevel(progressField, "sunken")
	local progressHolder: Frame = new("Frame", {
		Position = UDim2.fromOffset(3, 3),
		Size = UDim2.new(1, -6, 1, -6),
		BackgroundTransparency = 1,
		ClipsDescendants = true,
	}, progressField)
	local progress: Frame = new("Frame", {
		Size = UDim2.fromScale(0, 1),
		BackgroundColor3 = COLORS.title,
		BorderSizePixel = 0,
	}, progressHolder)

	browser = {
		window = win,
		scroll = scroll,
		address = address,
		status = status,
		progress = progress,
		throbberDot = dot,
		back = back,
		forward = forward,
		newButton = newButton,
	}
end

local function openBrowser()
	local win, isNew = openWindow("browser", Config.WikiName .. " - " .. Config.BrowserName, Vector2.new(780, 548), Vector2.new(10, 8), true)
	if isNew then
		buildBrowser(win)
		history = {}
		historyIndex = 0
		win.onClose = function()
			browser = nil
			navToken += 1
			loading = false
		end
		navigate({ kind = "main" })
	end
	focusWindow(win)
end

---------------------------------------------------------------------------
-- Notepad with the read-me
---------------------------------------------------------------------------
local function openReadMe()
	local win, isNew = openWindow("readme", "README.TXT - Notepad", Vector2.new(470, 380), Vector2.new(150, 60), true)
	if not isNew then
		return
	end
	textLabel(win.content, "File   Edit   Search   Help", UDim2.fromOffset(4, 0), UDim2.new(1, -8, 0, 18))
	local text = table.concat({
		`WELCOME TO {string.upper(Config.OSName)}`,
		"",
		`To read {Config.WikiName}:`,
		"  1. Double-click \"Dial-Up Networking\" and press Connect",
		`     (or just open {Config.WikiName}, it offers to dial).`,
		`  2. Double-click \"{Config.WikiName}\".`,
		"",
		"Everyone can read the articles. Only the site editor can",
		"write or change them:",
		"  " .. table.concat(Config.EditorUsernames, ", "),
		"",
		"Type a page name in the Location box and press Enter to",
		"jump to it, or search with the box at the top of a page.",
		"",
		"To leave: Start > Shut Down, or press POWER on the monitor.",
	}, "\n")
	local box = field(win.content, {
		Position = UDim2.fromOffset(0, 20),
		Size = UDim2.new(1, 0, 1, -20),
		Text = text,
		Font = MONO,
		TextSize = 14,
		MultiLine = true,
		TextEditable = false,
		TextWrapped = true,
		TextYAlignment = Enum.TextYAlignment.Top,
	})
	box.Name = "Text"
end

---------------------------------------------------------------------------
-- Opening and closing the PC
---------------------------------------------------------------------------
local frozen: { humanoid: Humanoid, walk: number, jumpPower: number, jumpHeight: number }? = nil

-- Stops your character walking off while you're at the keyboard.
local function freeze()
	local character = player.Character
	local humanoid = character and character:FindFirstChildOfClass("Humanoid")
	if humanoid then
		frozen = { humanoid = humanoid, walk = humanoid.WalkSpeed, jumpPower = humanoid.JumpPower, jumpHeight = humanoid.JumpHeight }
		humanoid.WalkSpeed = 0
		humanoid.JumpPower = 0
		humanoid.JumpHeight = 0
	end
end

local function unfreeze()
	local f = frozen
	frozen = nil
	if f and f.humanoid.Parent then
		f.humanoid.WalkSpeed = f.walk
		f.humanoid.JumpPower = f.jumpPower
		f.humanoid.JumpHeight = f.jumpHeight
	end
end

local function closePC()
	if not isOpen then
		return
	end
	isOpen = false
	session += 1
	navToken += 1
	hangUp() -- turning the PC off drops the line
	local ids = {}
	for id in windows do
		table.insert(ids, id)
	end
	for _, id in ids do
		closeWindow(id)
	end
	setStartMenu(false)
	drag = nil
	offLayer.Visible = false
	gui.Enabled = false
	currentModel = nil
	unfreeze()
	task.spawn(request, "closed")
end

local function shutDown()
	local s = session
	setStartMenu(false)
	offLayer.Visible = true
	if sleep(1.8, s) then
		closePC()
	end
end

powerButton.Activated:Connect(closePC)

local function openPC(model: Model)
	if isOpen then
		return
	end
	isOpen = true
	session += 1
	local s = session
	currentModel = model
	gui.Enabled = true
	desktop.Visible = false
	offLayer.Visible = false
	updateScale()
	freeze()

	task.spawn(function()
		if not runBoot(s) then
			return
		end
		desktop.Visible = true
		local hello = request("hello")
		if not alive(s) then
			return
		end
		isEditor = hello.ok == true and hello.isEditor == true
		savedToDataStore = hello.saved ~= false
		updateNavButtons()
		if isEditor and not savedToDataStore then
			messageBox(
				Config.OSName,
				"The DataStore isn't available, so new pages only last until the server stops. In Studio: Game Settings > Security > Enable Studio Access to API Services.",
				"warning"
			)
		end
	end)

	-- Close if the player gets moved away (pushed, teleported...).
	task.spawn(function()
		while alive(s) do
			task.wait(0.5)
			local character = player.Character
			local root = character and character:FindFirstChild("HumanoidRootPart")
			local pc = currentModel
			if alive(s) and root and root:IsA("BasePart") and pc then
				if (pc:GetPivot().Position - root.Position).Magnitude > Config.MaxUseDistance then
					closePC()
				end
			end
		end
	end)
end

openEvent.OnClientEvent:Connect(function(model: any)
	if typeof(model) == "Instance" and model:IsA("Model") then
		openPC(model)
	end
end)

player.CharacterRemoving:Connect(closePC)

local camera = Workspace.CurrentCamera
if camera then
	camera:GetPropertyChangedSignal("ViewportSize"):Connect(updateScale)
end
Workspace:GetPropertyChangedSignal("CurrentCamera"):Connect(function()
	local newCamera = Workspace.CurrentCamera
	if newCamera then
		newCamera:GetPropertyChangedSignal("ViewportSize"):Connect(updateScale)
		updateScale()
	end
end)

---------------------------------------------------------------------------
-- Desktop contents
---------------------------------------------------------------------------
desktopIcon(Config.WikiName, drawGlobe, openBrowser)
desktopIcon("Dial-Up Networking", drawComputers, openDialUp)
desktopIcon("Read Me", drawNotepad, openReadMe)

startItem(Config.WikiName, openBrowser)
startItem("Dial-Up Networking", openDialUp)
startItem("Read Me", openReadMe)
startItem("Disconnect", hangUp)
startItem("Shut Down...", function()
	task.spawn(shutDown)
end)

---------------------------------------------------------------------------
-- Clock, tray lights, connection timer and throbber
---------------------------------------------------------------------------
task.spawn(function()
	while true do
		if isOpen then
			clockLabel.Text = DateTime.now():FormatLocalTime("h:mm A", "en-us")
			if connected then
				for _, light in trayLights do
					light.BackgroundColor3 = if math.random() < 0.5 then Color3.fromRGB(0, 255, 0) else Color3.fromRGB(0, 128, 128)
				end
				local duration = dialDuration
				if duration then
					local seconds = math.floor(os.clock() - connectedAt)
					duration.Text = string.format("Duration: %03d:%02d:%02d", seconds // 3600, (seconds // 60) % 60, seconds % 60)
				end
			end
			local b = browser
			if b then
				if loading then
					local angle = os.clock() * 8
					b.throbberDot.Position = UDim2.new(0.5, math.cos(angle) * 12, 0.5, math.sin(angle) * 12)
				else
					b.throbberDot.Position = UDim2.fromScale(0.5, 0.15)
				end
			end
		end
		task.wait(0.05)
	end
end)
