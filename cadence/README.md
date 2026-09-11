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
| `--write-icon [PATH]` | Write the app icon as a multi-resolution `.ico` (10 sizes, 16-256 px) |
| `--write-png [PATH]` / `--write-svg [PATH]` | Write the icon as PNG or SVG |

`Cadence.ico` is checked in, but `Cadence.py` draws it from the same geometry as
`Cadence.svg`, so `--write-icon` reproduces it byte for byte at any time. It stores
16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 px — the sizes the Windows shell asks
for across its DPI scalings, so it never has to downsample a larger entry. 256 px
is the format's ceiling: an ICO directory entry holds the width in a single byte.
`--write-png` writes 1024 px for anywhere that is not Windows.

## Building `Cadence.exe`

PyInstaller bundles the interpreter, so the result runs on a machine with no
Python installed. On Windows:

```bat
pip install pyinstaller
python Cadence.py --write-icon Cadence.ico
pyinstaller --onefile --noconsole --name Cadence --icon Cadence.ico Cadence.py
```

`dist\Cadence.exe` is the finished program — a single file, nothing beside it.

You can also let GitHub build it: `.github/workflows/build-cadence-exe.yml` runs
those same commands on a Windows runner and uploads the exe as a build artifact.
Trigger it from the **Actions** tab (**Build Cadence.exe** → **Run workflow**),
then download `Cadence-windows` from the finished run. Pushing a tag that starts
with `cadence-v` also attaches the exe to a GitHub release.

## What it does

**Library.** Scans on every launch. A file is only re-parsed when its size or
modification time changed, so the second launch is effectively instant. Deleted
files drop out of the index. `F5` rescans on demand; **Full** re-reads everything.

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

*Playback* is done by the browser's own audio engine. That covers MP3, WAV, FLAC,
M4A/AAC, Ogg and Opus on Chrome and Edge. Formats the engine cannot decode still
appear in the library with full metadata — they just will not play.

**Playing.** Queue with shuffle and repeat, seeking (the server answers HTTP range
requests, so scrubbing a large FLAC does not download it first), volume, play
counts, and cover art in the player and the details panel.

**Organising.** Playlists, export to `.m3u8`, multi-select with `Ctrl`/`Shift`,
sortable columns, search across title / artist / album / genre / filename, an
artist → album tree, and "Reveal in File Manager".

**Preferences.** The cog opens a popup holding the equalizer, the behaviour
switches, theme and accent, and the update check.

*Equalizer* — ten bands from 31 Hz to 16 kHz, a preamp, seven presets, and a
limiter. The Web Audio graph is only built the first time you switch it on, so
an untouched equalizer leaves playback exactly as it was.

*Columns* — Artist, Album, Album artist, Genre, Track, Year, Format, Plays and
Time can each be switched off. Number and Title always stay. A hidden tag is
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

Cadence opens your audio files read-only. It never writes tags, renames, moves or
deletes anything in your music folder.

Everything it *does* write lives in one directory —
`%APPDATA%\Cadence` on Windows, `~/.config/cadence` on Linux,
`~/Library/Application Support/Cadence` on macOS — holding `library.db`
(a SQLite index with the cached tags, playlists and play counts) and a browser
profile for the app window. Deleting it discards the cache and playlists and
nothing else.

The server binds to `127.0.0.1` only, never to your network. Each run mints a
random token that requests must carry, and the `Host` header is checked, so
another page in your browser cannot read your library through the local port.

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

- `--frameless` launches in kiosk mode, which has no OS title bar or buttons at
  all, leaving Cadence's as the only ones. It fills the screen and cannot be
  dragged or resized.
- Switch **Cadence's own window controls** off under Preferences and keep only
  the ones Windows draws.

## Releases

Every version, what changed in it, and the Windows executable built for it:
[CHANGELOG.md](CHANGELOG.md) ·
[Releases](https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases)

Putting a new version out is three edited files and a commit —
[UPDATES.md](UPDATES.md) is the walkthrough, including what the build does,
what people running Cadence see, and how to fix a release that went out wrong.

## License

MIT — see [LICENSE](../LICENSE).
