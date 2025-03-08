
## Dependencies
- `ncursesw`
- `mediainfo` for viewing media info and file sizes
- `poppler`(specifically `pdftoppm`) for pdf previews

## Compiling and Installation

    
    ./c -cli  
    Explained command: ./c runs the build system
                        -c Creates objects
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

## Image Previews
`cfl` uses `Überzug` ([link](https://github.com/seebye/ueberzug)) for image previews.

1. **Überzug**

To use `Überzug` for image previews, set `DISPLAYIMG` and `CLEARIMG` in `config.h` to the paths of `displayimg_uberzug` and `clearimg_uberzug` scripts respectively.
  * Pros
    1. Better previews when compared to `w3mimgdisplay`
  * Cons
    1. Can't generate previews for mp3 album arts
    2. Non functional scrolling with arrow keys
