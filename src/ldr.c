#include <efi.h>
#include <efiutil.h>
#include "menu.h"
#include "config.h"

// About sub-menu
menu_screen about_menu = {
	.title = L"About YAUB",
	.timeout = -1,
	.selected_entry = 4,
	.entry_count = 5,
	.entries = {
		{ menu_type_info, L"Welcome to YAUB (Built for: " BUILD_ARCH ")!" },
		{ menu_type_info, L"This program is licensed under the ISC license" },
		{ menu_type_info, L"and the source code is available at:" },
		{ menu_type_info, L"https://github.com/kukrimate/yaub" },
		{ menu_type_exit, L"Back..." },
	}
};

// Main menu fixed entries
menu_entry main_menu_fixed[] = {
	{
		.type = menu_type_subscreen,
		.text = L"About YAUB",
		.subscreen = &about_menu
	},
	{
		.type = menu_type_exit,
		.text = L"Exit"
	}
};

static efi_device_path_protocol_t *get_self_volume_dp()
{
	efi_status_t status;
	efi_loaded_image_protocol_t *self_loaded_image;
	efi_device_path_protocol_t *dp;

	status = efi_bs->handle_protocol(efi_image_handle,
		&(efi_guid_t) EFI_LOADED_IMAGE_PROTOCOL_GUID,
		(void **) &self_loaded_image);
	if (EFI_ERROR(status))
		goto err;

	status = efi_bs->handle_protocol(self_loaded_image->device_handle,
		&(efi_guid_t) EFI_DEVICE_PATH_PROTOCOL_GUID,
		(void **) &dp);
	if (EFI_ERROR(status))
		goto err;

	return dp;
err:
	efi_abort(L"Error locating self volume device path!", status);
}

static void start_efi_image(efi_ch16_t *path, efi_ch16_t *flags)
{
	efi_status_t status;
	efi_in_key_t key;
	efi_handle_t child_image_handle;
	efi_device_path_protocol_t *image_dp;
	efi_loaded_image_protocol_t *loaded_image;

	// De-init menu before running image
	menu_clearscreen();
	menu_fini();

	// Load image
	image_dp = efi_dp_append_file_path(get_self_volume_dp(), path);
	status = efi_bs->load_image(false, efi_image_handle, image_dp, NULL, 0,
		&child_image_handle);
	if (EFI_ERROR(status))
		goto out;

	// Append load options
	if (flags != NULL) {
		status = efi_bs->handle_protocol(child_image_handle,
			&(efi_guid_t) EFI_LOADED_IMAGE_PROTOCOL_GUID,
			(void **) &loaded_image);
		if (EFI_ERROR(status))
			goto out;
		loaded_image->load_options_size = efi_strsize(flags);
		loaded_image->load_options = flags;
	}

	// Start image
	status = efi_bs->start_image(child_image_handle, NULL, NULL);

out:
	efi_free(image_dp);

	// Re-init menu on image exit
	menu_init();
	menu_clearscreen();

	// Wait for keypress before returning
	efi_print(L"Application exited with status: %p!\n"
			  L"Press any key to continue!\n", status);
	menu_wait_for_key(&key);
}

efi_status_t efiapi efi_main(efi_handle_t image_handle, efi_system_table_t *system_table)
{
	menu_screen *main_menu;
	efi_size_t entry_idx;
	menu_entry *selected;

	efi_init(image_handle, system_table);

	menu_init();
	menu_clearscreen();

	// Setup main menu
	main_menu = efi_alloc(sizeof(menu_screen));
	main_menu->title = L"Select boot option";
	main_menu->timeout = -1;
	main_menu->selected_entry = 0;
	main_menu->entry_count = 0;

	// Add boot entries to menu
	add_boot_entries(&main_menu);
	// Add fixed entries
	menu_add_entries(&main_menu, main_menu_fixed, ARRAY_SIZE(main_menu_fixed));

	for (;;) {
		selected = menu_run(main_menu);
		switch (selected->type) {
		case menu_type_exit:
			goto done;
		case menu_type_exec:
			start_efi_image(selected->path, selected->flags);
			break;
		default:
			break;
		}
	}

done:
	efi_free(main_menu);
	menu_clearscreen();
	menu_fini();
	return EFI_SUCCESS;
}
