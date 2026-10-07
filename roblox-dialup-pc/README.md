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
> the shared module's 57 tests pass in the Luau interpreter (see
> [Checking the code](#checking-the-code)), and the code was reviewed against the
> Roblox Creator Docs. Layout and timing can only be checked in Studio, so open
> **View → Output** on the first run and look for errors.

**Contents:** [What's in it](#whats-in-it) ·
[Install](#install) · [Using it](#using-it) ·
[Changing who can edit](#changing-who-can-edit) · [Settings](#settings) ·
[How it works](#how-it-works) · [Using your own PC model](#using-your-own-pc-model) ·
[Troubleshooting](#troubleshooting) · [Things to try changing](#things-to-try-changing) ·
[Checking the code](#checking-the-code) · [Sources](#sources)

## What's in it

| Part | What you get |
|---|---|
| The PC | Desk, CRT monitor with a glowing screen, tower with floppy and CD drive, keyboard, mouse, external modem whose lights blink while someone is using the PC |
| Boot | BIOS screen (CPU, memory count to 16384K, drives, modem), then a splash screen. Click to skip. The second time you use the PC it skips the BIOS |
| Desktop | Teal background, icons you double-click, Start menu, taskbar with a button for each window, a clock, and a "connected" icon whose lights flicker while you're online |
| Windows | Grey 3D bevels, navy title bars, drag them by the title bar, minimise and close buttons, error and warning message boxes |
| Dial-Up Networking | Connect To dialog (your username, a masked password, the phone number), a dialing sequence with status text, then "Connected at 28,800 bps" with a running duration timer. Optional dial-up sound |
| WebWalker browser | Back, Forward, Home, Reload, All Pages, Random, Help, Stop and (editor only) New. A Location bar you can type page names or addresses into. Status bar ("Transferring data from blockopedia.local: 1.2 KB of 3.4 KB") |
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
5. Press **Play**. The PC is on a desk 14 studs in front of the origin.

The name `RetroPCShared` matters: both scripts look it up by that name.

**Saving pages for real.** Out of the box, Studio can't use DataStores, so the wiki
keeps pages in memory and loses them when you stop playing (the editor gets a
warning). Live servers of a published game use the DataStore automatically. To save
from Studio as well: publish the place, open **File → Experience Settings →
Security**, turn on **Enable Studio Access to API Services**, and click **Save**.
Roblox warns that Studio then reads and writes **the same DataStores as your live
game**, so do this in a separate test copy of the game, not the one people play.

### B. Rojo

If you use [Rojo](https://rojo.space/): `rojo serve` in this folder and connect from
the Studio plugin, with a place made from Studio's Baseplate template open.

`rojo build -o retropc.rbxlx` also works, but the file it makes only has the three
scripts: no floor and no spawn point, so you'd fall forever. Open it and insert a
Baseplate (an anchored Part with its top at Y = 0) and a SpawnLocation before
pressing Play.

## Using it

1. Walk up to the PC and press **E** (or tap the prompt).
2. Double-click **Dial-Up Networking** and press **Connect**, or just open
   **Blockopedia**: it can't find the server, and offers a **Dial up now** button.
3. Double-click **Blockopedia**. Click a blue link in a list, or under **Links on this
   page** at the end of an article, to move around. Links inside the article text are
   coloured but can't be clicked (see [Wiki markup](#wiki-markup) for why). Red links
   are pages that don't exist yet.
4. Type a page name in the **Location** box and press Enter to jump to it. If no page
   has that name, it searches instead. Addresses like
   `http://blockopedia.local/wiki/Dial-up_modem` work too.

### Writing articles (L3g3ndDrag0n2007 only)

Press **New** on the toolbar, or **Edit this page** on an article, or **Start this
article** on a missing page. **Show preview** shows the result before you save. The
markup is a small part of Wikipedia's:

| Type this | To get |
|---|---|
| `'''bold'''` | **bold** |
| `''italic''` | *italic* |
| `'''''both'''''` | ***bold italic*** |
| `[[Other page]]` | a link to another article |
| `[[Other page\|click me]]` | a link with different text |
| `== Heading ==` | a section heading (`=== Sub heading ===` for smaller) |
| `* item` | a bullet point |
| `----` | a horizontal line |
| a blank line | a new paragraph |

**Titles** must start with a letter or digit, can use the letters A–Z (no accents),
digits, spaces and `- ( ) . , ' !`, and can be up to 40 characters. Upper and lower
case count as the same title ("Modem" and "modem" are one page). These names belong
to the browser and can't be titles: Main Page, All Pages, Search, Random, Help,
New Article.

**Articles** can be up to 6,000 characters. Accented letters and emoji count as one
character each, and a tab counts as 4 (it's turned into 4 spaces).

## Changing who can edit

In `RetroPCShared`, near the top:

```lua
EditorUsernames = { "L3g3ndDrag0n2007" },
EditorUserIds = {} :: { number },
```

Add more names to the list for more editors. Use the **username** (the @name on a
profile), not the display name: display names aren't unique, so anyone could copy
one. Roblox lets people change their username, and if that happens the name no longer
matches. A **user ID** never changes, so it's worth adding yours too, e.g.
`EditorUserIds = { 123456789 }`. The number is in the address of your Roblox profile
page.

## Settings

Every setting is in the `Config` table at the top of `RetroPCShared`, with a comment
next to each one. The position of the demo PC is the one setting kept in
`RetroPCServer` (`DEMO_PC_CFRAME`, at the top).

| Setting | Default | What it does |
|---|---|---|
| `EditorUsernames` | `{ "L3g3ndDrag0n2007" }` | Usernames that can create and edit articles (any case) |
| `EditorUserIds` | `{}` | User IDs that can create and edit articles |
| `WikiName`, `WikiSlogan` | `"Blockopedia"`, `"the free encyclopedia"` | The wiki's name and tagline |
| `OSName`, `BrowserName` | `"RetroOS 95"`, `"WebWalker"` | Names on the boot screen, desktop and browser |
| `SiteHost` | `"blockopedia.local"` | The made-up address shown in the Location bar |
| `ModemBitsPerSecond` | `28800` | The speed shown in the BIOS and "Connected at" message |
| `PhoneNumber` | `"555-0142"` | The number the Connect To dialog shows |
| `DialSeconds` | `7` | How long dialing takes |
| `DialUpSoundId` | `""` | A sound for dialing, written as `"rbxassetid://<number>"`. Empty means silent |
| `PageBytesPerSecond` | `3000` | How fast pages "download" |
| `MinPageLoadSeconds`, `MaxPageLoadSeconds` | `0.6`, `4` | Shortest and longest page load |
| `TitleMaxLength` | `40` | Longest title. Keep it at 48 or less: DataStore keys are limited to 50 characters and the key adds `e_` |
| `BodyMaxLength` | `6000` | Longest article, in characters |
| `SpawnDemoPC` | `true` | Build the demo PC on a desk. `false` = only your own tagged models |
| `PromptDistance` | `8` | How close (studs) you must be for the "Use computer" prompt |
| `MaxUseDistance` | `16` | Further than this (studs, from the PC model's pivot) and the screen switches off and saves are refused |
| `DataStoreName` | `"RetroPCWiki_v1"` | Where articles are saved. Changing it starts an empty wiki (the old one is kept under the old name) |
| `SaveCooldown` | `3` | Seconds between saves per player |
| `MaxRequestsPerSecond` | `10` | Requests per player per second, all kinds |
| `CacheSeconds` | `30` | How long a server trusts its copy of the page list and pages. Pages saved on another server show up after at most this long |
| `AllowUnfilteredInStudio` | `true` | In Studio without DataStore access, save even if the text filter fails. See [Text filtering](#text-filtering-required-by-roblox) |
| `PCTag` | `"RetroPC"` | The tag that marks a model as a PC |
| `RemoteFunctionName`, `OpenEventName` | `"RetroPCRemote"`, `"RetroPCOpen"` | Names of the remotes the server creates in ReplicatedStorage |

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

They talk through two remotes the server creates in ReplicatedStorage:

* **`RetroPCOpen`**, a RemoteEvent. When you press E, the server's
  `ProximityPrompt.Triggered` handler fires it to you alone with the PC model, and the
  client's `OnClientEvent` turns the monitor on. That's why only the player who
  pressed E sees the screen.
* **`RetroPCRemote`**, a RemoteFunction for everything else. The client calls
  `remote:InvokeServer("get", "Modem")` and waits. The server's `OnServerInvoke`
  looks up `handlers.get` and returns a table like `{ ok = true, entry = {...} }`.
  The requests are `hello` (am I an editor? is the DataStore working?), `index`,
  `get`, `search`, `random`, `save` and `closed`.

Roblox's own security guide says to treat everything that arrives from a client as
possibly faked, so every handler checks the types of its arguments, and the server
limits each player to `MaxRequestsPerSecond` requests.

### Why the editor lock can't be bypassed

Look at `saveEntry` in `RetroPCServer`. The first thing it does is:

```lua
if not Shared.isEditor(player.Name, player.UserId) then
	return fail("Access denied. ...")
end
```

`player` is filled in **by Roblox**, not sent by the client, so nobody can pretend to
be someone else. The client asks the server once, with the `hello` request
(`handlers.hello` calls `isEditor` with the real Player), and uses the answer only to
decide whether to show the **New** and **Edit** buttons. Even with those buttons
forced back on, the server still refuses. The server also re-checks the title and
body rules, because a modified client could skip its own checks.

### Saving: DataStore and the index

Articles go in a DataStore called `RetroPCWiki_v1`:

* `e_modem` holds one article:
  `{ title, body, author, authorId, created, updated, revision }`. The key is the
  lower-case title with `_` for spaces, so "Modem" and "modem" are the same article.
  Roblox limits keys to 50 characters, which is why titles stop at 40.
* `index` holds the list of every title, so "All pages", search and "Recently changed"
  don't have to load every article.

Saves use `UpdateAsync`, which reads and writes in one step. That matters when two
servers save at once: neither overwrites the other's index entry. The server keeps a
copy of the index and of articles it has read for 30 seconds (`CacheSeconds`), so most
page views never touch the DataStore.

DataStores have a per-minute request budget for each server, shared by every read
and save. The server only reads an article from the DataStore if its title is in the
index. Without that, anyone could ask for thousands of made-up titles, use up the
budget and make the editor's saves fail.

On a live server the DataStore is always used. If a request fails (because Roblox's
servers are busy, say), the page shows "The server is busy" and **Reload** tries
again; a failed read of the page list is retried after 5 seconds. Memory-only mode is
only for Studio without API access.

### Text filtering (required by Roblox)

Anything a player writes that other players will see has to go through Roblox's text
filter. Roblox says it can take a game down if it doesn't. `filterForEveryone` calls
`TextService:FilterStringAsync(...)` and then `GetNonChatStringForBroadcastAsync()`, and
the filtered text is what gets saved. If the filter can't run, the save is refused,
because unfiltered text must not be shown.

The text is filtered once, when it's saved, not every time someone reads it. That's
because `FilterStringAsync` needs the author to be in the same server, and readers are
usually in other servers. The broadcast filter (the one for text everyone can see) is
used for that reason.

The filter often doesn't work in Studio. When Studio has **no** DataStore access
(pages only kept in memory), `AllowUnfilteredInStudio = true` lets a Studio test save
anyway, because that text never leaves your Studio session. When Studio **has**
DataStore access, a failed filter refuses the save, because Studio shares the
DataStore with your live game and the text would reach real players. Live servers
never save unfiltered text.

### Drawing the screen

* The monitor is a `ScreenGui`. Inside it, the screen is laid out as an
  **800 × 600** display (SVGA, a common late-90s resolution), and a `UIScale` makes
  it fit your window. So every position in the client is in "screen pixels", e.g.
  `UDim2.fromOffset(10, 8)`.
* The 3D grey look is `bevel()`: two 1-pixel lines on each edge, light on the top and
  left and dark on the bottom and right. Swap the colours and the same thing looks
  pressed in, which is how buttons sink when you click them.
* A full-screen button with `Modal = true` frees the mouse even in first person.
* Long-running sequences (boot, dialing, page loads, saving) check a **session** or
  **token** number after every wait. Turning the PC off or clicking a new link changes
  the number, and the old sequence notices and stops. Search for `navToken` to see it.

### Pages at modem speed

`reveal()` adds up roughly how many bytes a page is, works out how long that takes at
`PageBytesPerSecond`, and shows the page's pieces one by one as the bytes "arrive",
updating the status bar as it goes. `loadSeconds` in the shared module keeps it
between 0.6 and 4 seconds so long articles don't take forever.

### Wiki markup

`parseArticle` in `RetroPCShared` turns markup into **rich text** (Roblox's
`<b>`, `<i>`, `<font color>` tags):

1. It escapes `<`, `>` and `&` first, so an article can't inject its own tags.
2. It finds `[[links]]` with the Lua pattern `%[%[(.-)%]%]`. `.-` means "as few
   characters as possible", so two links on one line don't merge into one.
3. It turns `'''''`, `'''` and `''` into tags, longest first.
4. `balanceTags` fixes the order of the tags. Rich text needs tags closed in the
   reverse order they were opened (`<b><i>x</i></b>`, never `<b><i>x</b></i>`), and
   markup like `'''bold ''both''' italic''` overlaps, so it closes and reopens tags
   where needed.

Rich text can't make part of a label clickable, so each article also lists its links
as buttons under **Links on this page**.

## Using your own PC model

1. Select your **Model** and add the tag `RetroPC` (in the **Tags** section of the
   Properties window, or run `CollectionService:AddTag(model, "RetroPC")`).
2. The server gives it a "Use computer" prompt. The prompt goes on a part with a
   boolean attribute `IsScreen`, or the model's PrimaryPart, or any part.
3. Distance is measured from the model's **pivot**. For a big model (a whole office,
   say), set its PrimaryPart near the screen, or players more than `MaxUseDistance`
   (16) studs from the pivot will see the screen switch off.
4. For blinking modem lights, give small parts a number attribute `LightIndex`.
   Light 1 stays on while the PC is in use; 2 and up flicker.

Set `SpawnDemoPC = false` to remove the built-in PC. You can have as many PCs as you
like; they all share one wiki. Copies made with `Clone()` while the game runs work too.
Tagged models outside Workspace (a template in ServerStorage, say) don't count as a
place you can save from.

## Troubleshooting

* **No PC, or no prompt.** Look in **View → Output**. If the server waits for
  `RetroPCShared`, Roblox prints a warning after 5 seconds that the thread may yield
  indefinitely: the ModuleScript isn't in ReplicatedStorage, or isn't named exactly
  `RetroPCShared`. The server part must be a **Script** in ServerScriptService. The
  prompt only shows within 8 studs (`PromptDistance`) of the screen.
* **Pressing E does nothing.** The LocalScript must be in **StarterPlayer →
  StarterPlayerScripts**. Check Output for an error from `RetroPCClient`.
* **"[RetroPC] DataStore unavailable..." in Output.** Normal in Studio without API
  access: pages are kept in memory. See [Saving pages for real](#a-copy-and-paste-no-extra-tools).
* **"Text filter failed in Studio; saving unfiltered to memory only".** Normal in
  Studio without DataStore access.
* **"The text filter isn't working right now".** The filter didn't answer, or (in
  Studio with DataStore access) it doesn't work in Studio. On a live server, try
  again later. In Studio, test editing without API access (memory only) or in a live
  server.
* **"That title was blocked by the Roblox text filter".** The filter changed the title
  (it hides some words, and things that look like personal information), so pick
  another one.
* **The editor can't see New or Edit.** `EditorUsernames` needs the username (the
  @name), not the display name. If the account was renamed, add its user ID to
  `EditorUserIds`. Multi-client tests in Studio use simulated test players, not your
  account, so test editing with a normal **Play**.
* **"You have to be at a computer to save".** You're more than `MaxUseDistance` (16)
  studs from the PC model's pivot.
* **"Saving too fast"** means wait 3 seconds (`SaveCooldown`). **"Too many
  requests"** means more than 10 requests in one second.
* **A page saved on another server is missing.** Each server refreshes its page list
  every 30 seconds (`CacheSeconds`), so wait and press **Reload**.

## Things to try changing

These are good ways to learn the code. Each one only needs a small change:

1. **Faster modem.** Set `ModemBitsPerSecond = 56000` and `PageBytesPerSecond = 7000`.
   What else on screen changes, and why? (Search for `ModemBitsPerSecond`.)
2. **Dial-up sound.** Upload a sound you're allowed to use and put
   `"rbxassetid://<its number>"` in `DialUpSoundId`. Read `dial()` to see where it
   plays and stops.
3. **Busy signal.** Give `dial()` a 1-in-5 chance of failing with "The line is busy",
   using `math.random`. Where does the window need redrawing?
4. **Delete button.** Add a `handlers.delete` on the server, copying how `saveEntry`
   checks the editor. Remove the title from the `index` too (with `updateKey`), and
   clear the server's copies (`entryCache[key]`, `indexCache`), or this server keeps
   showing the page for up to 30 seconds.
5. **New markup.** Make `__text__` underline. You only need one more `string.gsub`
   in `inlineMarkup`, and one more line in the tests.
6. **Visited links.** `COLORS.visited` is already defined. Keep a table of visited
   titles and use it in `articleLink`.

## Checking the code

Run these from inside this folder (`roblox-dialup-pc/`).

The shared module has tests that run in the standalone Luau interpreter
([luau-lang/luau releases](https://github.com/luau-lang/luau/releases)):

```sh
luau tests/shared.test.luau
```

To type-check all three scripts against the Roblox API, use
[luau-lsp](https://github.com/JohnnyMorganz/luau-lsp). It needs the Roblox
definitions file `globalTypes.d.luau` from the `scripts/` folder of the luau-lsp
repository, and a sourcemap made by Rojo:

```sh
rojo sourcemap default.project.json -o sourcemap.json
luau-lsp analyze --platform=roblox --definitions=globalTypes.d.luau --sourcemap=sourcemap.json src
```

Don't commit `sourcemap.json` or `globalTypes.d.luau`. In Studio, **View → Script
Analysis** does the same job.

## Sources

* Roblox Creator Docs, [Text filtering](https://create.roblox.com/docs/ui/text-filtering):
  you must filter displayed text players write, using `FilterStringAsync` and
  `GetNonChatStringForBroadcastAsync`
* Roblox Creator Docs, [TextService.FilterStringAsync](https://create.roblox.com/docs/reference/engine/classes/TextService#FilterStringAsync):
  throws if the author isn't on the current server
* Roblox Creator Docs, [Data stores](https://create.roblox.com/docs/cloud-services/data-stores):
  turning on Studio access, and the warning that Studio uses the same data stores as
  the live game
* Roblox Creator Docs, [Data store error codes and limits](https://create.roblox.com/docs/cloud-services/data-stores/error-codes-and-limits):
  50-character key names, 4,194,304 characters per value, per-server request limits
* Roblox Creator Docs, [Player.DisplayName](https://create.roblox.com/docs/reference/engine/classes/Player#DisplayName):
  display names aren't unique
* Roblox Creator Docs, [Instance.WaitForChild](https://create.roblox.com/docs/reference/engine/classes/Instance#WaitForChild):
  the warning after 5 seconds of waiting
* Roblox Creator Docs, [GuiButton.Modal](https://create.roblox.com/docs/reference/engine/classes/GuiButton#Modal):
  frees the mouse while the button is visible
* Roblox Creator Docs, [Rich text](https://create.roblox.com/docs/ui/rich-text): tags,
  closing them in reverse order, and the `&lt;` `&gt;` `&amp;` escapes
* Roblox Creator Docs, [Appearance modifiers](https://create.roblox.com/docs/ui/appearance-modifiers):
  `UIStroke` on text outlines the text unless `ApplyStrokeMode` is `Border`, and
  `UIPadding` pads all of a frame's contents
* Roblox Creator Docs, [CollectionService](https://create.roblox.com/docs/reference/engine/classes/CollectionService):
  tags, and adding them in the Properties window
* Roblox Creator Docs, [Security and cheat mitigation tactics](https://create.roblox.com/docs/scripting/security/security-tactics):
  "Never trust the client"; the server validates every request
* Roblox Creator Docs, [Securing the client-server boundary](https://create.roblox.com/docs/scripting/security/client-server-boundary):
  validating what arrives through remotes
* ITU-T, [Recommendation V.34 (09/94)](https://www.itu.int/rec/T-REC-V.34-199409-S/en):
  "Modem operating at data signalling rates of up to 28 800 bit/s"
* FOLDOC, [V.34](https://foldoc.org/V.34)
* Wikipedia, [555 (telephone number)](https://en.wikipedia.org/wiki/555_(telephone_number)):
  555-0100 to 555-0199 are reserved for fictional use
