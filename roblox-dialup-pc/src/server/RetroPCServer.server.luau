--!strict
--[[
	RetroPCServer (Script, goes in ServerScriptService)

	The server side of the retro PC. It:
	  * builds a beige 90s PC on a desk (unless Config.SpawnDemoPC is false)
	  * adds a "Use computer" prompt to every model tagged "RetroPC"
	  * keeps the wiki's entries in a DataStore (or in memory if Studio has no
	    access to API services)
	  * answers the client's requests: list pages, read a page, search, and
	    save a page. Saving is checked here, on the server, against
	    Config.EditorUsernames / Config.EditorUserIds. The client hiding the
	    "New article" button for other players is only cosmetic.

	Everything you see on the PC's screen (boot screen, desktop, dial-up,
	browser) is drawn by RetroPCClient on the player's own computer.
]]

local CollectionService = game:GetService("CollectionService")
local DataStoreService = game:GetService("DataStoreService")
local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local RunService = game:GetService("RunService")
local TextService = game:GetService("TextService")

local Shared = require(ReplicatedStorage:WaitForChild("RetroPCShared"))
local Config = Shared.Config

-- Where the demo PC's desk stands: 14 studs from the origin, turned round
-- so its screen faces the default spawn point.
local DEMO_PC_CFRAME = CFrame.new(0, 0, -14) * CFrame.Angles(0, math.pi, 0)

---------------------------------------------------------------------------
-- Remotes
---------------------------------------------------------------------------
local remote = Instance.new("RemoteFunction")
remote.Name = Config.RemoteFunctionName
remote.Parent = ReplicatedStorage

local openEvent = Instance.new("RemoteEvent")
openEvent.Name = Config.OpenEventName
openEvent.Parent = ReplicatedStorage

---------------------------------------------------------------------------
-- Storage
---------------------------------------------------------------------------
--[[
	Two kinds of key in one DataStore:
	  "index"          { [titleKey] = { title, updated, author } }
	                   the list of every page, so the server doesn't have to
	                   load each page to show "All pages" or search titles
	  "e_<title>"      { title, body, author, authorId, created, updated,
	                     revision }   one page
	The body is stored already filtered by Roblox's text filter.
]]
type IndexItem = { title: string, updated: number, author: string }
type Index = { [string]: IndexItem }
type Entry = {
	title: string,
	body: string,
	author: string,
	authorId: number,
	created: number,
	updated: number,
	revision: number,
}

local store: DataStore? = nil
local memory: { [string]: any } = {} -- used when the DataStore isn't available

do
	local ds: DataStore? = nil
	local problem: any = nil
	local ok, result = pcall(function()
		return DataStoreService:GetDataStore(Config.DataStoreName)
	end)
	if ok then
		ds = result
	else
		problem = result
	end
	-- In Studio, try one read: it fails when the place isn't published or
	-- Studio has no API access. Live servers skip this test, so a short
	-- DataStore outage only fails the requests made during it ("The server
	-- is busy") instead of keeping the server in memory mode for good.
	if ds and RunService:IsStudio() then
		local probeOk, probeError = pcall(function()
			(ds :: DataStore):GetAsync("index")
		end)
		if not probeOk then
			ds, problem = nil, probeError
		end
	end
	store = ds
	if not ds then
		warn(
			"[RetroPC] DataStore unavailable, so entries are kept in memory and lost when the server stops. "
				.. "In Studio: File > Experience Settings > Security > Enable Studio Access to API Services. ("
				.. tostring(problem)
				.. ")"
		)
	end
end

local function readKey(key: string): (boolean, any)
	local ds = store
	if not ds then
		return true, memory[key]
	end
	return pcall(function()
		return ds:GetAsync(key)
	end)
end

-- `transform` gets the current value and returns the new one, or nil to
-- leave it alone. It must not yield (UpdateAsync can call it more than once).
local function updateKey(key: string, transform: (any) -> any): (boolean, any)
	local ds = store
	if not ds then
		local new = transform(memory[key])
		if new ~= nil then
			memory[key] = new
		end
		return true, new
	end
	return pcall(function()
		return ds:UpdateAsync(key, transform)
	end)
end

-- The server keeps a copy of the index and of pages it has read, so most
-- requests never touch the DataStore. Other servers' saves show up after
-- Config.CacheSeconds.
local indexCache: Index = {}
local indexFetchedAt = -math.huge
-- Goes up whenever a save changes indexCache, so a read of the index that
-- started before the save can't overwrite the newer copy when it returns.
local indexGeneration = 0
-- False until the index has been read (or written) once. Until then an
-- empty indexCache means "don't know yet", not "no pages".
local indexLoaded = false
local entryCache: { [string]: { entry: Entry?, at: number } } = {}

local function getIndex(): Index
	if os.clock() - indexFetchedAt > Config.CacheSeconds then
		local generation = indexGeneration
		local ok, value = readKey("index")
		if not ok then
			warn("[RetroPC] Couldn't read the index:", value)
			-- Try again in 5 seconds rather than on every request.
			indexFetchedAt = os.clock() - Config.CacheSeconds + 5
		elseif generation == indexGeneration then
			indexCache = if type(value) == "table" then value else {}
			indexFetchedAt = os.clock()
			indexLoaded = true
		end
	end
	return indexCache
end

local function getEntry(key: string): (Entry?, string?)
	local cached = entryCache[key]
	if cached and os.clock() - cached.at < Config.CacheSeconds then
		return cached.entry, nil
	end
	local ok, value = readKey(key)
	if not ok then
		warn("[RetroPC] Couldn't read", key, value)
		return nil, "The server is busy. Try again in a moment."
	end
	local entry: Entry? = if type(value) == "table" then value else nil
	entryCache[key] = { entry = entry, at = os.clock() }
	return entry, nil
end

local function allTitles(): { string }
	local titles = {}
	for _, item in getIndex() do
		table.insert(titles, item.title)
	end
	table.sort(titles, function(a: string, b: string): boolean
		return string.lower(a) < string.lower(b)
	end)
	return titles
end

local function recentChanges(limit: number): { IndexItem }
	local items = {}
	for _, item in getIndex() do
		table.insert(items, item)
	end
	table.sort(items, function(a: IndexItem, b: IndexItem): boolean
		return a.updated > b.updated
	end)
	while #items > limit do
		table.remove(items)
	end
	return items
end

---------------------------------------------------------------------------
-- Text filter
---------------------------------------------------------------------------
-- Text that other players will see has to go through Roblox's filter.
-- Returns the filtered text, or nil if the filter couldn't run (then the
-- text must not be shown, so the save is refused).
local function filterForEveryone(text: string, fromUserId: number): string?
	local ok, result = pcall(function()
		return TextService:FilterStringAsync(text, fromUserId):GetNonChatStringForBroadcastAsync()
	end)
	if ok then
		return result
	end
	-- Only when nothing is saved for real: with DataStore access, Studio
	-- writes to the same DataStore your live servers read from.
	if RunService:IsStudio() and Config.AllowUnfilteredInStudio and store == nil then
		warn("[RetroPC] Text filter failed in Studio; saving unfiltered to memory only:", result)
		return text
	end
	warn("[RetroPC] Text filter failed:", result)
	return nil
end

---------------------------------------------------------------------------
-- Building the PC
---------------------------------------------------------------------------
local BEIGE = Color3.fromRGB(222, 214, 190)
local BEIGE_DARK = Color3.fromRGB(196, 188, 164)

local function part(model: Model, name: string, size: Vector3, cframe: CFrame, color: Color3, material: Enum.Material?): Part
	local p = Instance.new("Part")
	p.Name = name
	p.Size = size
	p.CFrame = cframe
	p.Color = color
	p.Material = material or Enum.Material.SmoothPlastic
	p.Anchored = true
	p.TopSurface = Enum.SurfaceType.Smooth
	p.BottomSurface = Enum.SurfaceType.Smooth
	p.Parent = model
	return p
end

local function label(parent: Instance, text: string, color: Color3, font: Enum.Font, size: UDim2, position: UDim2): TextLabel
	local l = Instance.new("TextLabel")
	l.BackgroundTransparency = 1
	l.Text = text
	l.TextColor3 = color
	l.Font = font
	l.TextScaled = true
	l.Size = size
	l.Position = position
	l.Parent = parent
	return l
end

-- The PC's front (screen, keyboard) faces the origin CFrame's -Z direction.
local function buildPC(origin: CFrame): Model
	local model = Instance.new("Model")
	model.Name = "RetroPC"

	-- Desk: the origin is the floor under the middle of the desk.
	local deskTop = 3
	local desk = part(model, "Desk", Vector3.new(7, 0.3, 3.5), origin * CFrame.new(0, deskTop, 0), Color3.fromRGB(120, 84, 52), Enum.Material.Wood)
	for _, xz in { { -3.2, -1.5 }, { 3.2, -1.5 }, { -3.2, 1.5 }, { 3.2, 1.5 } } do
		part(model, "Leg", Vector3.new(0.3, deskTop - 0.15, 0.3), origin * CFrame.new(xz[1], (deskTop - 0.15) / 2, xz[2]), Color3.fromRGB(100, 70, 44), Enum.Material.Wood)
	end
	local top = deskTop + 0.15

	-- CRT monitor: a deep beige box with a bezel and a dark glass screen.
	local monitorCF = origin * CFrame.new(-0.6, top + 1.35, 0.2)
	part(model, "MonitorBack", Vector3.new(2.2, 2.0, 1.6), monitorCF * CFrame.new(0, -0.05, 0.6), BEIGE_DARK)
	part(model, "Monitor", Vector3.new(3.0, 2.6, 0.9), monitorCF, BEIGE)
	part(model, "MonitorStand", Vector3.new(1.4, 0.3, 1.2), origin * CFrame.new(-0.6, top + 0.15, 0.4), BEIGE_DARK)
	local screen = part(model, "Screen", Vector3.new(2.4, 1.9, 0.05), monitorCF * CFrame.new(0, 0.1, -0.46), Color3.fromRGB(16, 24, 20), Enum.Material.Glass)
	part(model, "PowerLight", Vector3.new(0.08, 0.08, 0.05), monitorCF * CFrame.new(1.25, -1.05, -0.46), Color3.fromRGB(60, 255, 90), Enum.Material.Neon)

	-- What the screen shows in the world when nobody is using it.
	local gui = Instance.new("SurfaceGui")
	gui.Face = Enum.NormalId.Front
	gui.CanvasSize = Vector2.new(320, 240)
	gui.LightInfluence = 0
	gui.Parent = screen
	local green = Color3.fromRGB(80, 255, 120)
	label(gui, `{Config.OSName}`, green, Enum.Font.Code, UDim2.fromScale(0.9, 0.16), UDim2.fromScale(0.05, 0.12))
	label(gui, `{Config.WikiName} terminal`, green, Enum.Font.Code, UDim2.fromScale(0.9, 0.12), UDim2.fromScale(0.05, 0.36))
	label(gui, "C:\\> _", green, Enum.Font.Code, UDim2.fromScale(0.9, 0.12), UDim2.fromScale(0.05, 0.62))

	-- Tower case on the right with a floppy slot and a CD drive.
	local towerCF = origin * CFrame.new(2.3, top + 1.2, 0.3)
	part(model, "Tower", Vector3.new(1.0, 2.4, 2.2), towerCF, BEIGE)
	part(model, "FloppySlot", Vector3.new(0.6, 0.06, 0.05), towerCF * CFrame.new(0, 0.55, -1.1), Color3.fromRGB(40, 40, 40))
	part(model, "CDDrive", Vector3.new(0.8, 0.22, 0.05), towerCF * CFrame.new(0, 0.85, -1.1), BEIGE_DARK)
	part(model, "TowerLight", Vector3.new(0.06, 0.06, 0.05), towerCF * CFrame.new(0.3, -0.8, -1.1), Color3.fromRGB(60, 255, 90), Enum.Material.Neon)

	-- External modem with lights that blink while someone is using the PC.
	local modemCF = origin * CFrame.new(2.3, top + 0.12, -1.25)
	part(model, "Modem", Vector3.new(1.1, 0.22, 0.6), modemCF, Color3.fromRGB(60, 60, 64))
	for i = 1, 4 do
		local led = part(model, "ModemLight", Vector3.new(0.08, 0.05, 0.05), modemCF * CFrame.new(-0.4 + i * 0.16, 0.05, -0.3), Color3.fromRGB(70, 20, 20), Enum.Material.Neon)
		led:SetAttribute("LightIndex", i)
	end

	-- Keyboard and mouse.
	part(model, "Keyboard", Vector3.new(2.6, 0.15, 0.9), origin * CFrame.new(-0.6, top + 0.08, -1.1), BEIGE)
	part(model, "Mouse", Vector3.new(0.3, 0.12, 0.45), origin * CFrame.new(1.1, top + 0.06, -1.1), BEIGE)

	model.PrimaryPart = desk
	screen:SetAttribute("IsScreen", true)
	model.Parent = workspace
	CollectionService:AddTag(model, Config.PCTag)
	return model
end

---------------------------------------------------------------------------
-- Prompts and opening the PC
---------------------------------------------------------------------------
-- Which PC each player has open, so the modem lights know when to blink.
local usingPC: { [Player]: Model } = {}

local function updateInUse(model: Model)
	local inUse = false
	for _, m in usingPC do
		if m == model then
			inUse = true
		end
	end
	model:SetAttribute("InUse", inUse)
end

-- The part the prompt hangs off: a part with the IsScreen attribute, or
-- the PrimaryPart, or any part.
local function promptPart(model: Model): BasePart?
	for _, d in model:GetDescendants() do
		if d:IsA("BasePart") and d:GetAttribute("IsScreen") then
			return d
		end
	end
	return model.PrimaryPart or model:FindFirstChildWhichIsA("BasePart", true)
end

-- Prompts this script has connected. A copy of a PC made with Clone() brings
-- its prompt along but not the connection, so a prompt not in here is
-- replaced. Entries are removed when their prompt leaves the game
-- (destroyed, or its PC removed by setting Parent to nil). Not a weak
-- table: Luau can drop an Instance from one while it still exists.
local connectedPrompts: { [Instance]: boolean } = {}

local function setupPC(instance: Instance)
	if not instance:IsA("Model") then
		warn("[RetroPC] Only Models can be tagged", Config.PCTag, "-", instance:GetFullName())
		return
	end
	local model = instance
	local anchor = promptPart(model)
	if not anchor then
		return
	end
	local existing = anchor:FindFirstChild("RetroPCPrompt")
	if existing and connectedPrompts[existing] then
		return
	end
	if existing then
		existing:Destroy()
	end
	local prompt = Instance.new("ProximityPrompt")
	connectedPrompts[prompt] = true
	prompt.Destroying:Connect(function()
		connectedPrompts[prompt] = nil
	end)
	prompt.AncestryChanged:Connect(function()
		-- If the PC comes back later, setupPC gives it a fresh prompt.
		if not prompt:IsDescendantOf(game) then
			connectedPrompts[prompt] = nil
		end
	end)
	prompt.Name = "RetroPCPrompt"
	prompt.ActionText = "Use computer"
	prompt.ObjectText = Config.OSName
	prompt.MaxActivationDistance = Config.PromptDistance
	prompt.RequiresLineOfSight = false
	prompt.Parent = anchor

	prompt.Triggered:Connect(function(player: Player)
		local previous = usingPC[player]
		usingPC[player] = model
		if previous and previous ~= model then
			updateInUse(previous)
		end
		updateInUse(model)
		openEvent:FireClient(player, model)
	end)
end

for _, instance in CollectionService:GetTagged(Config.PCTag) do
	setupPC(instance)
end
CollectionService:GetInstanceAddedSignal(Config.PCTag):Connect(setupPC)

if Config.SpawnDemoPC then
	buildPC(DEMO_PC_CFRAME)
end

local function nearAPC(player: Player): boolean
	local character = player.Character
	local root = character and character:FindFirstChild("HumanoidRootPart")
	if not (root and root:IsA("BasePart")) then
		return false
	end
	for _, pc in CollectionService:GetTagged(Config.PCTag) do
		-- Only PCs in the world count, not templates kept in ServerStorage.
		if
			pc:IsA("Model")
			and pc:IsDescendantOf(workspace)
			and (pc:GetPivot().Position - root.Position).Magnitude <= Config.MaxUseDistance
		then
			return true
		end
	end
	return false
end

-- Blink the modem lights of PCs in use. A real modem's RD/SD lights flash
-- as data goes by, so random flicker looks about right.
task.spawn(function()
	local on = Color3.fromRGB(255, 60, 40)
	local off = Color3.fromRGB(70, 20, 20)
	while true do
		for _, pc in CollectionService:GetTagged(Config.PCTag) do
			local inUse = pc:GetAttribute("InUse") == true
			for _, d in pc:GetDescendants() do
				local index = d:GetAttribute("LightIndex")
				if d:IsA("BasePart") and type(index) == "number" then
					-- Light 1 ("power") stays on; the others flicker.
					local lit = inUse and (index == 1 or math.random() < 0.5)
					d.Color = if lit then on else off
				end
			end
		end
		task.wait(0.15)
	end
end)

---------------------------------------------------------------------------
-- Requests from the client
---------------------------------------------------------------------------
-- Requests counted per one-second window, per player.
local requestWindow: { [Player]: { start: number, count: number } } = {}
local lastSave: { [Player]: number } = {}

local function fail(message: string)
	return { ok = false, error = message }
end

-- Pages that were saved but whose index update failed, by key. They are
-- retried in the background and once more when the server shuts down, so
-- a page doesn't stay missing from the index for good.
local pendingIndex: { [string]: IndexItem } = {}
local retrying: { [string]: boolean } = {}

-- Adds (or updates) one page in the index. Returns false if the DataStore
-- refused; the page then isn't listed on any server yet.
local function addToIndex(key: string, item: IndexItem): boolean
	local ok, newIndex = updateKey("index", function(old: any): any
		local index: Index = if type(old) == "table" then old else {}
		local current = index[key]
		-- A retry mustn't replace a newer save of the same page.
		if current and current.updated > item.updated then
			return nil
		end
		index[key] = item
		return index
	end)
	if not ok then
		warn("[RetroPC] Couldn't update the index:", newIndex)
		return false
	end
	if type(newIndex) == "table" then
		indexGeneration += 1
		indexCache = newIndex
		indexFetchedAt = os.clock()
		indexLoaded = true
	end -- nil means it was already listed with a newer save
	local pending = pendingIndex[key]
	if pending and pending.updated <= item.updated then
		pendingIndex[key] = nil
	end
	return true
end

-- Lists a page. If the DataStore refuses, keeps trying in the background
-- (after 5, 15, 30 and 50 seconds). Returns true if it's listed now.
local function listPage(key: string, item: IndexItem): boolean
	if addToIndex(key, item) then
		return true
	end
	local pending = pendingIndex[key]
	if not pending or pending.updated <= item.updated then
		pendingIndex[key] = item
	end
	if not retrying[key] then
		retrying[key] = true
		task.spawn(function()
			for attempt = 1, 4 do
				task.wait(5 * attempt)
				local current = pendingIndex[key] -- the newest save of this page
				if not current or addToIndex(key, current) then
					break
				end
			end
			retrying[key] = nil
			local left = pendingIndex[key]
			if left then
				warn(
					"[RetroPC] Gave up adding",
					left.title,
					"to the index for now. It's tried again when the server shuts down; saving the page again also lists it."
				)
			end
		end)
	end
	return false
end

-- Roblox waits for this (up to 30 seconds) before a server shuts down: one
-- last try at listing the pages whose index update kept failing.
game:BindToClose(function()
	if store == nil or next(pendingIndex) == nil then
		return
	end
	local items = table.clone(pendingIndex)
	local ok, problem = updateKey("index", function(old: any): any
		local index: Index = if type(old) == "table" then old else {}
		for key, item in items do
			local current = index[key]
			if not (current and current.updated > item.updated) then
				index[key] = item
			end
		end
		return index
	end)
	if not ok then
		warn("[RetroPC] Couldn't list unsaved index entries at shutdown:", problem)
	end
end)

local function saveEntry(player: Player, rawTitle: any, rawBody: any, mode: any)
	-- 1. Only editors. player.Name and player.UserId come from Roblox, so a
	--    modified client can't fake them.
	if not Shared.isEditor(player.Name, player.UserId) then
		return fail("Access denied. Only the site's editor can create or change entries.")
	end
	if type(rawTitle) ~= "string" or type(rawBody) ~= "string" or (mode ~= "create" and mode ~= "edit") then
		return fail("Bad request.")
	end
	if os.clock() - (lastSave[player] or -math.huge) < Config.SaveCooldown then
		return fail("Saving too fast. Wait a few seconds.")
	end
	if not nearAPC(player) then
		return fail("You have to be at a computer to save.")
	end

	-- 2. The entry has to follow the rules (checked again here: never trust
	--    the client's copy of the checks).
	local title, titleError = Shared.normalizeTitle(rawTitle)
	if not title then
		return fail(titleError or "Bad title.")
	end
	local body, bodyError = Shared.cleanBody(rawBody)
	if not body then
		return fail(bodyError or "Bad article.")
	end

	-- 3. Filter what other players will read.
	lastSave[player] = os.clock()
	local filteredTitle = filterForEveryone(title, player.UserId)
	if filteredTitle == nil then
		return fail("The text filter isn't working right now. Try again later.")
	end
	if filteredTitle ~= title then
		return fail("That title was blocked by the Roblox text filter. Pick another.")
	end
	local filteredBody = filterForEveryone(body, player.UserId)
	if filteredBody == nil then
		return fail("The text filter isn't working right now. Try again later.")
	end

	-- 4. Save the page, then add it to the index.
	local key = Shared.titleKey(title)
	local now = os.time()
	local refusal: string? = nil
	local existingEntry: Entry? = nil
	local ok, saved = updateKey(key, function(old: any): any
		local existing: Entry? = if type(old) == "table" then old else nil
		-- UpdateAsync can call this more than once; only the last call counts.
		refusal = nil
		existingEntry = existing
		if mode == "create" and existing then
			refusal = `"{existing.title}" already exists. Open it and choose Edit instead.`
			return nil
		end
		if mode == "edit" and not existing then
			refusal = "That page doesn't exist any more."
			return nil
		end
		local entry: Entry = {
			-- Editing keeps the original title's spelling.
			title = if existing then existing.title else title,
			body = filteredBody,
			author = player.Name,
			authorId = player.UserId,
			created = if existing then existing.created else now,
			updated = now,
			revision = if existing then existing.revision + 1 else 1,
		}
		return entry
	end)
	if refusal then
		-- "Already exists" for a page missing from the index (an earlier save
		-- whose index update failed): list it now, so it can be found.
		local found = existingEntry
		if found and getIndex()[key] == nil then
			listPage(key, { title = found.title, updated = found.updated, author = found.author })
		end
		return fail(refusal)
	end
	if not ok or type(saved) ~= "table" then
		warn("[RetroPC] Save failed:", saved)
		return fail("The save failed (the server may be busy). Try again in a minute.")
	end
	local entry: Entry = saved
	entryCache[key] = { entry = entry, at = os.clock() }

	-- listed = false tells the editor the page is saved but readers can't
	-- find it yet (the server keeps trying to list it).
	local item: IndexItem = { title = entry.title, updated = now, author = player.Name }
	local listed = listPage(key, item)

	return { ok = true, entry = entry, listed = listed }
end

local handlers: { [string]: (Player, ...any) -> any } = {}

handlers.hello = function(player: Player)
	return {
		ok = true,
		isEditor = Shared.isEditor(player.Name, player.UserId),
		saved = store ~= nil,
	}
end

-- Until a server has read the index once, it can't tell "no pages" from
-- "couldn't read the list", so list requests answer "busy" instead.
local BUSY = "The server is busy. Try again in a moment."

handlers.index = function(_player: Player)
	getIndex()
	if not indexLoaded then
		return fail(BUSY)
	end
	return { ok = true, titles = allTitles(), recent = recentChanges(8) }
end

handlers.get = function(player: Player, rawTitle: any)
	if type(rawTitle) ~= "string" then
		return fail("Bad request.")
	end
	local title = Shared.normalizeTitle(rawTitle)
	if not title then
		return { ok = true, entry = nil }
	end
	local key = Shared.titleKey(title)
	-- Only pages in the index are read from the DataStore. Otherwise anyone
	-- could ask for thousands of made-up titles, use up the server's
	-- DataStore budget and make the editor's saves fail. Editors skip this,
	-- so they can still open (and re-save) a page whose index update failed.
	local index = getIndex()
	if not indexLoaded then
		return fail(BUSY)
	end
	if index[key] == nil and not Shared.isEditor(player.Name, player.UserId) then
		return { ok = true, entry = nil }
	end
	local entry, err = getEntry(key)
	if err then
		return fail(err)
	end
	return { ok = true, entry = entry }
end

handlers.search = function(_player: Player, query: any)
	if type(query) ~= "string" or #query > 100 then
		return fail("Bad request.")
	end
	getIndex()
	if not indexLoaded then
		return fail(BUSY)
	end
	return { ok = true, results = Shared.searchTitles(allTitles(), query, 50) }
end

handlers.random = function(_player: Player)
	getIndex()
	if not indexLoaded then
		return fail(BUSY)
	end
	local titles = allTitles()
	return { ok = true, title = if #titles > 0 then titles[math.random(#titles)] else nil }
end

handlers.save = saveEntry

handlers.closed = function(player: Player)
	local model = usingPC[player]
	usingPC[player] = nil
	if model then
		updateInUse(model)
	end
	return { ok = true }
end

remote.OnServerInvoke = function(player: Player, action: any, ...: any)
	local handler = if type(action) == "string" then handlers[action] else nil
	if not handler then
		return fail("Unknown request.")
	end
	local window = requestWindow[player]
	if not window or os.clock() - window.start >= 1 then
		window = { start = os.clock(), count = 0 }
	end
	assert(window)
	window.count += 1
	requestWindow[player] = window
	if window.count > Config.MaxRequestsPerSecond then
		return fail("Too many requests. Slow down.")
	end
	return handler(player, ...)
end

Players.PlayerRemoving:Connect(function(player: Player)
	local model = usingPC[player]
	usingPC[player] = nil
	if model then
		updateInUse(model)
	end
	requestWindow[player] = nil
	lastSave[player] = nil
end)
