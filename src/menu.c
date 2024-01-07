#include <efi.h>
#include <efiutil.h>
#include "menu.h"

// Menu colors
#define DEFAULT_COLOR  EFI_LIGHTGRAY | EFI_BACKGROUND_BLACK
#define SELECTED_COLOR EFI_BLACK     | EFI_BACKGROUND_LIGHTGRAY

// Banner height
#define BANNER_HEIGHT 3

// Menu globals
static efi_size_t cols, rows;
static efi_event_t timer_event;

void menu_add_entries(menu_screen **menu, menu_entry *entry, efi_size_t cnt)
{
    efi_size_t oldsize;

    oldsize = sizeof(menu_screen) +
        sizeof(menu_entry) * (*menu)->entry_count;
    *menu = efi_realloc(*menu, oldsize, oldsize + sizeof(menu_entry) * cnt);
    memcpy((*menu)->entries + (*menu)->entry_count,
        entry, sizeof(menu_entry) * cnt);
    (*menu)->entry_count += cnt;
}

void menu_wait_for_key(efi_in_key_t *key)
{
    efi_size_t index;

    efi_bs->wait_for_event(1, &efi_st->con_in->wait_for_key, &index);
    efi_st->con_in->read_key(efi_st->con_in, key);
}

void menu_init()
{
    efi_status_t status;

    status = efi_st->con_out->query_mode(efi_st->con_out,
       efi_st->con_out->mode->mode, &cols, &rows);
    status = efi_st->con_out->enable_cursor(efi_st->con_out, false);
    status = efi_bs->create_event(EVT_TIMER,
        TPL_CALLBACK, NULL, NULL, &timer_event);
    if (EFI_ERROR(status))
        efi_abort(L"Failed to create timer event!", EFI_ABORTED);
}

void menu_fini()
{
    efi_status_t status = efi_bs->close_event(timer_event);
    if (EFI_ERROR(status))
        efi_abort(L"Failed to close timer event!", EFI_ABORTED);
}

void menu_clearscreen()
{
    efi_st->con_out->set_attr(efi_st->con_out, DEFAULT_COLOR);
    efi_st->con_out->clear_screen(efi_st->con_out);
}

static void menu_draw_banner(menu_screen *screen)
{
    efi_st->con_out->set_attr(efi_st->con_out, SELECTED_COLOR);
    for (efi_size_t i = 0; i < BANNER_HEIGHT; ++i) {
        efi_st->con_out->set_cursor_pos(efi_st->con_out, 0, i);
        for (efi_size_t j = 0; j < cols; ++j) {
            efi_st->con_out->output_string(efi_st->con_out, L" ");
        }
    }
    efi_st->con_out->set_cursor_pos(efi_st->con_out, 0, BANNER_HEIGHT / 2);
    if (screen->timeout >= 0) {
        efi_print(L"Booting '%s' in %ds...", screen->entries[screen->selected_entry].text, screen->timeout);
    } else {
        efi_st->con_out->output_string(efi_st->con_out, screen->title);
    }
    efi_st->con_out->set_attr(efi_st->con_out, DEFAULT_COLOR);
}

static void menu_draw_entries(menu_screen *screen)
{
    for (efi_size_t i = 0; i < screen->entry_count; ++i) {
        if (i == screen->selected_entry) {
            efi_st->con_out->set_attr(efi_st->con_out, SELECTED_COLOR);
        } else {
            efi_st->con_out->set_attr(efi_st->con_out, DEFAULT_COLOR);
        }

        efi_st->con_out->set_cursor_pos(efi_st->con_out, 0, i + 3);
        efi_st->con_out->output_string(efi_st->con_out, screen->entries[i].text);
    }
}

//
// Convert seconds to 100ns
//
#define SEC_TO_100NS(secs) (secs) * 10000000

enum {
    EVENT_TICK,
    EVENT_UP,
    EVENT_DOWN,
    EVENT_SELECT,
    EVENT_UNK
};

//
// Wait for a timer or keypress
//
static int wait_for_event(void)
{
    efi_event_t events[2] = { timer_event, efi_st->con_in->wait_for_key };
    efi_size_t index;
    efi_in_key_t key;

    // Wait for event
    efi_bs->wait_for_event(ARRAY_SIZE(events), events, &index);

    // Timer
    if (index == 0)
        return EVENT_TICK;

    // Key
    efi_st->con_in->read_key(efi_st->con_in, &key);
    switch (key.scan) {
    case EFI_SCAN_UP:
        return EVENT_UP;
    case EFI_SCAN_DOWN:
        return EVENT_DOWN;
    case EFI_SCAN_NULL:
        if (key.c == L'\r' || key.c == L'\n' || key.c == L' ')
            return EVENT_SELECT;
    }

    return EVENT_UNK;
}

menu_entry *menu_run(menu_screen *screen)
{
    efi_status_t status;
    menu_entry *submenu_entry;

    if (screen->timeout == 0)
        goto event_expire;

    for (;;) {
        // Draw menu
        menu_clearscreen();
        menu_draw_banner(screen);
        menu_draw_entries(screen);

        // Set timer
        if (screen->timeout > 0) {
            status = efi_bs->set_timer(timer_event, EFI_TIMER_RELATIVE, SEC_TO_100NS(1));
            if (status != EFI_SUCCESS)
                efi_abort(L"Failed to set timer!", EFI_ABORTED);
        }

        // Wait for event
        int event = wait_for_event();

        // Disable timeout on any keypress
        if (event != EVENT_TICK) {
            screen->timeout = -1;
        }

        switch (event) {
        case EVENT_TICK:
            if (--screen->timeout == 0) {
event_expire:
                // Disable timeout
                screen->timeout = -1;
                // Select entry
                goto event_select;
            }
            break;
        case EVENT_UP:
            if (screen->selected_entry > 0)
                --screen->selected_entry;
            break;
        case EVENT_DOWN:
            if (screen->selected_entry < screen->entry_count - 1)
                ++screen->selected_entry;
            break;
        case EVENT_SELECT:
event_select:
            switch (screen->entries[screen->selected_entry].type) {
            case menu_type_subscreen:
                // Display screen for submenu entry
                submenu_entry =
                    menu_run(screen->entries[screen->selected_entry].subscreen);

                switch (submenu_entry->type) {
                case menu_type_exit:
                    // Continue current menu, if the submenu was exited
                    break;
                default:
                    // Return the entry if something was choose from the submenu
                    return submenu_entry;
                }
                break;
            case menu_type_info:
                // Do nothing on info entries
                break;
            default:
                // Return selected entry
                return screen->entries + screen->selected_entry;
            }
        }
    }
}
