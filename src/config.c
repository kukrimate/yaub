/*
 * config.c
 *
 *  Created on: Jan 28, 2017
 *      Author: kmate
 */
#include <efi.h>
#include <efiutil.h>
#include "menu.h"
#include "config.h"

//
// Convert an ASCII string to an EFI string
//
static efi_ch16_t *ascii_str_to_efi(char *ascii_str, efi_size_t len)
{
    efi_ch16_t *str = efi_alloc((len + 1) * sizeof(efi_ch16_t));
    for (efi_size_t i = 0; i < len; ++i) {
        str[i] = ascii_str[i];
    }
    str[len] = 0;
    return str;
}

//
// Retrieve the size of a file
//
static efi_size_t get_file_size(efi_file_protocol_t *file)
{
    efi_status_t    status;
    efi_file_info_t *file_info;
    efi_size_t      file_size;

    status = efi_get_file_info(file, &file_info);
    if (EFI_ERROR(status))
        efi_abort(L"Error getting file info!\n", status);

    file_size = file_info->file_size;
    efi_free(file_info);
    return file_size;
}

//
// Check if an ASCII string starts with a prefix
//
static efi_bool_t starts_with(char *str, char *prefix)
{
    while (*prefix)
        if (*prefix++ != *str++)
            return false;
    return true;
}

//
// Get the start of the next line in a string
//
static char *get_next_line(char *str)
{
    while (*str != '\n')
        if (*str++ == 0)
            return NULL;
    return ++str;
}

//
// Locate the file system corresponding to the boot volume
//
static efi_status_t locate_self_volume(efi_simple_file_system_protocol_t **self_volume)
{
    efi_status_t status;
    efi_loaded_image_protocol_t *loaded_image;

    status = efi_bs->handle_protocol(efi_image_handle,
        &(efi_guid_t) EFI_LOADED_IMAGE_PROTOCOL_GUID,
        (void **) &loaded_image);
    if (EFI_ERROR(status))
        return status;

    status = efi_bs->handle_protocol(loaded_image->device_handle,
        &(efi_guid_t) EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
        (void **) self_volume);
    return status;
}

//
// Read a file into a heap buffer from the boot volume, than NUL-terminate it
//
static char *read_text_file(efi_ch16_t *path)
{
    efi_status_t status;
    efi_simple_file_system_protocol_t *self_volume;

    // Open root directory of self-volume
    status = locate_self_volume(&self_volume);
    if (EFI_ERROR(status))
        efi_abort(L"Failed to locate boot volume!\n", status);
    efi_file_protocol_t *root_dir;
    status = self_volume->open_volume(self_volume, &root_dir);
    if (EFI_ERROR(status))
        efi_abort(L"Failed to open boot volume root directory!\n", status);

    // Open file
    efi_file_protocol_t *entries_file;
    status = root_dir->open(root_dir, &entries_file, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status))
        efi_abort(L"Error opening entries file!\n", status);

    // Read file
    efi_size_t entries_size = get_file_size(entries_file);
    char *data = efi_alloc(entries_size + 1);
    status = entries_file->read(entries_file, &entries_size, data);
    if (EFI_ERROR(status))
        efi_abort(L"Error reading boot entry description file!\n", status);

    // Close file
    status = entries_file->close(entries_file);
    if (EFI_ERROR(status))
        efi_abort(L"Error closing entries file!\n", status);
    // Close root directory
    status = root_dir->close(root_dir);
    if (EFI_ERROR(status))
        efi_abort(L"Failed to close boot volume root directory!\n", status);

    // NUL-terminate data, than return
    data[entries_size] = 0;
    return data;
}

//
// Parse the ASCII representation of a signed
//
static efi_ssize_t ascii_str_to_ssize(char *str, efi_size_t len)
{
    efi_bool_t negative = false;
    efi_ssize_t value = 0;

    // Consume sign if present
    if (len > 0) {
        if (*str == '-')
            negative = true;
        if (negative || *str == '+') {
            str++;
            len--;
        }
    }

    // Consume digits
    for (; len--; ++str)
        switch (*str) {
        case '0' ... '9':
            value = value * 10 + *str - '0';
            break;
        default:
            goto endloop;
        }
endloop:
    if (negative)
        return -value;
    return value;
}

//
// Make sure a menu entry has all required fields
//
static void validate_entry(menu_entry *entry)
{
    if (!entry->text)
        efi_abort(L"Menu entry without name!\n", EFI_INVALID_PARAMETER);
    if (!entry->path)
        efi_abort(L"Menu entry without path!\n", EFI_INVALID_PARAMETER);
}

void add_boot_entries(menu_screen **menu)
{
    // Read file
    char *file_data = read_text_file(ENTRIES_FILE_NAME);

    // Current entry
    efi_bool_t have_entry = false;
    menu_entry entry;

    for (char *current_line = file_data, *next_line;
            (next_line = get_next_line(current_line));
            current_line = next_line) {
        efi_size_t current_line_length = next_line - current_line;

        // Empty line or comment
        if (current_line[0] == ';' || current_line[0] == '\n')
            continue;

        // Start of a section
        if (current_line[0] == '[') {
            // Add previous entry if it existed
            if (have_entry) {
                validate_entry(&entry);
                menu_add_entries(menu, &entry, 1);
            }

            // Initialize new entry
            have_entry = true;
            entry.type = menu_type_exec;
            entry.text = NULL;
            entry.path = NULL;
            entry.flags = NULL;

            continue;
        }

        // Global options come before sections
        if (!have_entry) {
            if (starts_with(current_line, "default="))
                (*menu)->selected_entry = ascii_str_to_ssize(current_line + 8,
                    current_line_length - 9);
            else if (starts_with(current_line, "timeout="))
                (*menu)->timeout = ascii_str_to_ssize(current_line + 8,
                    current_line_length - 9);

            continue;
        }

        // Current line is part of an entry
        if (starts_with(current_line, "name="))
            entry.text = ascii_str_to_efi(current_line + 5,
                                          current_line_length - 6);
        else if (starts_with(current_line, "path="))
            entry.path = ascii_str_to_efi(current_line + 5,
                                          current_line_length - 6);
        else if (starts_with(current_line, "flags="))
            entry.flags = ascii_str_to_efi(current_line + 6,
                                           current_line_length - 7);
    }

    // Add final entry if it exists
    if (have_entry) {
        validate_entry(&entry);
        menu_add_entries(menu, &entry, 1);
    }

    // Ignore selected entry if the value was invalid
    if ((*menu)->selected_entry >= (*menu)->entry_count)
        (*menu)->selected_entry = 0;

    // Free file buffer
    efi_free(file_data);
}
