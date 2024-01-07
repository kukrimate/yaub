#pragma once
#include <efi.h>

// Defines the type a menu entry can have
typedef enum {
    menu_type_info,
    menu_type_subscreen,
    menu_type_exec,
    menu_type_exit
} menu_entry_type;

// Forward declaration for a menu screen
typedef struct menu_screen menu_screen;

// Represents a menu entry
typedef struct {
    menu_entry_type type;
    efi_ch16_t      *text;
    union {
        /* menu_type_subscreen */
        struct {
            menu_screen *subscreen;
        };
        /* menu_type_exec */
        struct {
            efi_ch16_t *path;
            efi_ch16_t *flags;
        };
    };
} menu_entry;

// Represents a menu scren
struct menu_screen {
    efi_ch16_t  *title;
    efi_ssize_t timeout;
    efi_size_t  selected_entry;
    efi_size_t  entry_count;
    menu_entry  entries[];
};

/* Add an entry to a menu */
void menu_add_entries(menu_screen **menu, menu_entry *entry, efi_size_t cnt);

/* Wait for a keypress */
void menu_wait_for_key(efi_in_key_t *key);

//
// Initialize the menu handling code
//
void menu_init();

//
// De-initialize menu handling code
//
void menu_fini();

//
// Clear the screen
//
void menu_clearscreen();

//
// Run a menu
//
menu_entry *menu_run(menu_screen *screen);
