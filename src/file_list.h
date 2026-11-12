#pragma once

#include <stdbool.h>

#define FILE_LIST_VIEW_ROWS 7

typedef enum {
    FILE_ITEM_NONE = 0,
    FILE_ITEM_PARENT,
    FILE_ITEM_DIR,
    FILE_ITEM_WAV,
    FILE_ITEM_TAPE
} file_item_type_t;

// Inicializace browseru v root adresáři (např. "/sd")
bool file_list_init(const char *root_path);

// Reload aktuálního adresáře bez změny current dir
bool file_list_reload_current_dir(void);

// Počet položek v aktuálním adresáři (včetně "..", pokud existuje)
int file_list_count(void);

// Pohyb kurzoru
bool file_list_next(void);
bool file_list_prev(void);

// Vybraný item
const char *file_list_get_name(void);
const char *file_list_get_path(void);          // vrací pouze přehratelný soubor, jinak NULL
file_item_type_t file_list_get_type(void);

// Adresářový browser
bool file_list_enter_selected(void);           // vstoupí do DIR nebo parent("..")
bool file_list_go_parent(void);                // explicitní návrat výš
const char *file_list_get_current_dir(void);

// Viditelné okno pro OLED
int file_list_get_scroll_top(void);
int file_list_get_selected_index(void);
const char *file_list_get_visible_name(int row);
file_item_type_t file_list_get_visible_type(int row);
bool file_list_is_visible_selected(int row);
