<h1 align="center">cfiles</h1>

<p align="center">
<a href="https://github.com/mananapr/cfiles/releases/latest"><img src="https://img.shields.io/github/release/mananapr/cfiles/all.svg" alt="Latest release" /></a>
<a href="https://aur.archlinux.org/packages/cfiles/"><img src="https://img.shields.io/aur/version/cfiles.svg" alt="Arch Linux" /></a>
<a href="https://github.com/mananapr/homebrew-cfiles"><img src="https://img.shields.io/badge/homebrew-v1.8-blue.svg" alt="Homebrew" /></a>
</p>

<p align="center">
<a href="https://github.com/mananapr/cfiles/blob/master/LICENSE"><img src="https://img.shields.io/badge/license-MIT-yellow.svg" alt="License" /></a>
</p>

`cfiles` is a terminal file manager with vim like keybindings, written in C using the ncurses
library. It aims to provide an interface like [ranger](https://github.com/ranger/ranger) while being lightweight, fast and
minimal.

![screenshot](cf.png)

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
`cfl` uses `$XDG_CONFIG_HOME/cfiles` directory to store the clipboard file. This is used so that the clipboard
can be shared between multiple instances of `cfiles`. That's why I won't be adding tabs in `cfiles` because multiple
instances can be openend and managed by any terminal multiplexer or your window manager.
Note that this also means the selection list will persist even if all instances are closed.

`cfl` also uses `$HOME/.local/share/Trash/files` as the Trash Directory, so it will be created a installation process

For storing bookmarks, `cfl` uses `$XDG_CONFIG_HOME/cfiles/bookmarks` file. Bookmarks are stored in the form `<key>:<path>`. You can either edit this file directly
or press `m` in `cfl` to add new bookmarks.

`cfl` looks for external scripts in the `$XDG_CONFIG_HOME/cfiles/scripts` directory. Make sure the scripts are executable before moving them to the scripts directory.

If `$XDG_CONFIG_HOME` is not set, then `$HOME/.config` is used.

## Opening Files
You can set `FILE_OPENER` in `config.h` to specify your file opening program. It is set to use `xdg-open` by default but you can change it to anything like `thunar`.

## Image Previews
`cfl` uses `Überzug` ([link](https://github.com/seebye/ueberzug)) for image previews.

1. **Überzug**

To use `Überzug` for image previews, set `DISPLAYIMG` and `CLEARIMG` in `config.h` to the paths of `displayimg_uberzug` and `clearimg_uberzug` scripts respectively.
  * Pros
    1. Better previews when compared to `w3mimgdisplay`
  * Cons
    1. Can't generate previews for mp3 album arts
    2. Non functional scrolling with arrow keys
