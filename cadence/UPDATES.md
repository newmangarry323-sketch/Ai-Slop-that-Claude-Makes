# Shipping an update to Cadence

How to put a new version of Cadence out, what happens when you do, and what
goes wrong. Written for whoever has the repository in front of them — no
Windows machine, Python install or build tools needed on your side, because
GitHub builds the executable.

If you only want to read what changed in each version, that is
[CHANGELOG.md](CHANGELOG.md). This file is about *making* a release.

---

## The short version

```bash
# 1. edit these three, keeping the version identical in all of them:
#      cadence/Cadence.py     APP_VERSION = "1.2.0"
#      cadence/version.txt    filevers / prodvers / FileVersion / ProductVersion
#      cadence/CHANGELOG.md   a new "## 1.2.0" section at the top

git add -A
git commit -m "Whatever you changed [release]"
git push origin <your-branch>
```

The `[release]` marker in the commit message is what publishes. Without it the
workflow still builds and checks the executable, it just does not create a
release. About 90 seconds later the release is live with `Cadence.exe`
attached, and everyone's copy of Cadence starts offering the update.

---

## Step 1 — pick the version number

`MAJOR.MINOR.PATCH`, compared numerically, so `1.0.10` is newer than `1.0.9`.

| Change | Bump |
| --- | --- |
| Fix something that was broken | PATCH — `1.1.0` → `1.1.1` |
| Add a feature, nothing breaks | MINOR — `1.1.0` → `1.2.0` |
| Change something people relied on | MAJOR — `1.1.0` → `2.0.0` |

The number must go **up**. The in-app updater compares the release tag against
the running build and only offers an update when the release is strictly newer,
so re-releasing the same number is invisible to anyone already running it.

## Step 2 — edit three files

The version lives in three places. **CI fails the build if they disagree**, so
you cannot ship a mismatch, but it is easier to get them right first time.

**`cadence/Cadence.py`** — one line near the top:

```python
APP_VERSION = "1.2.0"
```

**`cadence/version.txt`** — the Windows file-properties resource. Four places,
all four-part with a trailing `0`:

```python
filevers=(1, 2, 0, 0),
prodvers=(1, 2, 0, 0),
...
StringStruct('FileVersion', '1.2.0.0'),
StringStruct('ProductVersion', '1.2.0.0'),
```

**`cadence/CHANGELOG.md`** — a new section at the very top, newest first. The
heading must be exactly `## ` followed by the bare version, no `v`:

```markdown
## 1.2.0

One line saying what this release is about.

- What changed, and why it matters to someone using it.
- Say what a change means rather than which function moved.
- "Fixed: " for bug fixes, so they are easy to pick out.
```

That section becomes the release notes verbatim — it is the only description
anyone downloading will read, so write it for them and not for yourself.

## Step 3 — commit with `[release]`

```bash
git commit -m "Add gapless playback [release]"
git push origin <your-branch>
```

The marker can go anywhere in the message. Pushing a `cadence-v*` **tag** also
publishes, if you can push tags — some environments block tag refs, which is
why the marker exists.

Pushes to `main` and to any `claude/**` branch build. A push without the marker
builds and checks but publishes nothing, which is the right way to test that a
change compiles before committing to a release.

## Step 4 — what the workflow does

`.github/workflows/build-cadence-exe.yml`, on `windows-latest`, about 90
seconds:

1. **Sanity-check** — parses `Cadence.py` and confirms the version agrees
   across the three files. Fails the build if not.
2. **Generate the icon** — `Cadence.py --write-icon`, so the packaged icon is
   always drawn from the current source rather than a stale committed file.
3. **Work out the tag** — reads `APP_VERSION` and forms `cadence-v<version>`.
4. **Clear a stale release** — if a release already holds that tag, delete it,
   because a tag cannot be re-pointed through the releases API.
5. **Compose the notes** — pulls your `## <version>` section out of
   `CHANGELOG.md` and appends install instructions and links to the previous
   release.
6. **Build** — PyInstaller `--onefile --noconsole`, with the icon embedded, the
   `.ico` packed inside as data, and `version.txt` as the version resource.
7. **Smoke-test** — actually runs the built executable. A `--noconsole` build
   cannot print, so it is asked to write its icon to a file; producing that
   file proves the program starts and its code runs.
8. **Upload** the artifact, **publish** the release at the commit that was
   built, and **re-link** older releases so their notes point at what follows.

Watch it under **Actions → Build Cadence.exe**. If it fails, the log names the
step, and nothing is published.

## Step 5 — what people running Cadence see

- Cadence asks GitHub at most **once a day** whether a newer release exists.
- When there is one, a chip appears in the status bar and the Updates section
  of the Preferences popup fills in with your notes.
- On the Windows build, **Download and install** fetches the new executable
  beside the running one, renames the current one to `Cadence-previous.exe`,
  swaps the new one in, and restarts. Nothing is downloaded until that button
  is pressed, and the check can be switched off entirely.
- Running from source, the updater points at the release page instead.

Anyone who wants out can untick **Check for updates**, and anyone who wants
back to the old build renames `Cadence-previous.exe` over `Cadence.exe`.

---

## Fixing a release that went out wrong

**Nobody has downloaded it yet** — commit again with `[release]` and the same
version. The workflow deletes the old release and tag and republishes at the
new commit. Check the download count on the release page first.

**Someone already has it** — do not reuse the number. Ship a PATCH bump with a
`Fixed:` line saying what was wrong. Overwriting a version people are running
means two different programs claim to be the same build.

**Only the notes are wrong** — edit `CHANGELOG.md` and commit with `[release]`.
Every release's notes are regenerated from the changelog, including older ones.

---

## Things that will bite you

**The repository is the updater's source of truth.** `UPDATE_REPO` in
`Cadence.py` names `newmangarry323-sketch/Ai-Slop-that-Claude-Makes`. Rename or
move the repository and every already-released copy keeps checking the old
address. Nothing can fix that retroactively — update the constant *before* any
move so the next release is already looking in the right place.

**The asset must end in `.exe`.** The updater picks the first release asset
whose name ends that way. Rename it and the update offer silently disappears.

**The tag must keep the `cadence-v` prefix.** `TAG_PREFIX` in `Cadence.py`
strips it to get the version. A differently-shaped tag parses to nothing and
compares as older.

**`workflow_dispatch` only appears once the workflow file is on the default
branch.** Until then, "Run workflow" returns 404 and the push trigger is how
builds start.

**The executable is not code-signed**, so Windows SmartScreen warns on first
run: *More info* → *Run anyway*. Signing needs a certificate you have to buy.

**Windows caches icons per path.** A new build with a changed icon at the same
filename often still shows the old one. `ie4uinit.exe -show` clears it; signing
out and back in always does. The icon inside the executable is fine — confirm
with `Cadence.exe --write-icon check.ico` and compare against `Cadence.ico`.

**Release notes come from the changelog, not the commit message.** A beautiful
commit message with no changelog entry produces a release with nothing in it.

---

## Releasing without GitHub

If you ever want to build it yourself on a Windows machine:

```bat
pip install pyinstaller
cd cadence
python Cadence.py --write-icon Cadence.ico
pyinstaller --onefile --noconsole --name Cadence --icon Cadence.ico ^
  --add-data "Cadence.ico;." --version-file version.txt Cadence.py
```

`dist\Cadence.exe` is the result. Attaching it to a GitHub release by hand
works exactly the same as far as the updater is concerned, as long as the tag
is `cadence-v<version>` and the asset ends in `.exe`.

---

## Ideas not built yet

Nothing here is promised — it is a place to write down what you might want next
so it is not lost.

- [ ] Code signing, to stop the SmartScreen warning
- [ ] Gapless playback and crossfade
- [ ] Editing tags from inside Cadence (it only reads them today)
- [ ] Watching the music folder for changes instead of rescanning on launch
- [ ] Smart playlists (rules rather than a fixed list)
- [ ] A portable mode that keeps the index beside the executable
- [ ] Linux and macOS builds — the program already runs there from source
