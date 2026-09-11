# Changelog

Every release of Cadence, newest first. The version here is the value of
`APP_VERSION` in `Cadence.py`, and each release is tagged `cadence-v<version>`
on the commit that produced its executable.

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

[1.0.1]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.0.1
[1.0.0]: https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/releases/tag/cadence-v1.0.0
