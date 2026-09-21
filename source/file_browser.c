#include "file_browser.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#include <switch.h>

#include "sender.h"

#define BROWSER_VISIBLE_ROWS 18
#define BROWSER_NAME_WIDTH 32

static void clear_screen(void)
{
    printf("\x1b[2J");
    printf("\x1b[H");
}

static bool is_selected(
    FileBrowser *browser,
    const char *path)
{
    for (size_t i = 0;
         i < browser->selected_count;
         i++)
    {
        if (strcmp(
                browser->selected_paths[i],
                path) == 0)
        {
            return true;
        }
    }

    return false;
}

static void remove_selection(
    FileBrowser *browser,
    const char *path)
{
    for (size_t i = 0;
         i < browser->selected_count;
         i++)
    {
        if (strcmp(
                browser->selected_paths[i],
                path) == 0)
        {
            for (size_t j = i;
                 j + 1 < browser->selected_count;
                 j++)
            {
                strcpy(
                    browser->selected_paths[j],
                    browser->selected_paths[j + 1]
                );
            }

            browser->selected_count--;

            return;
        }
    }
}

static void add_selection(
    FileBrowser *browser,
    const char *path)
{
    if (browser->selected_count >=
        FILE_BROWSER_MAX_SELECTIONS)
    {
        return;
    }

    if (is_selected(browser, path))
        return;

    strncpy(
        browser->selected_paths[
            browser->selected_count
        ],
        path,
        FILE_BROWSER_PATH_MAX - 1
    );

    browser->selected_paths[
        browser->selected_count
    ][FILE_BROWSER_PATH_MAX - 1] = '\0';

    browser->selected_count++;
}

static bool load_directory(
    FileBrowser *browser)
{
    DIR *dir =
        opendir(browser->current_path);

    if (!dir)
        return false;

    browser->entry_count = 0;
    browser->cursor = 0;

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (browser->entry_count >=
            FILE_BROWSER_MAX_ENTRIES)
        {
            break;
        }

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        FileBrowserEntry *item =
            &browser->entries[
                browser->entry_count
            ];

        memset(
            item,
            0,
            sizeof(FileBrowserEntry)
        );

        snprintf(
            item->name,
            sizeof(item->name),
            "%s",
            entry->d_name
        );

        int written =
            snprintf(
                item->path,
                sizeof(item->path),
                "%s/%s",
                browser->current_path,
                entry->d_name
            );

        if (written < 0 ||
            (size_t)written >=
                sizeof(item->path))
        {
            continue;
        }

        item->directory =
            (entry->d_type == DT_DIR);

        item->selected =
            is_selected(
                browser,
                item->path
            );

        browser->entry_count++;
    }

    closedir(dir);

    /*
     * Directories first.
     */
    for (size_t i = 0;
         i < browser->entry_count;
         i++)
    {
        for (size_t j = i + 1;
             j < browser->entry_count;
             j++)
        {
            FileBrowserEntry *a =
                &browser->entries[i];

            FileBrowserEntry *b =
                &browser->entries[j];

            if (!a->directory &&
                b->directory)
            {
                FileBrowserEntry temp =
                    *a;

                *a = *b;
                *b = temp;
            }
        }
    }

    return true;
}

static void draw_browser(
    FileBrowser *browser)
{
    clear_screen();

    printf(
        "========================================\n"
    );

    printf(
        "              LocalS-NX\n"
    );

    printf(
        "========================================\n"
    );

    printf(
        "%s\n",
        browser->current_path
    );

    printf(
        "----------------------------------------\n"
    );

    if (browser->entry_count == 0)
    {
        printf(
            "(empty)\n"
        );
    }
    else
    {
        /*
         * Keep the cursor inside the visible
         * portion of the directory.
         */
        size_t start = 0;

        if (browser->cursor >=
            BROWSER_VISIBLE_ROWS)
        {
            start =
                browser->cursor -
                BROWSER_VISIBLE_ROWS + 1;
        }

        size_t end =
            start + BROWSER_VISIBLE_ROWS;

        if (end > browser->entry_count)
            end = browser->entry_count;

        for (size_t i = start;
             i < end;
             i++)
        {
            FileBrowserEntry *item =
                &browser->entries[i];

            char display_name[
                BROWSER_NAME_WIDTH + 1
            ];

            snprintf(
                display_name,
                sizeof(display_name),
                "%.32s",
                item->name
            );

            const char *cursor =
                (i == browser->cursor)
                    ? ">"
                    : " ";

            const char *selected =
                item->selected
                    ? "[X]"
                    : "[ ]";

            const char *type =
                item->directory
                    ? "<DIR>"
                    : "     ";

            printf(
                "%s %s %s %-32s\n",
                cursor,
                selected,
                type,
                display_name
            );
        }
    }

    printf(
        "----------------------------------------\n"
    );

    printf(
        "Selected: %zu\n",
        browser->selected_count
    );

    printf(
        "A Open   B Back   X Select\n"
    );

    printf(
        "Y Send   + Exit\n"
    );

    consoleUpdate(NULL);
}

static bool enter_directory(
    FileBrowser *browser,
    const char *name)
{
    char old_path[
        FILE_BROWSER_PATH_MAX
    ];

    char new_path[
        FILE_BROWSER_PATH_MAX
    ];

    snprintf(
        old_path,
        sizeof(old_path),
        "%s",
        browser->current_path
    );

    int written;

    if (strcmp(
            browser->current_path,
            "sdmc:/") == 0)
    {
        written =
            snprintf(
                new_path,
                sizeof(new_path),
                "sdmc:/%s",
                name
            );
    }
    else
    {
        written =
            snprintf(
                new_path,
                sizeof(new_path),
                "%s/%s",
                browser->current_path,
                name
            );
    }

    if (written < 0 ||
        (size_t)written >=
            sizeof(new_path))
    {
        return false;
    }

    strcpy(
        browser->current_path,
        new_path
    );

    if (!load_directory(browser))
    {
        strcpy(
            browser->current_path,
            old_path
        );

        load_directory(browser);

        return false;
    }

    return true;
}

static void go_back(
    FileBrowser *browser)
{
    if (strcmp(
            browser->current_path,
            "sdmc:/") == 0)
    {
        return;
    }

    char *last =
        strrchr(
            browser->current_path,
            '/'
        );

    if (!last)
        return;

    if (last ==
        browser->current_path + 5)
    {
        browser->current_path[6] = '\0';
    }
    else
    {
        *last = '\0';
    }

    load_directory(browser);
}

void file_browser_init(
    FileBrowser *browser)
{
    memset(
        browser,
        0,
        sizeof(FileBrowser)
    );

    snprintf(
        browser->current_path,
        sizeof(browser->current_path),
        "sdmc:/"
    );

    load_directory(browser);
}

bool file_browser_run(
    FileBrowser *browser,
    int server
)
{
    PadState pad;

    padInitializeDefault(
        &pad
    );

    load_directory(browser);

    /*
     * Wait until Y, which opened the browser,
     * has been released.
     */
    while (appletMainLoop())
    {
        padUpdate(&pad);

        if (!(padGetButtons(&pad) &
              HidNpadButton_Y))
        {
            break;
        }

        svcSleepThread(
            10000000ULL
        );
    }

    draw_browser(browser);

    while (appletMainLoop())
    {
        padUpdate(&pad);

        u64 down =
            padGetButtonsDown(&pad);

        if (down &
            HidNpadButton_Plus)
        {
            return false;
        }

        if (down &
            HidNpadButton_Up)
        {
            if (browser->entry_count > 0)
            {
                if (browser->cursor == 0)
                {
                    browser->cursor =
                        browser->entry_count - 1;
                }
                else
                {
                    browser->cursor--;
                }

                draw_browser(browser);
            }
        }

        if (down &
            HidNpadButton_Down)
        {
            if (browser->entry_count > 0)
            {
                browser->cursor++;

                if (browser->cursor >=
                    browser->entry_count)
                {
                    browser->cursor = 0;
                }

                draw_browser(browser);
            }
        }

        if (down &
            HidNpadButton_A)
        {
            if (browser->entry_count > 0)
            {
                FileBrowserEntry *item =
                    &browser->entries[
                        browser->cursor
                    ];

                if (item->directory)
                {
                    enter_directory(
                        browser,
                        item->name
                    );

                    draw_browser(browser);
                }
            }
        }

        if (down &
            HidNpadButton_B)
        {
            go_back(browser);

            draw_browser(browser);
        }

        if (down &
            HidNpadButton_X)
        {
            if (browser->entry_count > 0)
            {
                FileBrowserEntry *item =
                    &browser->entries[
                        browser->cursor
                    ];

                if (!item->directory)
                {
                    if (is_selected(
                            browser,
                            item->path))
                    {
                        remove_selection(
                            browser,
                            item->path
                        );
                    }
                    else
                    {
                        add_selection(
                            browser,
                            item->path
                        );
                    }

                    item->selected =
                        is_selected(
                            browser,
                            item->path
                        );

                    draw_browser(browser);
                }
            }
        }

if (down &
    HidNpadButton_Y)
{
    if (browser->selected_count > 0)
    {
        int destination =
            sender_choose_device(
                server
            );

if (destination >= 0)
{
    const SenderDevice *device =
        sender_get_device(
            (size_t)destination
        );

    const char *paths[
        FILE_BROWSER_MAX_SELECTIONS
    ];

    for (size_t i = 0;
         i < browser->selected_count;
         i++)
    {
        paths[i] =
            browser->selected_paths[i];
    }

    sender_send_files(
        device,
        paths,
        browser->selected_count
    );

    while (appletMainLoop())
    {
        padUpdate(&pad);

        u64 send_down =
            padGetButtonsDown(&pad);

        if (send_down &
            HidNpadButton_B)
        {
            break;
        }

        svcSleepThread(
            10000000ULL
        );
    }

    draw_browser(browser);
}
else
{
    draw_browser(browser);
}
    }
}

        svcSleepThread(
            1000000ULL
        );
    }

    return false;
}