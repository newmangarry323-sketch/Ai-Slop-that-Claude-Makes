--!strict
--[[
	Example 4: a mini-game built on the welder ("How much can your shelf hold?")

	Put this Script in ServerScriptService (with the welder installed).

	A steel shelf rests against a steel wall on a wooden prop. Weld the
	shelf to the wall, then hold E on "Start test": the prop is pulled away
	and crates, each heavier than the last, are dropped onto the shelf. The
	sign counts how many it held before the weld snapped.

	It works because of how the welder judges loads: a part hanging off a
	weld (nothing underneath it) loads that weld with its weight, and its
	weight times its distance from the weld (leverage). Loose parts resting
	on top, like these crates, add to that load. So:
	  * a quick tack snaps as soon as the prop goes
	  * a short weld holds the empty shelf but drops it after a few crates
	  * a long, clean weld along the whole back edge holds every crate
]]

local ReplicatedStorage = game:GetService("ReplicatedStorage")

local WelderShared = require(ReplicatedStorage:WaitForChild("WelderShared"))
local Config = WelderShared.Config

local ORIGIN = Config.DemoPosition + Vector3.new(0, 0, -18)
local CRATES = 6

local wood = Color3.fromRGB(124, 92, 70)
local steel = Color3.fromRGB(140, 142, 146)

local function block(parent: Instance, name: string, size: Vector3, offset: Vector3, material: Enum.Material, color: Color3, anchored: boolean): Part
	local part = Instance.new("Part")
	part.Name = name
	part.Size = size
	part.CFrame = CFrame.new(ORIGIN + offset)
	part.Material = material
	part.Color = color
	part.Anchored = anchored
	part.TopSurface = Enum.SurfaceType.Smooth
	part.BottomSurface = Enum.SurfaceType.Smooth
	part.Parent = parent
	return part
end

-- Same lookup the welder uses: is there a welder joint between a and b?
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

local function build()
	local model = Instance.new("Model")
	model.Name = "ShelfChallenge"

	-- The wall faces the practice bench (+Z).
	local wall = block(model, "Steel Wall", Vector3.new(6, 6, 0.6), Vector3.new(0, 3, 0), Enum.Material.Metal, steel, true)
	local shelf = block(model, "Steel Shelf", Vector3.new(3, 0.3, 2.5), Vector3.new(0, 3.5, 0.3 + 1.25), Enum.Material.Metal, steel, false)
	-- The prop holds the shelf up while you weld. It's wood, so it can't
	-- be welded by accident.
	local prop = block(model, "Wooden Prop", Vector3.new(0.6, 3.35, 0.6), Vector3.new(0, 3.35 / 2, 2.4), Enum.Material.Wood, wood, true)

	-- A sign everyone can read (SurfaceGui is drawn on the part itself).
	local sign = block(model, "Sign", Vector3.new(3, 1.6, 0.2), Vector3.new(4.5, 4.5, 0.4), Enum.Material.SmoothPlastic, Color3.fromRGB(25, 25, 30), true)
	local surface = Instance.new("SurfaceGui")
	surface.Face = Enum.NormalId.Front -- local -Z
	surface.PixelsPerStud = 60
	surface.Parent = sign
	local text = Instance.new("TextLabel")
	text.Size = UDim2.fromScale(1, 1)
	text.BackgroundTransparency = 1
	text.TextColor3 = Color3.new(1, 1, 1)
	text.Font = Enum.Font.GothamBold
	text.TextScaled = true
	text.Text = "Weld the shelf to the wall,\nthen hold E on Start test"
	text.Parent = surface
	-- Turn the sign so its Front face (-Z) looks at the bench (+Z).
	sign.CFrame = sign.CFrame * CFrame.Angles(0, math.pi, 0)

	local button = block(model, "Start Test", Vector3.new(0.8, 1, 0.8), Vector3.new(4.5, 0.5, 2), Enum.Material.Neon, Color3.fromRGB(60, 200, 90), true)
	local prompt = Instance.new("ProximityPrompt")
	prompt.ActionText = "Start test"
	prompt.ObjectText = "Shelf challenge"
	prompt.HoldDuration = 0.5
	prompt.RequiresLineOfSight = false
	prompt.Parent = button

	model.Parent = workspace

	local running = false
	prompt.Triggered:Connect(function()
		if running then
			return
		end
		running = true
		prompt.Enabled = false

		local function still(): boolean
			return findJoint(wall, shelf) ~= nil
		end

		if not still() then
			text.Text = "Not welded yet!\nWeld the back edge first"
			running = false
			prompt.Enabled = true
			return
		end

		prop:Destroy()
		text.Text = "Prop removed..."
		task.wait(1.5) -- the welder checks loads 4 times a second

		local held = 0
		if still() then
			-- Spots on the shelf, back to front: front spots have more leverage.
			local spots = { Vector2.new(-0.9, 0.6), Vector2.new(0.9, 0.6), Vector2.new(0, 1.2), Vector2.new(-0.9, 1.9), Vector2.new(0.9, 1.9), Vector2.new(0, 2.1) }
			for i = 1, CRATES do
				local spot = spots[(i - 1) % #spots + 1]
				local crate = block(model, "Crate " .. i, Vector3.new(0.8, 0.8, 0.8), Vector3.new(spot.X, 4.6, 0.3 + spot.Y), Enum.Material.WoodPlanks, wood, false)
				-- Heavier each time: density 10, 20, 30... (Roblox's density
				-- units; water is 1). Mass = density x volume.
				crate.CustomPhysicalProperties = PhysicalProperties.new(10 * i, 0.5, 0.05)
				text.Text = string.format("Crate %d (mass %d)", i, math.floor(crate:GetMass() + 0.5))
				task.wait(2.5)
				if not still() then
					break
				end
				held = i
			end
		end

		if held == CRATES then
			text.Text = string.format("Perfect weld!\nHeld all %d crates", CRATES)
		elseif held == 0 and not still() then
			text.Text = "Snapped straight away.\nA tack weld isn't enough"
		else
			text.Text = string.format("The weld held %d crate%s.\nLonger, steadier weld = stronger", held, if held == 1 then "" else "s")
		end

		-- Reset after a while so the next player can try.
		task.wait(8)
		model:Destroy()
		build()
	end)
end

build()

--[[
	Try this:
	  * Keep a high score (a NumberValue, or a DataStore so it survives).
	  * Add a "Prop back" button so a player can repair and retry.
	  * Make the crates fall from higher up. The welder only checks impacts
	    for parts that aren't attached to anything anchored, so you'd need
	    to add an impact check for this case. Look at checkImpact in
	    WelderServer.
]]
