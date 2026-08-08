# cfl (mcfl branch)

Dual-pane mmenu file manager. No classic multi-mode browser.

## Browse first; Jump on Ctrl+J

Starts in **Browse** (filterable list of the current directory).

| Key | Action |
|-----|--------|
| type | Filter (`q` is text — never quits) |
| **Tab** | Multi-mark |
| **Enter** | Open directory / file |
| **Esc** | Parent directory |
| **Ctrl+J** / **`cfl -j`** | Jump — recursive dirs under **`$HOME`** (`JUMP_OMIT_DIRS`) |
| **Ctrl+V** | marks → clipboard → **copy-to** dest |
| **Ctrl+X** | marks → clipboard → **move-to** dest |
| **Ctrl+Q** | Quit |

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
| `jump_to.h` | transfer + Browse / Jump / dest |
| `c` | build script |

Branch: **`mcfl`**.
