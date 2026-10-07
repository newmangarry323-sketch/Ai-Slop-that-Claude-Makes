--!strict
--[[
	RetroPCShared (ModuleScript, goes in ReplicatedStorage)

	Settings and helper functions used by both RetroPCServer and RetroPCClient.
	Nothing in here creates or changes instances, so it is safe to require
	from either side (and from the plain Luau test runner in tests/).

	What lives here:
	  * Config: every setting you are likely to want to change
	  * isEditor: who is allowed to create and edit entries
	  * normalizeTitle / titleKey / cleanBody: the rules an entry must follow
	  * parseArticle: turns wiki markup into rich text blocks for the screen
]]

local RetroPCShared = {}

---------------------------------------------------------------------------
-- Settings. Change these to tune the PC.
---------------------------------------------------------------------------
local Config = {
	-- Only these players can create or edit entries. Everyone else can read.
	-- Names are compared without caring about upper/lower case, the same way
	-- Roblox treats usernames.
	EditorUsernames = { "L3g3ndDrag0n2007" },
	-- Optional: user IDs that are also editors. A user ID never changes, a
	-- username can, so adding your ID here keeps you as editor after a rename.
	-- Find it in the address of your Roblox profile page.
	EditorUserIds = {} :: { number },

	-- Names shown on screen. All made up on purpose.
	WikiName = "Blockopedia",
	WikiSlogan = "the free encyclopedia",
	OSName = "RetroOS 95",
	BrowserName = "WebWalker",
	SiteHost = "blockopedia.local",

	-- Dial-up. 28,800 bit/s is the top speed of the V.34 modem standard
	-- (1994). Try 14400 for an older modem or 56000 for a late-90s one.
	ModemBitsPerSecond = 28800,
	-- 555-0100 to 555-0199 are set aside for fiction in North America.
	PhoneNumber = "555-0142",
	DialSeconds = 7, -- how long the "Dialing... Verifying..." sequence takes
	-- Optional dial-up sound: upload your own audio and put its ID here,
	-- e.g. "rbxassetid://1234567890". Empty means no sound.
	DialUpSoundId = "",

	-- Pages "download" at this rate (bytes per second) so they appear bit by
	-- bit. 28,800 bits a second is 3,600 bytes a second before overheads.
	PageBytesPerSecond = 3000,
	MinPageLoadSeconds = 0.6,
	MaxPageLoadSeconds = 4,

	-- Entry rules.
	TitleMaxLength = 40, -- the data store key must stay under 50 characters
	BodyMaxLength = 6000,

	-- Spawn a ready-made PC on a desk (its position is set at the top of
	-- RetroPCServer). Set to false to only use your own models tagged
	-- "RetroPC" (see README).
	SpawnDemoPC = true,

	-- How close you must be to use a PC (studs).
	PromptDistance = 8,
	MaxUseDistance = 16, -- the server checks this before saving

	-- Saving.
	DataStoreName = "RetroPCWiki_v1",
	SaveCooldown = 3, -- seconds between saves per player
	MaxRequestsPerSecond = 10, -- per player, for everything else
	CacheSeconds = 30, -- how long the server trusts its copy of the index
	-- Roblox's text filter often doesn't run in Studio. When this is true,
	-- Studio saves unfiltered text if the filter fails. Live servers never do.
	AllowUnfilteredInStudio = true,

	-- Names of the instances the scripts create.
	PCTag = "RetroPC",
	RemoteFunctionName = "RetroPCRemote",
	OpenEventName = "RetroPCOpen",
}
RetroPCShared.Config = Config

-- Page names the browser uses itself. Entries can't take these titles.
local RESERVED = {
	["main page"] = true,
	["all pages"] = true,
	["search"] = true,
	["random"] = true,
	["help"] = true,
	["new article"] = true,
}

---------------------------------------------------------------------------
-- Who can edit
---------------------------------------------------------------------------
-- Always call this on the server with the real Player's Name and UserId.
-- The client also calls it, but only to decide which buttons to show:
-- hiding a button doesn't stop anyone, the server check does.
function RetroPCShared.isEditor(name: string, userId: number): boolean
	for _, id in Config.EditorUserIds do
		if id == userId then
			return true
		end
	end
	local lowered = string.lower(name)
	for _, editor in Config.EditorUsernames do
		if string.lower(editor) == lowered then
			return true
		end
	end
	return false
end

---------------------------------------------------------------------------
-- Text helpers
---------------------------------------------------------------------------
function RetroPCShared.trim(s: string): string
	return (string.gsub(s, "^%s+", ""):gsub("%s+$", ""))
end

-- Title rules: letters, digits, spaces and - ( ) . , ' !
-- It must start with a letter or digit. Spaces are tidied up.
-- Returns the clean title, or nil and the reason it was refused.
function RetroPCShared.normalizeTitle(raw: string): (string?, string?)
	local title = RetroPCShared.trim((string.gsub(raw, "%s+", " ")))
	if title == "" then
		return nil, "The title is empty."
	end
	if #title > Config.TitleMaxLength then
		return nil, `The title is too long (max {Config.TitleMaxLength} characters).`
	end
	if not string.match(title, "^[%w][%w %-%(%)%.,'!]*$") then
		return nil, "Titles can only use letters, digits, spaces and - ( ) . , ' !"
	end
	if RESERVED[string.lower(title)] then
		return nil, `"{title}" is a built-in page name.`
	end
	return title, nil
end

-- The data store key for a title. Upper/lower case doesn't matter, so
-- "Dial-up" and "dial-up" are the same entry.
function RetroPCShared.titleKey(title: string): string
	return "e_" .. string.gsub(string.lower(title), " ", "_")
end

-- Tidies an article body: Windows line endings, tabs and control characters.
-- Returns the clean body, or nil and the reason it was refused.
function RetroPCShared.cleanBody(raw: string): (string?, string?)
	if utf8.len(raw) == nil then
		return nil, "The text has invalid characters."
	end
	local body = string.gsub(raw, "\r\n?", "\n")
	body = string.gsub(body, "\t", "    ")
	body = string.gsub(body, "[%c]", function(c: string): string
		return if c == "\n" then c else ""
	end)
	body = RetroPCShared.trim(body)
	if body == "" then
		return nil, "The article is empty."
	end
	if #body > Config.BodyMaxLength then
		return nil, `The article is too long (max {Config.BodyMaxLength} characters).`
	end
	return body, nil
end

-- Rich text treats < > & as markup, so plain text has to escape them.
function RetroPCShared.escapeRichText(s: string): string
	s = string.gsub(s, "&", "&amp;")
	s = string.gsub(s, "<", "&lt;")
	s = string.gsub(s, ">", "&gt;")
	return s
end

function RetroPCShared.formatBytes(bytes: number): string
	if bytes < 1024 then
		return `{math.floor(bytes)} bytes`
	end
	return string.format("%.1f KB", bytes / 1024)
end

-- How long a page of this size takes over the "modem".
function RetroPCShared.loadSeconds(bytes: number): number
	return math.clamp(bytes / Config.PageBytesPerSecond, Config.MinPageLoadSeconds, Config.MaxPageLoadSeconds)
end

-- "28,800"
function RetroPCShared.commas(n: number): string
	local s = tostring(math.floor(n))
	local out = string.reverse((string.gsub(string.reverse(s), "(%d%d%d)", "%1,")))
	return (string.gsub(out, "^,", ""))
end

-- Titles containing the query (any case), sorted, at most `limit` of them.
function RetroPCShared.searchTitles(titles: { string }, query: string, limit: number): { string }
	local q = string.lower(RetroPCShared.trim(query))
	local found = {}
	if q == "" then
		return found
	end
	for _, title in titles do
		if string.find(string.lower(title), q, 1, true) then
			table.insert(found, title)
		end
	end
	table.sort(found, function(a: string, b: string): boolean
		return string.lower(a) < string.lower(b)
	end)
	while #found > limit do
		table.remove(found)
	end
	return found
end

---------------------------------------------------------------------------
-- Wiki markup
---------------------------------------------------------------------------
--[=[
	A small subset of the markup Wikipedia uses:
	  == Heading ==          section heading (=== Sub heading === too)
	  * item                 bullet point
	  ----                   horizontal line
	  '''bold'''  ''italic''
	  [[Other page]]         link to another entry
	  [[Other page|text]]    link that shows different text
	  a blank line           starts a new paragraph

	parseArticle returns a list of blocks the client turns into TextLabels,
	and the list of pages linked to (in order, no repeats). `exists` is
	asked about each link; missing pages are drawn red, like on Wikipedia.
]=]
export type Block = {
	kind: "heading" | "subheading" | "paragraph" | "bullet" | "rule",
	text: string, -- rich text, already escaped
}

export type ParsedArticle = {
	blocks: { Block },
	links: { string },
}

local LINK_COLOR = "#0000EE"
local MISSING_COLOR = "#CC0000"

local function inlineMarkup(line: string, links: { string }, seen: { [string]: boolean }, exists: (string) -> boolean): string
	local text = RetroPCShared.escapeRichText(line)

	text = string.gsub(text, "%[%[(.-)%]%]", function(inner: string): string
		local target, label = string.match(inner, "^(.-)|(.*)$")
		if not target then
			target, label = inner, inner
		end
		local title = RetroPCShared.normalizeTitle(target :: string)
		if not title then
			return label :: string -- not a valid page name: show as plain text
		end
		local key = string.lower(title)
		if not seen[key] then
			seen[key] = true
			table.insert(links, title)
		end
		local color = if exists(title) then LINK_COLOR else MISSING_COLOR
		return `<font color="{color}"><u>{label}</u></font>`
	end)

	-- Bold first, so ''' isn't read as '' followed by '.
	text = string.gsub(text, "'''(.-)'''", "<b>%1</b>")
	text = string.gsub(text, "''(.-)''", "<i>%1</i>")
	return text
end

function RetroPCShared.parseArticle(body: string, exists: ((string) -> boolean)?): ParsedArticle
	local check: (string) -> boolean = exists or function(_: string): boolean
		return true
	end
	local blocks: { Block } = {}
	local links: { string } = {}
	local seen: { [string]: boolean } = {}
	local paragraph: { string } = {}

	local function flush()
		if #paragraph > 0 then
			table.insert(blocks, { kind = "paragraph", text = table.concat(paragraph, " ") })
			table.clear(paragraph)
		end
	end

	for line in string.gmatch(body .. "\n", "(.-)\n") do
		local stripped = RetroPCShared.trim(line)
		local equals, heading = string.match(stripped, "^(==+)%s*(.-)%s*==+$")
		if stripped == "" then
			flush()
		elseif equals and heading ~= "" then
			flush()
			local kind: "heading" | "subheading" = if #equals == 2 then "heading" else "subheading"
			table.insert(blocks, { kind = kind, text = inlineMarkup(heading :: string, links, seen, check) })
		elseif string.match(stripped, "^%-%-%-%-+$") then
			flush()
			table.insert(blocks, { kind = "rule", text = "" })
		elseif string.match(stripped, "^%*") then
			flush()
			local item = RetroPCShared.trim(string.sub(stripped, 2))
			table.insert(blocks, { kind = "bullet", text = inlineMarkup(item, links, seen, check) })
		else
			table.insert(paragraph, inlineMarkup(stripped, links, seen, check))
		end
	end
	flush()

	return { blocks = blocks, links = links }
end

return RetroPCShared
