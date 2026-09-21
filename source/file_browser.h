#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <stdbool.h>
#include <stddef.h>

#define FILE_BROWSER_MAX_ENTRIES 256
#define FILE_BROWSER_MAX_SELECTIONS 256
#define FILE_BROWSER_PATH_MAX 1024

typedef struct
{
    char name[256];
    char path[FILE_BROWSER_PATH_MAX];

    bool directory;
    bool selected;

} FileBrowserEntry;


typedef struct
{
    FileBrowserEntry entries[
        FILE_BROWSER_MAX_ENTRIES
    ];

    size_t entry_count;
    size_t cursor;

    char current_path[
        FILE_BROWSER_PATH_MAX
    ];

    size_t selected_count;

    char selected_paths[
        FILE_BROWSER_MAX_SELECTIONS
    ][FILE_BROWSER_PATH_MAX];

} FileBrowser;


/*
 * Run the file browser.
 *
 * server is the LocalSend HTTP server
 * socket used while discovering a destination.
 */
bool file_browser_run(
    FileBrowser *browser,
    int server
);


/*
 * Initialize the browser at sdmc:/.
 */
void file_browser_init(
    FileBrowser *browser
);

#endif