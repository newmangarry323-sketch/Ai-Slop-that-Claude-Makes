# Changelog

Every release of Cadence, newest first. The version here is the value of
`APP_VERSION` in `Cadence.py`, and each release is tagged `cadence-v<version>`
on the commit that produced its executable.

## 1.3.0

The seven things on the roadmap, six of them finished.

- **Tag editing.** Cadence only read tags before; now it writes them too.
  Select a track and press `F2`, or use **Edit Tags…** on the right-click menu.
  Title, artist, album, album artist, genre, composer, year, track and disc
  can all be changed, and several tracks can be retagged at once — a field the
  selection disagrees on is left blank and marked, so leaving it alone keeps
  each track's own value. MP3 and FLAC only for now; anything else says so
  rather than pretending.

  The write is deliberately careful. Only the tag block is rebuilt: the audio
  frames are copied through byte for byte, and existing cover art is carried
  over. The new file is written beside the original and moved into place in one
  step, so an interruption leaves the original intact rather than a half-written
  file. After writing, the file is read back and the index is updated from what
  the file actually says, so the library can never claim a tag the file does not
  have.

- **Gapless playback and crossfade.** Two audio decks take turns. With gapless
  on, the next track is fetched and decoded about a second before the current
  one ends, then started as it runs out — so the ending of a track is no longer
  clipped and there is no pause between them. Crossfade (0–12 seconds, in
  Preferences) plays both together and ramps one down as the other comes up.

- **The music folder is watched.** Cadence noticed new files only when it was
  restarted or when you pressed `F5`. It now checks the folder every few
  seconds and rescans by itself when something has been added, removed or
  changed. `--no-watch`, or the preference, switches it off.

- **Smart playlists.** Rules rather than a fixed list of tracks: *genre is
  Electronic* **and** *year is more than 2010*, sorted by year, capped at 25.
  Twelve fields, text and numeric operators, match all or any, and the list
  re-evaluates itself whenever the library changes. They live in the index, so
  they travel with `library.db`.

- **Portable mode.** `--portable`, or a file named `cadence-portable.txt`
  beside the executable, keeps the index, settings and playlists in a
  `Cadence-data` folder next to the program instead of in your user profile.
  A USB stick is then self-contained.

- **Linux and macOS builds.** The program already ran there from source; the
  release now carries built executables for both alongside the Windows one.
  They are console programs — start them from a terminal.

- **Code signing is wired up but not yet switched on.** Signing is what removes
  the Windows SmartScreen warning. The tools that apply a signature are free and
  open source; what Windows actually checks is that the certificate behind the
  signature chains to a CA it trusts, and a certificate you generate yourself
  is worse than none — the file then claims a publisher nobody can verify.
  SignPath Foundation issues one free to open-source projects, which is the
  route this repository would take.

  The build now takes either route with no code change: SignPath, which holds
  the key on its own hardware and signs a build it fetches from this repository
  so nothing secret reaches the runner, or a certificate held directly as a
  `.pfx` in the repository secrets. Set up neither and it publishes unsigned as
  before, saying so in the log along with what Windows makes of the result.
  README.md lists what each route needs.

Also in this release:

- Composer is a column you can switch on, a field you can search on in a smart
  playlist, and part of what the Details panel shows. It was read from files all
  along but never sent to the interface.
- Changing the music folder, or a file appearing while a scan is already
  running, no longer competes with the watcher.

## 1.2.4

- **Fixed: tracks would not play until the equalizer was switched off and on
  again.** If the equalizer had ever been enabled, Cadence rebuilt its audio
  graph while the page was still loading — before anything had been clicked.
  A browser starts an audio graph created that early in a suspended state, and
  once the player is routed through a suspended graph the result is silence
  rather than an error, which is why toggling the equalizer appeared to fix it:
  that click was the interaction the graph had been waiting for. The graph is
  now built on the first real interaction instead, and resumed whenever it goes
  to sleep. With the equalizer off, no audio graph is created at all.
- **Fixed: a folder could delete another folder's tracks.** Deciding which
  tracks had gone missing compared paths by prefix, so `D:\Music2` counted as
  being inside `D:\Music` and its tracks were removed from the library as
  missing. Paths are compared by containment now.
- **Fixed: changing the music folder left the old folder's tracks behind.**
  Only files under the designated folder are ever scanned, so those leftovers
  could never be refreshed or removed — they sat in the library pointing at
  files Cadence was no longer watching. Changing the folder now drops them.
- Fixed: every disclosure chevron pointed the wrong way. A collapsed section
  pointed up and an expanded one pointed right. Collapsed points right and
  expanded points down now.
- **Hide the Windows title bar** is on the View menu as well as in Preferences.
- Malformed ids in a request are answered rather than raising and dropping the
  connection.

## 1.2.2

- Fixed: choosing an equalizer preset applied it correctly but then showed
  **Custom** in the dropdown. Moving a band by hand clears the preset name,
  since the curve is no longer that preset, and applying one was tripping over
  its own bands doing exactly that.

## 1.2.1

- **Hiding the Windows title bar is a setting now, not just a flag.** 1.1.3 added
  `--frameless`, which was no use to anyone launching Cadence by double-clicking
  it, since that passes no flags. It is a switch in Preferences, it is
  remembered, and flipping it reopens the window there and then rather than
  waiting for a restart.

  Worth being plain about the trade: a page cannot hide the buttons its own
  browser window draws. Switching this on therefore drops the frame altogether
  by opening full screen, leaving only Cadence's own controls — the window can
  no longer be dragged or resized. If you would rather keep a normal window,
  leave this off and instead switch off **Cadence's own window controls**, so
  there is only one set rather than two.

## 1.2.0

- **Playlists can now be kept on this computer instead of in the index.** The
  New Playlist dialog asks where it should live. One kind is stored in
  `library.db` as before, alongside play counts, and travels with that file. The
  other is held by the browser and never written to the index, so it survives
  the index being deleted or rebuilt and stays on the machine it was made on.
  They sit in their own group in the Playlists sidebar and behave the same
  otherwise — open, play, rename, delete, add and remove tracks, export.

  Local playlists remember tracks by file path rather than by row number,
  because row numbers are reassigned when the index is rebuilt and paths are
  not. If a track is missing from the library the list says so rather than
  quietly shrinking.

- Documented what leaves your computer, which is nothing about your music. The
  whole program makes one outbound request, a read of the latest release number
  from GitHub, and only when the update check is on.

## 1.1.4

- **Video files are no longer indexed.** `.mp4` is a container, not a format —
  it holds music videos as readily as albums — so Cadence was picking up video
  from folders like Downloads. It now looks inside rather than trusting the
  extension: a file with a video track is left out, while an audio-only `.mp4`
  is still indexed as before. The same check covers Ogg carrying Theora.
  Anything already in your library from an earlier build is re-read once and
  dropped if it turns out to be video, and the verdict is remembered so a
  rescan does not reopen every video in the folder.
- **Fixed: the equalizer could not be adjusted on some browsers.** The faders
  were native vertical sliders, which depend on browser behaviour that has
  changed more than once and does not exist at all in some browsers — where it
  is missing the control collapses to an unusably narrow horizontal slider.
  They are drawn by Cadence now, so they behave the same everywhere, and they
  take the keyboard: arrows adjust, Page Up and Page Down move in threes, Home
  and End go to the extremes, and 0 returns a band to flat.

## 1.1.3

- **Four more themes** — Black, Nord, Gruvbox and Paper join Dark, Light and the
  two Solarized variants. Eight in total; the status bar item cycles them.
- **Nine more accent ramps** — Slate, Ocean, Violet, Plum, Ember, Amber, Moss,
  Crimson and Graphite. Fifteen in total, and they stay independent of the
  theme, so any accent works with any theme.
- Fixed: the marks inside the window controls sat slightly off centre. They were
  text characters, and a glyph is centred by its line box rather than by its
  ink, so the cross and the dash landed at different heights — and the arrow was
  not in the interface font at all. They are drawn as shapes now, which centres
  exactly and looks the same on every machine.
- `--frameless` opens without the operating system's title bar and window
  buttons, leaving Cadence's own as the only ones on screen. A page cannot hide
  the window buttons its browser draws, so this launches the window in kiosk
  mode instead: the trade is that it fills the display and cannot be dragged or
  resized.
- Cadence's own window controls can be switched off under Preferences, for
  anyone who would rather keep only the ones Windows draws than have both.

## 1.1.2

- **Closing the window now stops Cadence.** The window is a browser and the
  program behind it is a separate process, and closing the window told that
  process nothing, so it stayed running with the port held — invisible except
  in Task Manager, and accumulating one copy per launch. The page now checks in
  every few seconds and says goodbye on its way out; the program stops a few
  seconds later. Reloading the page is not a goodbye. Neither is minimising for
  a long stretch: browsers throttle a hidden window's timers to about one a
  minute, so the silent grace is longer than that, while a window that closes
  properly still shuts things down in a few seconds. `--keep-alive` opts out.
- Quitting from inside Cadence — the red control, or File → Exit — now closes
  the window it opened as well as stopping the server.

## 1.1.1

- The running version now shows in the status bar, so which build you are on is
  visible at a glance instead of buried in the settings tab. Clicking it opens
  Preferences, where the update check lives.

## 1.1.0

A preferences popup, an equalizer, Solarized, and update checking.

- **Preferences popup.** The cog in the activity bar now opens a popup rather
  than a settings tab. It holds the equalizer, the behaviour switches, theme
  and accent, and the update check. The full settings tab is still there behind
  **All settings…**, under **View**, and in the command palette.
- **Equalizer.** Ten bands (31 Hz to 16 kHz), a preamp, seven presets, and a
  limiter for when boosted bands would otherwise clip. Built on Web Audio
  peaking filters. The audio graph is only created the first time the equalizer
  is switched on, so leaving it alone leaves playback untouched.
- **Choose which columns the track list shows.** Artist, Album, Album artist,
  Genre, Track, Year, Format, Plays and Time can each be switched off; Number
  and Title always stay. Hidden tags are still read, still searchable, and
  still shown in the Details panel. Sorting falls back to the default order if
  the column it was sorting by is hidden.
- **Behaviour switches:** follow the playing track, single click plays, cover
  art in the player, animate the playing indicator, compact rows, rescan on
  launch, count plays, check for updates.
- **Solarized Dark and Solarized Light** join Dark and Light. The status bar
  now cycles through all four. Accent ramps are independent of the theme.
- **Update checking.** Asks GitHub at most once a day whether a newer release
  exists and shows a chip in the status bar when there is one. On the Windows
  build it can download and install: the new executable is written beside the
  running one, the current version is kept as `Cadence-previous.exe`, and
  Cadence restarts into the new build. Nothing is downloaded without pressing
  the button, and the check can be switched off.
- **Window controls** sit at the left of the menu bar. Red quits Cadence and
  stops the server; green goes full screen; amber leaves it. A web page cannot
  minimise its own window, so amber is a restore-down rather than a minimise.
- The app now serves a real `/favicon.ico` carrying all ten sizes, plus PNG and
  SVG icons, so the window and taskbar have a proper icon to use. The `.ico` is
  also packed inside the executable.
- Fixed: the gear icon was a malformed hand-drawn path. It is now generated
  from gear geometry — eight teeth and a hub.

## 1.0.1

Sharper icon at high-DPI display scaling.

- The `.ico` now stores 16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 px. It was
  missing 20, 40 and 96 — the sizes the Windows shell asks for at 125%, 150%
  and 200% scaling. Windows falls back to downsampling a larger entry when the
  exact size is absent, which is what made the icon look soft.
- The 64 px entry moved from a DIB to PNG, so the file is smaller at ten sizes
  than it was at seven.
- `--write-png` now writes 1024 px instead of 512, for uses outside Windows.
  256 px remains the largest `.ico` entry: the format stores an entry's width
  in a single byte.
- The icon renderer skips pixels outside a shape's bounding box before
  evaluating its distance field, roughly halving render time. Output is
  unchanged — verified byte for byte against the previous renderer at every
  stored size.

## 1.0.0

First release.

- Indexes one designated folder and everything beneath it on every launch,
  re-reading only files whose size or modification time changed.
- Interface modelled on VS Code: activity bar, side bar, editor tabs,
  breadcrumbs, a toggleable panel, command palette and quick open, and a
  status bar in a dark blue accent. Dark and light themes, six accent ramps.
- Tag parsing written from the format specifications, with no third-party
  libraries: ID3v2.2/2.3/2.4 and ID3v1, FLAC, Ogg Vorbis, Opus, Speex,
  MP4/M4A, WAV and AIFF, including embedded cover art. MP3 duration is
  recovered from Xing, VBRI or CBR frame headers.
- Playback with queue, shuffle and repeat. The local server answers HTTP range
  requests, so seeking a large lossless file does not download it first.
- Playlists with `.m3u8` export, search, sortable virtualised track list,
  play counts, and "Reveal in File Manager".
- The whole program is one file importing only the standard library. It also
  draws its own icon, so `--write-icon` produces the `.ico` used to package it.
- Windows executable built with PyInstaller by GitHub Actions.

[1.2.3]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.2.3
[1.2.4]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.2.4
[1.2.2]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.2.2
[1.2.1]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.2.1
[1.2.0]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.2.0
[1.1.4]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.1.4
[1.1.3]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.1.3
[1.1.2]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.1.2
[1.1.1]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.1.1
[1.1.0]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.1.0
[1.0.1]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.0.1
[1.0.0]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.0.0
