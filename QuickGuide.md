# 4KDowner — QuickGuide for humans & AI assistants

Read this file first when starting a new session on this repo.  
Plain-text twin: [`QuickGuide.txt`](QuickGuide.txt) (same content).  
**App version:** `CMakeLists.txt` → `project(FourKDowner VERSION …)` (single source for exe, packaging, MSI).

---

## 1. What it is

Desktop app (Windows 10/11 x64 + Linux x64; no macOS) that:

- Pastes video/playlist/channel URLs from clipboard
- Fetches metadata + thumbnails via yt-dlp (packaged as **ytdown**)
- Downloads selectable quality (incl. 2K/4K/8K when available)
- Optionally auto-converts with FFmpeg
- Has a separate Converter workspace for local files

| | |
|---|---|
| **UI** | Custom immediate-mode GUI on raylib (**not** Qt, **not** ImGui) |
| **Language** | C++17 |
| **Build** | CMake ≥ 3.20 |
| **License** | GNU GPL v3 (root `LICENSE`); third-party texts in `licenses/` |
| **Branding** | “by NickStan” |
| **Entry** | `src/main.cpp` → `Application::Run()` |

---

## 2. Repo layout (this git repo)

```text
src/
  main.cpp
  GUI/                 Almost all product code (~70+ files)
assets/
  fonts/               InterVariable.ttf
  logo/
  emoji/noto/          Bundled frequent Noto-style PNG emoji (~28 files)
  (no assets/emoji/twemoji — Twemoji is CDN-only alternate backend)
scripts/
  Windows/             main.ps1, build-portable.ps1, build-msi.ps1, msi/*.wxs
  Linux/               main.sh, build-portable.sh, setup-ytdown-portable.sh
cmake/
  PackagePortable.cmake, StagePortableFiles.cmake, shim/ (include-order shims)
licenses/              Third-party license texts
resources/             app.rc (Windows icon)
LICENSE                GPL-3 for the app
README.md
QuickGuide.md / .txt   This onboarding guide
.clang-format          SortIncludes: Never — keep Windows include order
build-windows/         Local Win build (gitignored)
build-linux/           Local Linux build (gitignored)
```

**Not present:** `tests/`, `.github/workflows` CI, vendored deps inside this repo.

---

## 3. Sibling `packages/` (required — outside this repo)

Expected layout under `Coding/`:

```text
Coding/
  4KDowner/              ← this repo
  packages/
    raylib/              REQUIRED (CMake FATAL_ERROR if missing)
    tinyfiledialogs/
    ffmpeg/bin/          ffmpeg + ffprobe (can hold both OS binaries)
    ytdown/              Win: python/python.exe + yt-dlp; Linux: venv or bin/
    nodejs/bin/          optional but needed for many YouTube JS challenges
  yCompiled/
    4KDownerCompiled/    packaging output (portable / zip / MSI)
```

- CMake: `PACKAGES_DIR = ${CMAKE_CURRENT_SOURCE_DIR}/../packages`
- Tool discovery walks CWD upward looking for `packages/...` (`ToolPaths.cpp`, `YtDlpLocator.cpp`, `YtDlpYouTube.cpp`)

Setup helpers:

- `scripts/Windows/setup-ytdown-portable.ps1`
- `scripts/Linux/setup-ytdown-portable.sh`

---

## 4. Mental model / architecture

Thin shell + fat coordinator + async workers + external CLIs:

```text
main.cpp
  └─ Application          window, fonts, emoji Pump(), frame loop
       └─ DockArea        ~10.5k LOC — ALL workspaces, queues, dialogs, undo
            ├─ DownloaderListItem → LinkCardNode | LinkCardGroupNode
            ├─ ConverterFileCardNode
            ├─ DownloadRunner[3] / ConvertRunner[3]   (max 3 parallel each)
            ├─ LinkInfoLoader / LinkGroupInfoLoader / ConverterInfoLoader
            ├─ DownloadFormatPredictor, YtDlpYouTube, BrowserDiagnostics
            ├─ UI widgets: Button, Checkbox, Dropdown, FoldoutPanel, PathField…
            ├─ CardChrome, EmojiText + IEmojiBackend
            └─ UndoStack, ShortcutRouter, TaskbarProgress
```

### Application (`Application.cpp` / `.h`)

- `SetWorkingDirectoryToExecutable()` so CWD = folder containing exe
- Window ~800×630 (min ~600×487), dark green clear
- Inter font + large Unicode ranges; footer 12px AA bake
- Default emoji backend: `EmojiBackendKind::NotoSprites`
- Each frame: `EmojiText::Pump()`, reap abandoned loaders, `dockArea` Update/Draw
- Windows: live-resize WndProc so UI redraws while dragging border

### DockArea (`DockArea.h` / `DockArea.cpp`)

- Two workspaces: **Downloader** | **Converter**
- Layout: header / left list (~60%) / right settings / footer
- Orchestrates queues, overwrite prompts, cancel confirms, About/Info dialogs

Include shims (`cmake/shim` + `*Include.h`) break circular includes between list items and group nodes — **do not casually reorder includes**.

---

## 5. Primary data flow

1. **Insert Link** → clipboard → `DownloaderListItem::MakeFromUrl`  
   - Group if playlist (`list=`) / channel (`/@`, `/channel/`) — `LinkInfoLoader` helpers

2. **Info load** (background):  
   - Single: `LinkInfoLoader` → yt-dlp `--skip-download`, thumbnail, formats → `LinkInfo`  
   - Group: `LinkGroupInfoLoader` → `--flat-playlist --dump-single-json`  
     Children materialize in pages (`kPageSize = 50`), load-more for large channels

3. **User options:** quality / container / media mode / path / Auto Convert  
   (global right pane + per-card overrides)

4. **Download** (`BuildDownloadRequestForCard` → `DownloadRunner`):  
   - Default path: `%USERPROFILE%/Videos/4kDowner` or `$HOME/Videos/4kDowner`  
   - If Auto Convert: stage under `Documents/4KDownerTemp` with `_downloaded` suffix; remember `finalOutputDirectory` as the user path  
   - Max 3 parallel downloads; soft preempt / prioritize in queue

5. **On complete + Auto Convert** → `ConvertRunner` (ffmpeg) → user folder;  
   staging cleaned via `WipeStagingFilesByStem` (only that title’s staging files, **not** the whole Temp tree, **not** `cache/emoji/`)

**Converter-only path:** Choose File (tinyfiledialogs) → `ConverterFileCardNode` + `ConverterInfoLoader` → `ConvertRunner` with global/custom codec options.

---

## 6. Paths (important)

### Exe-relative (after `SetWorkingDirectoryToExecutable`)

| Path | Role |
|---|---|
| `assets/…` | fonts, logo, emoji/noto |
| `packages/…` | when running portable (ffmpeg, ytdown, nodejs) |

### Documents (`GetDocuments4KDownerTempPath` in `WinAppPaths.cpp`)

| Path | Role |
|---|---|
| `Documents/4KDownerTemp/` | download staging for auto-convert |
| `Documents/4KDownerTemp/cache/emoji/noto/` | CDN emoji cache — created **only** when an emoji is actually downloaded; **not** at app startup |

### LocalAppData (other caches, not emoji)

- `%LOCALAPPDATA%/4KDowner/cache/link-info`
- `%LOCALAPPDATA%/4KDowner/converter-previews`
- `%LOCALAPPDATA%/4KDowner/logs/`

### Default download destination

`Videos/4kDowner` (under user profile)

---

## 7. Emoji system (hybrid)

**Files:** `IEmojiBackend.h`, `SpriteEmojiBackend.*`, `EmojiText.*`, `EmojiBackendFactory.cpp`

- **Default:** `NotoSprites` (RealityRipple Noto PNGs via jsDelivr)
- **Alternate:** Twemoji (CDN only; no bundled `assets/emoji/twemoji/`)

**Lookup order for a glyph:**

1. `assets/emoji/<pack>/` — bundled frequent set (ships in portable)
2. `Documents/…/cache/emoji/<pack>/` — already downloaded
3. CDN → save into cache (`mkdir` only then)

- Noto naming: often keeps FE0F (e.g. `2764-fe0f.png` for red heart)
- Twemoji naming: usually omits FE0F; `CandidateFilenames` tries both

**Do not** load Segoe/Noto as raylib fonts for color emoji — that yields gray silhouettes. Color comes from PNG sprites.

---

## 8. YouTube / quality / cookies

### `YtDlpYouTube.cpp`

- Cookies-from-browser retry: firefox → edge → chrome → vivaldi → opera (+ Opera GX on Win); remember preferred; fall back to no cookies
- JS runtimes: Node from `packages/nodejs` and/or Deno on PATH
- `player_client` args for high-res when needed (`android_vr`, `tv_downgraded`, …)

### `DownloadFormatPredictor`

- Parses format streams → quality ladder, estimates, predicted codecs
- Honesty: e.g. WEBM unavailable at 8K when only MP4 exists

Format-unavailable retries must **not** silently drop height caps (`BuildRelaxedFormatSelector` keeps quality filter).

---

## 9. Build & package

Do not invent new scripts unless asked.

### Local Windows

```bash
cmake -S . -B build-windows
cmake --build build-windows --config Release --target 4KDowner
# → build-windows/Release/4KDowner.exe
```

### Local Linux

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --target 4KDowner
```

### CMake targets

| Target | Output |
|---|---|
| `4KDowner` | main exe |
| `package-portable` | `../yCompiled/4KDownerCompiled/4KDowner-<ver>-{windows\|linux}-x64/` |
| `package-archive` | zip (Win) / tar.gz (Linux) |

Override portable output folder **without editing scripts**:

```powershell
scripts/Windows/build-portable.ps1 -PortableRoot "C:\path\to\folder"
# sets -D4KDOWNER_PACKAGE_DIR=…
```

### One-shot

- `scripts/Windows/main.ps1` — portable + zip + MSI
- `scripts/Linux/main.sh` — portable + tar.gz

Portable staging (`StagePortableFiles.cmake`) copies: `assets/`, `packages/ffmpeg` (native bins), `packages/ytdown`, `packages/nodejs` (native).

### Version (single source)

Change only `CMakeLists.txt` → `project(FourKDowner VERSION x.y.z)`. Everything else derives from it:

- `cmake/Version.h.in` → `build-*/generated/Version.h` → UI, logs, window title
- `resources/app.rc.in` → exe File Version (Windows)
- `cmake/PackagePortable.cmake` → portable/archive folder names
- `scripts/Windows/ProjectVersion.ps1`, `scripts/Linux/project-version.sh` → build scripts
- `scripts/Windows/build-msi.ps1` → `-d ProductVersion=…` → `4KDowner.wxs`

### clang-format

After editing any first-party `.cpp`/`.h` under `src/`, run clang-format (repo `.clang-format`). Prefer:

`C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe`

---

## 10. Notable product behaviors

- Playlist/channel group cards: stack chrome, expand, load-more, batch download
- Group Quality (Max / 8K / 4K / 2K) forces children; per-video can override until next group change
- Global + Custom Auto Convert
- Undo/redo for cards/options/paths/batch
- Taskbar progress (Win; optional DBus on Linux)
- Footer notifications; copyable browser/cookie diagnostic logs
- Optional keep download indices: `N. Title` for playlist children
- Dev seed footer buttons `+10` / `8K` (look ship-time — not behind `#ifdef`)

---

## 11. Risks / tech debt (for contributors)

- `DockArea.cpp` is a monolith (~10.5k LOC) — change carefully, small slices
- Sibling `packages/` required; clone alone will not build
- `nodejs` often missing → YouTube JS challenge failures
- No automated tests / CI
- Include-order shims are fragile
- Version duplicated across CMake + packager scripts
- MSI uses `WixUI_InstallDir_NoLicense` (no EULA page) while app is GPL — ensure `LICENSE`/`licenses` ship with distributions when releasing
- Process/cancel + cookies-from-browser are OS-sensitive (close Chromium browsers if cookie DB locked)

---

## 12. Agent / workflow hints

- Prefer reading this file + `README.md` before large refactors
- Do not edit plan markdown files unless the user asks
- Do not touch packaging scripts unless asked; use `-PortableRoot` / CMake `-D` instead
- Only commit when the user explicitly asks
- Format C/C++ with clang-format after edits
- On Win32 blocks: `windows.h` before `shellapi.h` / `shlobj.h` / `shobjidl.h`

### Quick landing for a new AI session

1. Read `QuickGuide.md` (or `QuickGuide.txt`)
2. Skim `Application.cpp` (lifecycle)
3. Skim `DockArea.h` (API surface)
4. Follow `DownloadRunner` + `LinkInfoLoader` for happy path
5. Treat `DockArea.cpp` as last place to edit, in small diffs
