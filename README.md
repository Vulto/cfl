
## Dependencies
- `ncurses`
- `mediainfo` for viewing media info and file sizes

## Compiling and Installation

    
    ./c -cli  
    Explained command: ./c runs the build system
                        -c Compile
                        -l Link 
                        -i Install

## Keybindings
| Key | Function |
|:---:| --- |
| <kbd> ? </kbd> | List all Keybindings |

## Directories Used
`cfl` uses `$XDG_CONFIG_HOME/cfl` directory to store the clipboard file. This is used so that the clipboard
can be shared between multiple instances of `cfl`. 
Multiple instances can be opened and managed by any terminal multiplexer or your window manager.
Note that this also means the selection list will persist even if all instances are closed.

`cfl` also uses `$HOME/.local/share/Trash/files` as the Trash Directory, so it will be created a installation process

For storing bookmarks, `cfl` uses `$XDG_CONFIG_HOME/cfl/bookmarks` file. Bookmarks are stored in the form `<key>:<path>`. You can either edit this file directly
or press `m` in `cfl` to add new bookmarks.

`cfl` looks for external scripts in the `$XDG_CONFIG_HOME/cfl/scripts` directory. Make sure the scripts are executable before moving them to the scripts directory.

If `$XDG_CONFIG_HOME` is not set, then `$HOME/.config` is used.

## Opening Files
You can set `FILE_OPENER` in `config.h` to specify your file opening program. It is set to use `xdg-open` by default but you can change it to anything.

## Image Previews (experimental)
(NOTE: For image preview, download the image_preview branch.)
`cfl` uses `Überzugpp` ([link](https://github.com/jstkdng/ueberzugpp)) for image previews.
