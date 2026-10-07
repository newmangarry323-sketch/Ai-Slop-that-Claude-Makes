# Roblox Retro PC (dial-up wiki)

A working 90s computer for Roblox. Walk up to the beige PC on the desk and press
**E**. The monitor fills your screen, the BIOS counts its memory, and **RetroOS 95**
boots to a teal desktop. Dial up ("Dialing... Verifying user name and password...
Connected at 28,800 bps") and open **Blockopedia**, a Wikipedia-style encyclopedia,
in the **WebWalker** browser. Pages trickle in at modem speed with a status bar,
a progress bar and a spinning throbber.

Everyone can read Blockopedia. **Only `L3g3ndDrag0n2007` can create or edit
articles.** The server checks this before every save, so a hacked client can't get
round it.

> **Status: not tested in Roblox Studio yet.** It was written without Studio. All
> three scripts pass `luau-lsp` type checking against the Roblox API definitions,
> and the shared module's 47 tests pass in the Luau interpreter (see
> [Checking the code](#checking-the-code)). Layout and timing can only be checked
> in Studio, so open **View → Output** on the first run and look for errors.

## What's in it

| Part | What you get |
|---|---|
| The PC | Desk, CRT monitor with a glowing screen, tower with floppy and CD drive, keyboard, mouse, external modem whose lights blink while someone is online |
| Boot | BIOS screen (CPU, memory count to 16384K, drives, modem), then a splash screen. Click to skip. The second time you use the PC it skips the BIOS |
| Desktop | Teal background, icons you double-click, Start menu, taskbar with a button for each window, a clock, and a "connected" icon whose lights flicker |
| Windows | Grey 3D bevels, navy title bars, drag them by the title bar, minimise and close buttons, error and warning message boxes |
| Dial-Up Networking | Connect To dialog (your username, a masked password, the phone number), a dialing sequence with status text, then "Connected at 28,800 bps" with a running duration timer. Optional dial-up sound |
| WebWalker browser | Back, Forward, Home, Reload, All Pages, Random, Help, Stop and (editor only) New. A Location bar you can type page names or URLs into. Status bar ("Transferring data from blockopedia.local: 1.2 KB of 3.4 KB") |
| Blockopedia | Main page with recent changes, articles, red links for pages that don't exist yet, "Links on this page", search, an A–Z list of all pages, a help page, "403 Forbidden" for anyone else who tries to edit |
| Editor (one user only) | Create and edit articles with Wikipedia-style markup, a live character counter and a preview. Saved to a DataStore, so pages survive server restarts |
| Leaving | Start → Shut Down ("It's now safe to turn off your computer."), or the POWER button on the monitor |

## Install

You need three scripts. Pick **one** of the two ways below.

### A. Copy and paste (no extra tools)

1. In Studio, open your place.
2. **ReplicatedStorage** → Insert → **ModuleScript**, name it `RetroPCShared`,
   paste in [`src/shared/RetroPCShared.luau`](src/shared/RetroPCShared.luau).
3. **ServerScriptService** → Insert → **Script**, name it `RetroPCServer`,
   paste in [`src/server/RetroPCServer.server.luau`](src/server/RetroPCServer.server.luau).
4. **StarterPlayer → StarterPlayerScripts** → Insert → **LocalScript**, name it
   `RetroPCClient`, paste in [`src/client/RetroPCClient.client.luau`](src/client/RetroPCClient.client.luau).
5. To save articles between sessions: publish the place, then **File → Game Settings
   → Security → Enable Studio Access to API Services**. Without this the wiki still
   works, but pages are kept in memory and lost when the server stops (the editor
   gets a warning).
6. Press **Play**. The PC is on a desk 14 studs in front of the origin.

The name `RetroPCShared` matters: both scripts look it up by that name.

### B. Rojo

If you use [Rojo](https://rojo.space/): `rojo serve` in this folder and connect from
the Studio plugin, or `rojo build -o retropc.rbxlx` to build a place file.

## Using it

1. Walk up to the PC and press **E** (or tap the prompt).
2. Double-click **Dial-Up Networking** and press **Connect**, or just open
   **Blockopedia**: it can't find the server, and offers a **Dial up now** button.
3. Double-click **Blockopedia**. Click blue links to move around. Red links are pages
   that don't exist yet.
4. Type a page name in the **Location** box and press Enter to jump to it. If no page
   has that name, it searches instead.

### Writing articles (L3g3ndDrag0n2007 only)

Press **New** on the toolbar, or **Edit this page** on an article, or **Start this
article** on a missing page. The markup is a small part of Wikipedia's:

| Type this | To get |
|---|---|
| `'''bold'''` | **bold** |
| `''italic''` | *italic* |
| `[[Other page]]` | a link to another article |
| `[[Other page\|click me]]` | a link with different text |
| `== Heading ==` | a section heading (`=== Sub heading ===` for smaller) |
| `* item` | a bullet point |
| `----` | a horizontal line |
| a blank line | a new paragraph |

Titles can use letters, digits, spaces and `- ( ) . , ' !`, up to 40 characters.
Articles can be up to 6,000 characters.

## Changing who can edit

In `RetroPCShared`, near the top:

```lua
EditorUsernames = { "L3g3ndDrag0n2007" },
EditorUserIds = {} :: { number },
```

Add more names to the list for more editors. Roblox lets people change their username,
and if that happens the name no longer matches. A **user ID** never changes, so it's
worth adding yours too, e.g. `EditorUserIds = { 123456789 }`. The number is in the
address of your Roblox profile page.

## How it works

Reading this next to the code is the quickest way to learn it. Every script has
section headings (`-- Storage`, `-- Pages`, ...) you can search for.

### Who does what: client and server

Roblox runs your game in two places: the **server** (one per game instance) and the
**client** (each player's own computer). Players can change anything their client
does, so:

* `RetroPCClient` (a LocalScript) draws everything you see on the PC. It **asks**
  the server for pages and **asks** it to save.
* `RetroPCServer` (a Script) owns the data. It builds the PC, stores the articles,
  and decides whether a save is allowed.
* `RetroPCShared` (a ModuleScript) holds settings and functions both sides need, so
  the rules are only written once.

They talk through a **RemoteFunction** called `RetroPCRemote`. The client calls
`remote:InvokeServer("get", "Modem")` and waits. The server's `OnServerInvoke`
looks up `handlers.get` and returns a table like `{ ok = true, entry = {...} }`.
Roblox's own security guide covers why the server must check everything that comes
from a client.

### Why the editor lock can't be bypassed

Look at `saveEntry` in `RetroPCServer`. The first thing it does is:

```lua
if not Shared.isEditor(player.Name, player.UserId) then
	return fail("Access denied. ...")
end
```

`player` is filled in **by Roblox**, not sent by the client, so nobody can pretend to
be someone else. The client also calls `isEditor`, but only to decide whether to show
the **New** and **Edit** buttons. Even with those buttons forced back on, the server
still refuses. The server also re-checks the title and body rules, because a modified
client could skip its own checks.

### Saving: DataStore and the index

Articles go in a DataStore called `RetroPCWiki_v1`:

* `e_modem` holds one article: `{ title, body, author, created, updated, revision }`.
  The key is the lower-case title with `_` for spaces, so "Modem" and "modem" are the
  same article. Roblox limits keys to 50 characters, which is why titles stop at 40.
* `index` holds the list of every title, so "All pages", search and "Recently changed"
  don't have to load every article.

Saves use `UpdateAsync`, which reads and writes in one step. That matters when two
servers save at once: neither overwrites the other's index entry. The server keeps a
copy of the index and of articles it has read for 30 seconds (`CacheSeconds`), so most
page views never touch the DataStore. DataStores have per-minute request limits, and
this caching keeps well under them.

### Text filtering (required by Roblox)

Anything a player writes that other players will see has to go through Roblox's text
filter. Roblox says it can take a game down if it doesn't. `filterForEveryone` calls
`TextService:FilterStringAsync(...)` and then `GetNonChatStringForBroadcastAsync()`, and
the filtered text is what gets saved. If the filter can't run, the save is refused,
because unfiltered text must not be shown. The filter often doesn't work in Studio, so
`AllowUnfilteredInStudio` lets Studio tests save anyway. Live servers ignore that
setting.

### Drawing the screen

* The monitor is a `ScreenGui`. Inside it, the screen is laid out as an
  **800 × 600** display (SVGA, a common late-90s resolution), and a `UIScale` makes
  it fit your window. So every position in the client is in "screen pixels", e.g.
  `UDim2.fromOffset(10, 8)`.
* The 3D grey look is `bevel()`: two 1-pixel lines on each edge, light on the top and
  left and dark on the bottom and right. Swap the colours and the same thing looks
  pressed in, which is how buttons sink when you click them.
* A full-screen button with `Modal = true` frees the mouse even in first person.
* Long-running sequences (boot, dialing, page loads) check a **session** or
  **token** number after every wait. Turning the PC off or clicking a new link changes
  the number, and the old sequence notices and stops. Search for `navToken` to see it.

### Pages at modem speed

`reveal()` adds up roughly how many bytes a page is, works out how long that takes at
`PageBytesPerSecond`, and shows the page's pieces one by one as the bytes "arrive",
updating the status bar as it goes. `loadSeconds` in the shared module keeps it
between 0.6 and 4 seconds so long articles don't take forever.

### Wiki markup

`parseArticle` in `RetroPCShared` turns markup into **rich text** (Roblox's
`<b>`, `<i>`, `<font color>` tags). It escapes `<`, `>` and `&` first, so an article
can't inject its own tags. Then it finds `[[links]]` with the Lua pattern
`%[%[(.-)%]%]`. `.-` means "as few characters as possible", so two links on one
line don't merge into one. Rich text can't make part of a label clickable, so each
article also lists its links as buttons under **Links on this page**.

## Using your own PC model

Tag any **Model** with `RetroPC` using the Tag Editor (or
`CollectionService:AddTag(model, "RetroPC")`). The server gives it a "Use computer"
prompt. The prompt goes on a part with a boolean attribute `IsScreen`, or the model's
PrimaryPart, or any part. Set `SpawnDemoPC = false` in the settings to remove the
built-in one. You can have as many PCs as you like; they all share one wiki.

## Things to try changing

These are good ways to learn the code. Each one only needs a small change:

1. **Faster modem.** Set `ModemBitsPerSecond = 56000` and `PageBytesPerSecond = 7000`.
   What else on screen changes, and why? (Search for `ModemBitsPerSecond`.)
2. **Dial-up sound.** Upload a sound you're allowed to use, put its ID in
   `DialUpSoundId`. Read `dial()` to see where it plays and stops.
3. **Busy signal.** Give `dial()` a 1-in-5 chance of failing with "The line is busy",
   using `math.random`. Where does the window need redrawing?
4. **Delete button.** Add a `handlers.delete` on the server, copying how `saveEntry`
   checks the editor. Remember to remove the title from the `index` too.
5. **New markup.** Make `__text__` underline. You only need one more `string.gsub`
   in `inlineMarkup`, and one more line in the tests.
6. **Visited links.** `COLORS.visited` is already defined. Keep a table of visited
   titles and use it in `articleLink`.

## Checking the code

The shared module has tests that run in the standalone Luau interpreter
([luau-lang/luau releases](https://github.com/luau-lang/luau/releases)):

```sh
luau tests/shared.test.luau
```

To type-check all three scripts against the Roblox API, use
[luau-lsp](https://github.com/JohnnyMorganz/luau-lsp) with its Roblox definitions file
and a sourcemap made by Rojo:

```sh
rojo sourcemap default.project.json -o sourcemap.json
luau-lsp analyze --platform=roblox --definitions=globalTypes.d.luau --sourcemap=sourcemap.json src
```

In Studio, **View → Script Analysis** does the same job.

## Sources

* Roblox Creator Docs, [Text filtering](https://create.roblox.com/docs/ui/text-filtering):
  you must filter displayed text players write, using `FilterStringAsync` and
  `GetNonChatStringForBroadcastAsync`
* Roblox Creator Docs, [Data store error codes and limits](https://create.roblox.com/docs/cloud-services/data-stores/error-codes-and-limits):
  50-character key names, 4,194,304 characters per value, per-server request limits
* Roblox Creator Docs, [GuiButton.Modal](https://create.roblox.com/docs/reference/engine/classes/GuiButton#Modal):
  frees the mouse while the button is visible
* Roblox Creator Docs, [Rich text](https://create.roblox.com/docs/ui/rich-text): tags
  and the `&lt;` `&gt;` `&amp;` escapes
* Roblox Creator Docs, [Security and cheat mitigation tactics](https://create.roblox.com/docs/scripting/security/security-tactics):
  "Never trust the client"; the server validates every request
* Roblox Creator Docs, [Securing the client-server boundary](https://create.roblox.com/docs/scripting/security/client-server-boundary):
  validating what arrives through remotes
* ITU-T, [Recommendation V.34 (09/94)](https://www.itu.int/rec/T-REC-V.34-199409-S/en):
  "Modem operating at data signalling rates of up to 28 800 bit/s"
* FOLDOC, [V.34](https://foldoc.org/V.34)
* Wikipedia, [555 (telephone number)](https://en.wikipedia.org/wiki/555_(telephone_number)):
  555-0100 to 555-0199 are reserved for fictional use
