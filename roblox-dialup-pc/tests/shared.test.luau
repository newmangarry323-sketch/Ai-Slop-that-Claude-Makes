--[[
	Tests for RetroPCShared, run with the standalone Luau interpreter
	(https://github.com/luau-lang/luau/releases):

	    luau tests/shared.test.luau

	Exits with an error if any check fails.
]]

local Shared = require("../src/shared/RetroPCShared")

local failures = 0
local count = 0

local function check(name: string, got: any, want: any)
	count += 1
	if got ~= want then
		failures += 1
		print(`FAIL {name}\n  got:  {tostring(got)}\n  want: {tostring(want)}`)
	end
end

-- isEditor -------------------------------------------------------------
check("editor exact name", Shared.isEditor("L3g3ndDrag0n2007", 1), true)
check("editor any case", Shared.isEditor("l3g3nddrag0n2007", 1), true)
check("not editor", Shared.isEditor("SomeoneElse", 1), false)
check("lookalike name is not editor", Shared.isEditor("L3g3ndDrag0n2008", 1), false)
check("name with extra text is not editor", Shared.isEditor("L3g3ndDrag0n2007x", 1), false)
table.insert(Shared.Config.EditorUserIds, 42)
check("editor by user id", Shared.isEditor("Renamed", 42), true)
table.clear(Shared.Config.EditorUserIds)

-- normalizeTitle -------------------------------------------------------
check("title tidy spaces", (Shared.normalizeTitle("  Dial-up   modem ")), "Dial-up modem")
check("title empty", (Shared.normalizeTitle("   ")), nil)
check("title bad char", (Shared.normalizeTitle("<b>hi</b>")), nil)
check("title must start alnum", (Shared.normalizeTitle("-dash")), nil)
check("title reserved", (Shared.normalizeTitle("main PAGE")), nil)
check("title too long", (Shared.normalizeTitle(string.rep("a", 41))), nil)
check("title max length ok", (Shared.normalizeTitle(string.rep("a", 40))), string.rep("a", 40))
check("title punctuation", (Shared.normalizeTitle("Rock 'n' Roll (1950s), vol. 1!")), "Rock 'n' Roll (1950s), vol. 1!")

-- titleKey -------------------------------------------------------------
check("key", Shared.titleKey("Dial-up Modem"), "e_dial-up_modem")
check("key length under 50", #Shared.titleKey(string.rep("a", 40)) < 50, true)

-- cleanBody ------------------------------------------------------------
check("body CRLF", (Shared.cleanBody("a\r\nb")), "a\nb")
check("body control chars removed", (Shared.cleanBody("a\0b\7c")), "abc")
check("body empty", (Shared.cleanBody("  \n ")), nil)
check("body too long", (Shared.cleanBody(string.rep("x", 6001))), nil)
check("body invalid utf8", (Shared.cleanBody("bad \255 byte")), nil)
-- The limit counts characters: 6000 two-byte letters fit, 6001 don't.
check("body accented 6000 ok", Shared.cleanBody(string.rep("é", 6000)) ~= nil, true)
check("body accented 6001 too long", (Shared.cleanBody(string.rep("é", 6001))), nil)
check("length counts characters", Shared.length("héllo"), 5)
check("tab becomes 4 spaces", (Shared.cleanBody("a\tb")), "a    b")
check("title message", select(2, Shared.normalizeTitle("Café")), "Titles must start with a letter or digit and can only use A-Z, 0-9, spaces and - ( ) . , ' !")

-- escape / format ------------------------------------------------------
check("escape", Shared.escapeRichText("<a & b>"), "&lt;a &amp; b&gt;")
check("commas", Shared.commas(28800), "28,800")
check("commas small", Shared.commas(56), "56")
check("commas million", Shared.commas(1234567), "1,234,567")
check("bytes", Shared.formatBytes(500), "500 bytes")
check("kb", Shared.formatBytes(2048), "2.0 KB")
check("load min", Shared.loadSeconds(1), Shared.Config.MinPageLoadSeconds)
check("load max", Shared.loadSeconds(1e9), Shared.Config.MaxPageLoadSeconds)

-- search ---------------------------------------------------------------
local found = Shared.searchTitles({ "Modem", "Dial-up", "modem history", "Floppy" }, "MODEM", 10)
check("search count", #found, 2)
check("search sorted", found[1], "Modem")
check("search limit", #Shared.searchTitles({ "a1", "a2", "a3" }, "a", 2), 2)
check("search empty query", #Shared.searchTitles({ "a" }, "  ", 5), 0)

-- parseArticle ---------------------------------------------------------
local body = table.concat({
	"A '''modem''' turns data into ''sound''.",
	"It uses a [[Phone line]].",
	"",
	"== History ==",
	"* First [[Modem|modems]] were slow",
	"* See [[Phone line]] again",
	"----",
	"Text with <script> & [[<bad>]] stuff",
}, "\n")
local parsed = Shared.parseArticle(body, function(title)
	return title == "Modem"
end)
local b = parsed.blocks
check("block count", #b, 6)
check("para joins lines", b[1].kind, "paragraph")
check(
	"para rich text",
	b[1].text,
	'A <b>modem</b> turns data into <i>sound</i>. It uses a <font color="#CC0000"><u>Phone line</u></font>.'
)
check("heading", b[2].kind, "heading")
check("heading text", b[2].text, "History")
check("bullet", b[3].kind, "bullet")
check("bullet link label", b[3].text, 'First <font color="#0000EE"><u>modems</u></font> were slow')
check("rule", b[5].kind, "rule")
check("escaped", b[6].text, "Text with &lt;script&gt; &amp; &lt;bad&gt; stuff")
check("links unique", #parsed.links, 2)
check("links order", parsed.links[1], "Phone line")
check("links second", parsed.links[2], "Modem")
check("subheading", Shared.parseArticle("=== Sub ===").blocks[1].kind, "subheading")
check("unclosed bold left alone", Shared.parseArticle("'''x").blocks[1].text, "'''x")

-- Rich text tags must close in reverse order, even when the markup overlaps.
local function rich(markup: string): string
	return Shared.parseArticle(markup).blocks[1].text
end
check("bold italic", rich("'''''both''''' text"), "<b><i>both</i></b> text")
check("overlap bold then italic", rich("'''bold ''both''' italic''"), "<b>bold <i>both</i></b><i> italic</i>")
check("overlap italic then bold", rich("''a '''b'' c'''"), "<i>a <b>b</b></i><b> c</b>")
check("nested is unchanged", rich("'''a ''b'' c'''"), "<b>a <i>b</i> c</b>")
check(
	"bold across a link",
	rich("[[Page|'''Bold]] text'''"),
	'<font color="#0000EE"><u><b>Bold</b></u></font><b> text</b>'
)

print(`{count - failures}/{count} checks passed`)
if failures > 0 then
	error(`{failures} check(s) failed`)
end
