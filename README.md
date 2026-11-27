# cfl

A dual-pane terminal file manager written in C with ncurses. Browse directories,
preview files (text, hex, images via Überzugpp), multi-select, copy/move/delete,
bookmarks, FreeDesktop trash, and directory pickers for jump / move / copy.

## Dependencies

| Package | Purpose |
|---------|---------|
| **ncursesw** | TUI (wide-character support) |
| **libmagic** | MIME detection for previews |
| **mediainfo** (optional) | Image/video details in file-info mode |
| **ueberzugpp** (optional) | Image previews in the right pane |
| **cp**, **rm**, **sudo** | File operations (system tools) |

## Build and install

```sh
./c -cli
# -c  compile
# -l  link
# -i  install binary, man page, and cfl.sh
```

Installs (with `-i`):

- binary → `/usr/local/bin/cfl`
- man page → `/usr/share/man/man1/cfl.1`
- shell helper → `/usr/share/cfl/cfl.sh`

## Quick start

```sh
cfl                  # open current directory
cfl /path/to/dir     # open a directory
man cfl              # full manual
```

In-app help: press **`?`**.

## Features (recent)

### Selection
- **`v`** — toggle select current file (dark gray bar, full row width)
- **`V`** — select all files in the current directory
- **`c`** — clear selection
- Selection is **session-local** (`clipboard.<pid>`); not shared across instances; cleared on quit and after copy/paste/delete/move/trash

### Search and bulk delete
- **`/`** — fuzzy search in the current directory (list + live preview)
- **`D`** — delete-search mode: filter, **Tab** to mark (dark red), right panel shows deletion details, **Enter** deletes with a confirmation dialog listing every path

### Jump / Move / Copy destination pickers
Scan under `/home`, `/etc`, `/usr` (errors and `JUMP_OMIT_DIRS` skipped; results rank **shorter paths first**):

| Key | Mode |
|-----|------|
| **`J`** | Jump to directory |
| **`t`** | Move selection to chosen directory |
| **`C`** | Copy selection to chosen directory |
| **`m`** | Move selection into the **current** directory |
| **`p`** | Paste (copy) selection into the **current** directory |

Omit list is configurable in `config.h` (`JUMP_OMIT_DIRS`, e.g. `.pki`, `usr`, `.local`).

### File info
- **`i`** — toggle file-info mode: right pane shows metadata instead of previews; stops Überzug
- Status shows **`[INFO]`** while active
- Images: resolution and proportion (via mediainfo when available)
- Videos: resolution, proportion, FPS, codec, duration, size

### Tabs
Four in-process tabs, in the same spirit as nnn's contexts (not separate processes). Each tab remembers its directory, cursor, list scroll, hidden-files toggle, and whether file-info mode is on. The selection clipboard is shared, so you can mark files in one tab and paste them in another. Switching tabs clears the Überzug image and draws the new tab's preview.

### Large directories
Opening a directory reads it once and keeps the sorted list until it changes. Scrolling only draws the rows on screen, so moving around a million files costs the same as moving around a small folder. `V` marks every name in one pass. Search ranks matches when the list is short and otherwise keeps directory order, so typing in a huge folder does not sort a million rows on every key.

### Image previews
- One long-lived Überzugpp process. While keys are repeating, cfl moves the cursor and does not send a decode per file; the preview is drawn about 50 ms after the last key (`PREVIEW_IDLE_MS` in `config.h`)
- Directory listings are cached until the directory changes. File-vs-directory is taken from `d_type` (one `stat` only when the type is unknown) instead of stating every name on every comparison
- libmagic is loaded once. Recognized image headers skip MIME detection
- Large images are downscaled in the background into `$XDG_CACHE_HOME/cfl/thumbs` (or `~/.cache/cfl/thumbs`) with `ffmpeg` or ImageMagick when that tool is installed. The next visit of that file sends the thumbnail. `CFL_NO_THUMB=1` disables it
- **Smart clear**: when browsing images, only clear the previous layer if the new image is **smaller** or dimensions are unknown; equal/larger images splash without a full clear

### FreeDesktop Trash
- **`d`** then **`g`** — trash selection (not a raw dump folder)
- Location: `$XDG_DATA_HOME/Trash` (default `~/.local/share/Trash`)
- Layout: `files/` + `info/*.trashinfo` (`Path=` URL-encoded, `DeletionDate=`)
- Cross-device: `cp -a` then `rm` when `rename` fails with `EXDEV`
- **`d`** then **`d`** — permanent delete

### Quit into last directory
A child process cannot change the parent shell CWD. Use the helper:

```sh
# after install
echo 'source /usr/share/cfl/cfl.sh' >> ~/.bashrc   # or ~/.zshrc
```

Then run `cfl` and press **`q`**: the shell function `cd`s to the last browsed path (file: `$XDG_CONFIG_HOME/cfl/lastdir` or `$CFL_LASTDIR`).

## Key bindings (summary)

| Key | Action |
|:---:|--------|
| `h` `j` `k` `l` | Parent / down / up / open |
| `1` `2` `3` `4` | Switch tab. A new tab opens in the current directory and keeps its own cursor, scroll, and hidden-files state. The status line shows the four tabs (reverse = current, underline = open). |
| `g` `G` `T` `M` `B` | Start / end / top / mid / bottom of list |
| `/` | Fuzzy search |
| `J` | Jump to… |
| `D` | Delete-search |
| `v` `V` | Toggle select / select all |
| `c` | Clear selection |
| `p` `m` | Paste / move into **current** dir |
| `t` `C` | Move to… / Copy to… |
| `r` | Rename selection |
| `d` | Delete menu (`g` trash, `d` permanent) |
| `i` | Toggle file info panel |
| `.` | Toggle hidden files |
| `=` `+` `-` | Bookmarks open / add / remove |
| `!` | Shell in current directory |
| `K` `[` `]` | Preview scroll / page |
| `n` | Create directory |
| `u` | Reload |
| `?` | Help |
| `q` | Quit (writes lastdir) |

## Configuration

Edit **`config.h`** then recompile:

- Key bindings (`KEY_*`)
- `FILE_OPENER`, `SHOW_HIDDEN`, `SHOW_SELECTION_COUNT`
- `CONFIRM_ON_DEL`
- `JUMP_OMIT_DIRS` for jump/move/copy pickers
- Überzugpp binary/output/identifier

## Paths and files

| Path | Role |
|------|------|
| `$XDG_CONFIG_HOME/cfl/bookmarks` | Bookmarks (`key:path`) |
| `$XDG_CONFIG_HOME/cfl/clipboard.<pid>` | Session selection |
| `$XDG_CONFIG_HOME/cfl/lastdir` | Last dir on quit (for `cfl.sh`) |
| `$XDG_CONFIG_HOME/cfl/scripts` | External scripts |
| `$XDG_DATA_HOME/Trash` | FreeDesktop trash |
| `$XDG_CACHE_HOME/cfl/thumbs` | Downscaled image previews (or `~/.cache/cfl/thumbs`) |

If `$XDG_CONFIG_HOME` is unset, `$HOME/.config` is used. If `$XDG_DATA_HOME` is unset, `$HOME/.local/share` is used.

## Manual page

```sh
man cfl
```

Source: `cfl.1` (installed with `./c -cli`).

## License

See `LICENSE` (GPL as documented in the man page).

## Author

Jeffeson Sá — jeff.ssa@proton.me  
https://github.com/Vulto/cfl
