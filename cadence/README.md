<img src="Cadence.svg" width="72" align="left" alt="">

# Cadence

A music manager for a local library. Point it at one folder; it indexes that folder
and everything beneath it on every launch, and puts the result behind an interface
modelled on VS Code — activity bar, side bar, editor tabs, panel, command palette,
and a status bar in a dark blue accent.

<br clear="left">

![Cadence](docs/screenshot.png)

## The whole program is one file

`Cadence.py` is the entire application — tag parser, library index, web server,
interface and icon generator. It imports nothing outside the Python standard
library, so there is no `pip install` step and no `requirements.txt`.

```
python Cadence.py                       # first run asks you to pick a folder
python Cadence.py --folder "D:\Music"   # designate it up front and scan now
```

It serves a local page on `127.0.0.1` and opens it in a chromeless browser window
(Chrome, Edge or Brave in `--app` mode), so it looks like a desktop app rather
than a tab. If none of those are installed it falls back to your default browser.

Closing the window stops Cadence. The window and the program behind it are
separate processes, so the page checks in every few seconds and says goodbye on
its way out; the program exits a moment later. Nothing is left running in the
background. Reloading the page is not a goodbye, and neither is minimising for a
long time. `--keep-alive` turns this off if you want the server to outlive its
window.

### Options

| Flag | What it does |
| --- | --- |
| `--folder PATH` | Designate the music folder and scan it (also accepted positionally) |
| `--port N` | Pin the port (default `8731`; falls back to a free one if taken) |
| `--rescan` | Re-read every file, ignoring size and timestamp |
| `--no-scan` | Skip the launch scan |
| `--no-browser` | Start the server only |
| `--keep-alive` | Keep running after the window is closed |
| `--frameless` | Open without the OS title bar and window buttons (fills the screen) |
| `--portable` | Keep the index and settings beside the program, not in your user profile |
| `--no-watch` | Do not rescan automatically when the folder changes |
| `--write-icon [PATH]` | Write the app icon as a multi-resolution `.ico` (10 sizes, 16-256 px) |
| `--write-png [PATH]` / `--write-svg [PATH]` | Write the icon as PNG or SVG |

`Cadence.ico` is checked in, but `Cadence.py` draws it from the same geometry as
`Cadence.svg`, so `--write-icon` reproduces it byte for byte at any time. It stores
16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 px — the sizes the Windows shell asks
for across its DPI scalings, so it never has to downsample a larger entry. 256 px
is the format's ceiling: an ICO directory entry holds the width in a single byte.
`--write-png` writes 1024 px for anywhere that is not Windows.

## Building it

PyInstaller bundles the interpreter, so the result runs on a machine with no
Python installed. On Windows:

```bat
pip install pyinstaller
python Cadence.py --write-icon Cadence.ico
pyinstaller --onefile --noconsole --name Cadence --icon Cadence.ico Cadence.py
```

`dist\Cadence.exe` is the finished program — a single file, nothing beside it.

On Linux or macOS, leave `--noconsole` off: Cadence is started from a terminal
there, and its log is the only place problems would show up.

```sh
pip install pyinstaller
python Cadence.py --write-png Cadence.png
pyinstaller --onefile --name Cadence --add-data "Cadence.png:." Cadence.py
```

You can also let GitHub build all three: `.github/workflows/build-cadence-exe.yml`
runs those same commands on Windows, Linux and macOS runners and uploads each as
a build artifact. Trigger it from the **Actions** tab (**Build Cadence** →
**Run workflow**), then download `Cadence-windows`, `Cadence-linux-x86_64` or
`Cadence-macos-arm64` from the finished run. A commit message containing
`[release]`, or a tag starting with `cadence-v`, also attaches all three to a
GitHub release.

The Linux build is made on the runner's current Ubuntu, so it needs a
comparably recent glibc; on an older distribution, run `Cadence.py` directly
instead — it only needs Python 3.10 or newer.

### Code signing

Windows SmartScreen warns about any executable it has not seen signed by a
publisher it recognises, which is why the download shows a warning on first run.

The *tools* that apply a signature are free and open source — Microsoft's
`signtool` ships with the Windows SDK, and
[osslsigncode](https://github.com/mtrojnar/osslsigncode) and
[Jsign](https://ebourg.github.io/jsign/) do Authenticode from Linux and macOS.
What costs money is the *trust*: Windows believes a signature because the
certificate behind it chains to a CA in Microsoft's root programme. Signing with
a certificate you generated yourself is worse than not signing — the file then
claims a publisher nobody can verify, and SmartScreen blocks it harder.

For an open-source project there is a free route to a real certificate:

| Route | Cost | What it needs |
| --- | --- | --- |
| [SignPath Foundation](https://signpath.org/) | Free for OSS | An OSI-approved licence with no proprietary parts, an actively maintained project, and an application |
| A CA such as Certum's open-source offering | Roughly £20–30/year | Identity verification |
| [Azure Trusted Signing](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options) | About $10/month | An identity or business history check |

The workflow supports two of these and needs no code change to switch on.

**SignPath** keeps the private key on its own hardware and signs a build it
fetches from this repository, so nothing secret ever reaches the runner. Set the
secret `SIGNPATH_API_TOKEN` and the variable `SIGNPATH_ORGANIZATION_ID`;
`SIGNPATH_PROJECT_SLUG` and `SIGNPATH_POLICY_SLUG` override the defaults
(`cadence` and `release-signing`).

**A certificate held directly**, as a `.pfx`: set `WINDOWS_CERT_PFX` (the file,
base64-encoded) and `WINDOWS_CERT_PASSWORD`. Optionally set the variable
`WINDOWS_TIMESTAMP_URL` to use a timestamp server other than DigiCert's —
timestamping is what keeps a signature valid after the certificate expires.

Set up neither and builds publish unsigned, exactly as now; the build says which
route it took and what Windows makes of the result.

## What it does

**Library.** Scans on every launch. A file is only re-parsed when its size or
modification time changed, so the second launch is effectively instant. Deleted
files drop out of the index. `F5` rescans on demand; **Full** re-reads everything.

**The folder is watched.** Cadence checks the designated folder every few
seconds and rescans by itself when a file has been added, removed or changed, so
new music appears without a restart. `--no-watch`, or the preference, turns it
off and leaves you with `F5`.

**Formats.** Tags are read by hand-written parsers, so nothing needs installing:

| Container | Tags | Duration / bitrate | Cover art |
| --- | --- | --- | --- |
| MP3 | ID3v2.2 / 2.3 / 2.4, ID3v1 fallback | Xing / VBRI / CBR from frame headers | `APIC` |
| FLAC | Vorbis comments | `STREAMINFO` | `PICTURE` |
| Ogg Vorbis, Opus, Speex | Vorbis comments | Final page granule position | `METADATA_BLOCK_PICTURE` |
| MP4 / M4A / M4B | iTunes `ilst` atoms | `mdhd`, falling back to `mvhd` | `covr` |
| WAV | RIFF `LIST`/`INFO` + any ID3 chunk | `fmt ` byte rate | — |
| AIFF | `NAME` / `AUTH` | `COMM` | — |

Anything else with an audio extension is still indexed using its filename.
Files with no usable tags fall back to `Artist - Title` filename parsing and the
`Artist/Album/track` folder convention.

**Video is left out.** `.mp4` and `.ogg` are containers that can hold either
sound or pictures, so Cadence looks inside instead of trusting the extension: a
file with a video track is skipped, while an audio-only `.mp4` is indexed
normally. Skipped files are remembered, so a rescan does not reopen every video
in the folder.

*Playback* is done by the browser's own audio engine. That covers MP3, WAV, FLAC,
M4A/AAC, Ogg and Opus on Chrome and Edge. Formats the engine cannot decode still
appear in the library with full metadata — they just will not play.

**Playing.** Queue with shuffle and repeat, seeking (the server answers HTTP range
requests, so scrubbing a large FLAC does not download it first), volume, play
counts, and cover art in the player and the details panel.

**Playlists** come in two kinds, and the New Playlist dialog asks which you
want. One is kept **in the library index**, stored in `library.db` alongside
play counts, and travels wherever that file goes. The other is kept **only on
this computer**, held by the browser rather than written to the index — it
survives the index being deleted or rebuilt, and it does not follow `library.db`
to another machine. Local playlists remember their tracks by file path rather
than by row number, which is why a rebuilt index does not lose them.

**Smart playlists** are rules instead of a list: *genre is Electronic* **and**
*year is more than 2010*, sorted by year, capped at 25. Twelve fields, text and
numeric operators, match all or any. They re-evaluate themselves whenever the
library changes, so a smart playlist never needs maintaining.

**Editing tags.** `F2`, or **Edit Tags…** on the right-click menu. Title,
artist, album, album artist, genre, composer, year, track and disc, on one track
or on a whole selection at once — a field the selection disagrees on is left
blank and marked, so leaving it alone keeps each track's own value.

MP3 and FLAC can be written; every other format is read-only and says so. Only
the tag block is rebuilt — the audio frames are copied through byte for byte and
cover art is carried over — and the new file is written beside the original then
moved into place in one step, so an interruption leaves the original intact. The
file is read back afterwards and the index updated from what it actually says.

**Organising.** Playlists, export to `.m3u8`, multi-select with `Ctrl`/`Shift`,
sortable columns, search across title / artist / album / genre / filename, an
artist → album tree, and "Reveal in File Manager".

**Preferences.** The cog opens a popup holding the equalizer, the behaviour
switches, theme and accent, and the update check.

*Equalizer* — ten bands from 31 Hz to 16 kHz, a preamp, seven presets, and a
limiter. The Web Audio graph is only built the first time you switch it on, so
an untouched equalizer leaves playback exactly as it was.

*Gapless and crossfade* — with gapless on, the next track is fetched and decoded
about a second before the current one ends and started as it runs out, so
nothing is clipped and nothing pauses. Crossfade, 0 to 12 seconds, plays both
together and ramps one down as the other comes up. Two audio decks take turns to
do it.

*Columns* — Artist, Album, Album artist, Genre, Composer, Track, Year, Format,
Plays and Time can each be switched off. Number and Title always stay. A hidden tag is
still read, still searchable and still shown in the Details panel; it is only
kept out of the list.

*Behaviour* — follow the playing track, single click plays, cover art in the
player, animate the playing indicator, compact rows, rescan on launch, count
plays, check for updates.

**Updates.** Cadence asks GitHub at most once a day whether a newer release
exists and shows a chip in the status bar when one does. The Windows build can
download and install it: the new executable is written beside the running one,
your current version is kept as `Cadence-previous.exe`, and Cadence restarts
into the new build. Nothing is ever downloaded without you pressing the button,
and the whole check can be switched off.

### Keyboard

| | |
| --- | --- |
| `Ctrl+P` / `Ctrl+Shift+P` | Quick open / command palette |
| `Ctrl+Shift+E` `F` `Y` `Q` | Library · Search · Playlists · Queue |
| `Ctrl+B` / `Ctrl+J` | Toggle side bar / panel |
| `Space` · `Ctrl+←` `→` | Play-pause · previous · next |
| `S` · `R` · `M` | Shuffle · repeat · mute |
| `Enter` · `Ctrl+A` · `Ctrl+C` · `Del` | Play selection · select all · copy path · remove |
| `F2` | Edit the tags of the selection |
| `F5` | Rescan |

## If the icon looks stale on Windows

Windows keeps its own icon cache, and it does not always notice that a file's
icon changed — especially when the new executable has the same name and path as
the old one. If Cadence still shows an old icon after updating, clear the cache
rather than reinstalling:

```bat
ie4uinit.exe -show
```

If that does not do it, sign out and back in. The icon inside the executable is
the one this repository ships; you can confirm it by running
`Cadence.exe --write-icon check.ico` and comparing that file with `Cadence.ico`
— they are byte for byte identical.

## Your files

Cadence opens your audio files read-only, with exactly one exception: **Edit
Tags…**, which you have to ask for. It never renames, moves or deletes anything
in your music folder, and it never touches a file you did not select.

When it does write a tag, it rewrites only the tag block — the audio frames are
copied through byte for byte and cover art is carried over — into a new file
beside the original, which is then moved into place in one step. An interrupted
write therefore leaves the original file exactly as it was.

Everything else it writes lives in one directory —
`%APPDATA%\Cadence` on Windows, `~/.config/cadence` on Linux,
`~/Library/Application Support/Cadence` on macOS — holding `library.db`
(a SQLite index with the cached tags, playlists and play counts) and a browser
profile for the app window. Deleting it discards the cache and playlists and
nothing else.

**Portable mode** puts that directory beside the program instead: pass
`--portable`, or drop a file named `cadence-portable.txt` next to the
executable, and the index lands in `Cadence-data` in the same folder. A USB
stick is then self-contained, and your user profile is left untouched.

The server binds to `127.0.0.1` only, never to your network. Each run mints a
random token that requests must carry, and the `Host` header is checked, so
another page in your browser cannot read your library through the local port.

### What leaves your computer

Your music never does. Cadence has no account, no sync, and no upload of any
kind. GitHub hosts the program; it never sees anything about your library.

The entire program makes exactly one kind of outbound request, and only when
**Check for updates** is on:

```
GET https://api.github.com/repos/.../releases/latest
```

That is a read. It carries no body — no filenames, no paths, no tags, no play
counts, nothing about your library — and the reply is simply the newest version
number. GitHub sees a request arriving, as any website does, and nothing more.
Pressing **Download and install** then fetches the new executable, which is the
same file you would download from the releases page by hand. Untick **Check for
updates** in Preferences and Cadence makes no outbound request at all.

Everything else is loopback: the page talks to `127.0.0.1` and nowhere else.
Your files stay where they are, read-only, and the index stays in the config
directory above. You can verify all of this yourself — search `Cadence.py` for
`https://` and you will find one address.

## Themes

Eight themes — Dark, Black, Nord, Gruvbox, Solarized Dark, Light, Paper and
Solarized Light — and fifteen accent ramps: Deep Blue, Midnight, Azure, Steel,
Indigo, Teal, Slate, Ocean, Violet, Plum, Ember, Amber, Moss, Crimson and
Graphite. Theme and accent are independent:
the theme sets the surfaces, the accent tints the status bar, selection, buttons
and tab markers from one seven-stop ramp. The status bar item on the right
cycles through the themes.

## Window controls

The three dots at the left of the menu bar: red quits Cadence and stops the
server, green goes full screen, amber leaves it. A web page cannot minimise its
own window, so the amber control is a restore-down rather than a minimise.

Windows draws its own buttons on the window as well, and **a page cannot hide
those** — they belong to the browser window, not to the page inside it. Two ways
out, whichever you prefer:

- Switch **Hide the Windows title bar and buttons** on under Preferences. The
  window reopens without a frame, leaving Cadence's controls as the only ones.
  It fills the screen and cannot be dragged or resized, because dropping the
  frame is the only way to be rid of the buttons on it. The choice is
  remembered, so a double-clicked shortcut honours it. `--frameless` does the
  same from a command line.
- Or keep a normal window and switch **Cadence's own window controls** off
  instead, so there is one set of buttons rather than two.

## Releases

Every version, what changed in it, and the executables built for it —
Windows, Linux and macOS:
[CHANGELOG.md](CHANGELOG.md) ·
[Releases](https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases)

Putting a new version out is three edited files and a commit —
[UPDATES.md](UPDATES.md) is the walkthrough, including what the build does,
what people running Cadence see, and how to fix a release that went out wrong.

## License

MIT — see [LICENSE](../LICENSE).
