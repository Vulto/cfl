//config.h

#define CONFIRM_ON_DEL false 

// Set to 1 if you want to see hidden files on startup
#define SHOW_HIDDEN false

// Set to 0 if you don't want to see number of selected files in the statusbar
#define SHOW_SELECTION_COUNT true

// Set to 0 if you don't want to see PDF Previews (previews are broken)
#define SHOW_PDF_PREVIEWS false

// Display Image Script
#define DISPLAYIMG "/home/vulto/.config/cfl/scripts/"

// Clear Image Preview Script
#define CLEARIMG "/home/vulto/.config/cfl/scripts/"

// Program used to open non-text file (Eg: `xdg-open` or `thunar`)
#define FILE_OPENER "nopen"

// Go to parent directory
#define KEY_NAVBACK 'h'

// Go down
#define KEY_NAVDOWN 'j'

// Go up
#define KEY_NAVUP 'k'

// Go to next directory / Open file
#define KEY_NAVNEXT 'l'

// Go to the beginning of the dir
#define KEY_START 'g'

// Go to the end of the directory
#define KEY_GOEND 'G'

// Go to the top of the current view
#define KEY_TOP 'T'

// Go to the middle of the current view
#define KEY_MID 'M'

// Go to the bottom of the current view
#define KEY_BOTTOM 'B'

// Search all files with current directory as base
#define KEY_SEARCHALL '/'

// Search all files in the current directory
#define KEY_SEARCHDIR 's'

// Add files to selection list
#define KEY_SEL 'V'

// View all the selected files
#define KEY_VIEWSEL '\t' //TAB

// Edit the clippboard file
#define KEY_EDITSEL 'e'

// Empty the selection
#define KEY_EMPTYSEL 'c'

// Copy files in selection list to the current directory
#define KEY_PASTE 'p'

// Move files in selection list to the current directory
#define KEY_MV 'm'

// Rename files in selection list
#define KEY_RENAME 'R'

// For getting an option to either move the file to trash or delete it
#define KEY_REMOVEMENU 'd'

// For moving the file to trash after pressing KEY_REMOVEMENU
#define KEY_GARBAGE 'g'

// For removing the file after pressing KEY_REMOVEMENU
#define KEY_DELETE 'd'

// View file info
#define KEY_INFO 'i'

// Toggle Hidden Files
#define KEY_TOGGLEHIDE '.'

// Open Bookmark list
#define KEY_BOOKMARK '='

// Add Bookmark
#define KEY_ADDBOOKMARK '+'

// Remove Bookmark 
#define KEY_RMBOOKMARK '-'

// External Scripts Key
#define KEY_SCRIPT '#'

#define KEY_SHELL '!'

// View preview
#define KEY_PREVIEW '['

// Reload
#define KEY_UPDATE 'u'

// Quit
#define KEY_ESCAPE 'q'
