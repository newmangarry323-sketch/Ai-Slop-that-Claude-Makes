**Cadence — a local music manager with a VS Code style interface**

I keep my music as files in a folder and wanted something that just reads that folder and plays it. Cadence points at one folder, indexes everything beneath it on every launch, and puts it behind an interface modelled on VS Code: activity bar, side bar, editor tabs, command palette, status bar.

It's one Python file using only the standard library, so there's no `pip install`. That constraint meant writing the tag parsers from the format specs — ID3v2, FLAC, Ogg Vorbis, Opus, MP4/M4A, WAV, AIFF, with cover art. A SQLite index only re-reads files whose size or timestamp changed, so relaunching is instant, and the server answers HTTP range requests so seeking a large FLAC doesn't download it first. Plus a 10-band Web Audio equalizer, playlists, 8 themes and 15 accents.

**How Claude helped:** Claude Code wrote it, across one long conversation where I described what I wanted and reported what was broken. It handled the tedious parts well — the binary format parsers, an icon rasteriser that draws its own `.ico`, a workflow that builds the Windows exe. It also drove a headless browser to test its own UI, catching bugs I hadn't reported: hidden panes still rendering, every icon cropping to a corner, the equalizer faders being unusable outside one browser.

**Free:** MIT licensed. No account, no paid tier.

**Security and data:** The Windows build is **not code-signed**, so SmartScreen warns on first run — only run it if you're comfortable with that, or run the source with Python instead. Your audio files are opened read-only: no tags written, nothing renamed or moved. The server binds to 127.0.0.1 only and requires a random per-run token. Its one outbound request reads the latest release number from GitHub for the update check; it sends nothing about your library and can be switched off. Index and playlists stay in `%APPDATA%\Cadence`.
