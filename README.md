# cfl (mcfl branch)

Dual-pane mmenu file manager. No classic multi-mode browser.

## Browse first; Jump on Ctrl+J

Starts in **Browse** (filterable list of the current directory).

| Key | Action |
|-----|--------|
| type | Filter (`q` is text — does not quit) |
| **↑ / ↓** | Move selection (clears op-log preview) |
| **→** / **Enter** | Browse/Jump: open directory or file |
| **←** | Parent directory (Browse); parent row in Jump / dest lists |
| **Tab** | Multi-mark |
| **Del** | Trash marks (or hover) → FreeDesktop Trash |
| **Ctrl+C** | marks → clipboard → **Copy to** dest menu |
| **Ctrl+X** | marks → clipboard → **Move to** dest menu |
| **Ctrl+R** | Rename hover; if **2+** marks, bulk rename in `$EDITOR`/vim |
| **Ctrl+Z** | Undo last trash / copy / move / rename batch |
| **Ctrl+A** | Select all files in current directory |
| **Ctrl+Shift+A** | Invert selection (none marked → select all) — **st needs config** |
| **Ctrl+.** | Toggle hidden files — **st needs config** |
| **Ctrl+S** | Run script from `~/.config/cfl/scripts` on marks/hover |
| **Ctrl+J** / **`cfl -j`** | Jump — recursive dirs under **`$HOME`** (`JUMP_OMIT_DIRS`) |
| **F1** | Help (blocks other ops; Esc/F1 close) |
| **Enter** (in dest) | Confirm copy/move into selected directory |
| **Esc** | Close mode / help; Browse: quit cfl **with** shell cd-on-quit |
| **Ctrl+Q** | Quit cfl immediately from **any** mode — **no** shell cd-on-quit |

After trash/copy/move/rename/undo/script, raw status text is shown in the **preview** until you move the cursor.

### st (suckless terminal)

Stock **st** cannot send distinct bytes for several chords:

| Chord | On stock st | What to do |
|-------|-------------|------------|
| **Ctrl+M** | Same as **Enter** (`\r`) | Cannot disable without breaking Enter |
| **Ctrl+[** | Same as **Esc** (`0x1B`) | Same code; both close/quit as Esc |
| **Ctrl+Shift+A** | Usually same as Ctrl+A | Add `ttysend` in `config.h` (below) |
| **Ctrl+.** | Often not sent | Add `ttysend` in `config.h` (below) |

Add to st `config.h` `shortcuts[]` (then recompile st):

```c
/* cfl: Ctrl+Shift+A → invert selection */
{ ControlMask|ShiftMask, XK_a, ttysend, {.s = "\033[27;6;97~"} },
{ ControlMask|ShiftMask, XK_A, ttysend, {.s = "\033[27;6;65~"} },
/* cfl: Ctrl+. → toggle hidden */
{ ControlMask,           XK_period, ttysend, {.s = "\033[27;5;46~"} },
```

## Scripts

Put executables in:

- `$XDG_CONFIG_HOME/cfl/scripts`, or
- `~/.config/cfl/scripts`

**Ctrl+S** lists them; Enter runs `script path1 path2 …` (abs paths). Output appears in the preview op-log.

## Build / install

```sh
./c -cli          # compile + link + install
./c -c            # compile
./c -l            # link
./c -w            # wipe binary/objects
```

Uses `$CC` (default `cc`), `$PREFIX` (default `/usr/local/bin`), `$SUDO` (default `sudo`).

```sh
cfl              # Browse cwd
cfl -j           # start in Jump ($HOME recursive)
cfl -j ~/foo     # optional path, then Jump
```

## Layout

| File | Role |
|------|------|
| `main.c` | entry, globals, init / cleanup |
| `config.h` | keys, omit list |
| `ui.h` | layout, status, path helpers |
| `preview.h` | previews / ueberzug |
| `search.h` | filter + marked list |
| `mjump.h` | transfer + Browse / Jump / dest |
| `c` | build script |

Branch: **`mcfl`**.
