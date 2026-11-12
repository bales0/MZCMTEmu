#include "file_list.h"

#include <dirent.h>
#include <strings.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>

#define FILE_LIST_MAX_ITEMS 512

typedef struct {
    char *name;
    char *path;
    file_item_type_t type;
} file_item_t;

static file_item_t s_items[FILE_LIST_MAX_ITEMS];
static int s_count = 0;
static int s_selected = 0;
static int s_scroll_top = 0;
static char *s_root_dir = NULL;
static char *s_current_dir = NULL;

// --------------------------------------------------------
// helpers
// --------------------------------------------------------

static void free_item(file_item_t *item)
{
    if (item == NULL) {
        return;
    }

    free(item->name);
    free(item->path);
    item->name = NULL;
    item->path = NULL;
    item->type = FILE_ITEM_NONE;
}

static void clear_items(void)
{
    for (int i = 0; i < s_count; i++) {
        free_item(&s_items[i]);
    }

    s_count = 0;
    s_selected = 0;
    s_scroll_top = 0;
}

static char *dup_string(const char *src)
{
    if (src == NULL) {
        return NULL;
    }

    size_t len = strlen(src);
    char *dst = (char *)malloc(len + 1);
    if (dst == NULL) {
        return NULL;
    }

    memcpy(dst, src, len + 1);
    return dst;
}

static char *normalize_path(const char *path)
{
    if (path == NULL) {
        return NULL;
    }

    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }

    char *out = (char *)malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }

    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

static char *join_path(const char *dir, const char *name)
{
    if (dir == NULL || name == NULL) {
        return NULL;
    }

    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    bool has_slash = (dir_len > 0 && dir[dir_len - 1] == '/');

    size_t total = dir_len + (has_slash ? 0 : 1) + name_len + 1;
    char *out = (char *)malloc(total);
    if (out == NULL) {
        return NULL;
    }

    memcpy(out, dir, dir_len);
    size_t pos = dir_len;

    if (!has_slash) {
        out[pos++] = '/';
    }

    memcpy(&out[pos], name, name_len + 1);
    return out;
}

static bool has_wav_extension(const char *name)
{
    if (name == NULL) {
        return false;
    }

    size_t len = strlen(name);
    if (len < 4) {
        return false;
    }

    const char *ext = name + len - 4;
    return (strcasecmp(ext, ".wav") == 0);
}

static bool has_tape_extension(const char *name)
{
    if (name == NULL) {
        return false;
    }

    size_t len = strlen(name);
    if (len < 4) {
        return false;
    }

    const char *ext = name + len - 4;
    return (strcasecmp(ext, ".mzf") == 0) ||
           (strcasecmp(ext, ".mzt") == 0) ||
           (strcasecmp(ext, ".m12") == 0) ||
           (strcasecmp(ext, ".lep") == 0) ||
           (strcasecmp(ext, ".l16") == 0) ||
           (strcasecmp(ext, ".tap") == 0);
}

static bool is_at_root(void)
{
    if (s_root_dir == NULL || s_current_dir == NULL) {
        return true;
    }

    return (strcmp(s_root_dir, s_current_dir) == 0);
}

static int item_sort_rank(file_item_type_t type)
{
    switch (type) {
        case FILE_ITEM_PARENT: return 0;
        case FILE_ITEM_DIR: return 1;
        case FILE_ITEM_WAV:
        case FILE_ITEM_TAPE: return 2;
        case FILE_ITEM_NONE:
        default: return 3;
    }
}

static const char *extension_ptr(const char *name)
{
    const char *dot;

    if (name == NULL) {
        return NULL;
    }

    dot = strrchr(name, '.');
    if (dot == NULL || dot == name) {
        return name + strlen(name);
    }

    return dot;
}

static int strncasecmp_len(const char *a, size_t a_len,
                           const char *b, size_t b_len)
{
    size_t min_len = (a_len < b_len) ? a_len : b_len;
    int cmp = strncasecmp(a, b, min_len);

    if (cmp != 0) {
        return cmp;
    }
    if (a_len < b_len) {
        return -1;
    }
    if (a_len > b_len) {
        return 1;
    }
    return 0;
}

static char *make_parent_path(const char *path)
{
    if (path == NULL) {
        return NULL;
    }

    if (s_root_dir != NULL && strcmp(path, s_root_dir) == 0) {
        return dup_string(s_root_dir);
    }

    char *tmp = dup_string(path);
    if (tmp == NULL) {
        return NULL;
    }

    char *slash = strrchr(tmp, '/');
    if (slash == NULL) {
        return tmp;
    }

    // "/" necháme být
    if (slash == tmp) {
        slash[1] = '\0';
        return tmp;
    }

    *slash = '\0';

    // nepustit nad root
    if (s_root_dir != NULL && strncmp(tmp, s_root_dir, strlen(s_root_dir)) != 0) {
        free(tmp);
        return dup_string(s_root_dir);
    }

    return tmp;
}

static int item_cmp(const void *a, const void *b)
{
    const file_item_t *ia = (const file_item_t *)a;
    const file_item_t *ib = (const file_item_t *)b;

    int rank_a = item_sort_rank(ia->type);
    int rank_b = item_sort_rank(ib->type);

    // pořadí: parent -> dir -> soubory; soubory pak název -> koncovka
    if (rank_a != rank_b) {
        return rank_a - rank_b;
    }

    if (ia->name == NULL && ib->name == NULL) {
        return 0;
    }

    if (ia->name == NULL) {
        return -1;
    }

    if (ib->name == NULL) {
        return 1;
    }

    if (rank_a == 2) {
        const char *ext_a = extension_ptr(ia->name);
        const char *ext_b = extension_ptr(ib->name);
        size_t stem_a_len = (size_t)(ext_a - ia->name);
        size_t stem_b_len = (size_t)(ext_b - ib->name);
        int stem_cmp = strncasecmp_len(ia->name, stem_a_len,
                                       ib->name, stem_b_len);

        if (stem_cmp != 0) {
            return stem_cmp;
        }

        return strcasecmp(ext_a, ext_b);
    }

    return strcasecmp(ia->name, ib->name);
}

static void clamp_scroll_bounds(void)
{
    if (s_selected < 0) {
        s_selected = 0;
    }

    if (s_count <= 0) {
        s_selected = 0;
        s_scroll_top = 0;
        return;
    }

    if (s_selected >= s_count) {
        s_selected = s_count - 1;
    }

    if (s_scroll_top < 0) {
        s_scroll_top = 0;
    }

    int max_scroll = s_count - FILE_LIST_VIEW_ROWS;
    if (max_scroll < 0) {
        max_scroll = 0;
    }

    if (s_scroll_top > max_scroll) {
        s_scroll_top = max_scroll;
    }
}

static void reveal_selected(void)
{
    if (s_selected < s_scroll_top) {
        s_scroll_top = s_selected;
    } else if (s_selected >= (s_scroll_top + FILE_LIST_VIEW_ROWS)) {
        s_scroll_top = s_selected - FILE_LIST_VIEW_ROWS + 1;
    }

    clamp_scroll_bounds();
}

static const char *basename_ptr_local(const char *path)
{
    const char *slash;

    if (path == NULL) {
        return "";
    }

    slash = strrchr(path, '/');
    return (slash == NULL) ? path : slash + 1;
}

static void select_item_by_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return;
    }

    for (int i = 0; i < s_count; i++) {
        if (s_items[i].name != NULL && strcmp(s_items[i].name, name) == 0) {
            s_selected = i;
            reveal_selected();
            return;
        }
    }
}

static bool add_item(const char *name, const char *path, file_item_type_t type)
{
    if (s_count >= FILE_LIST_MAX_ITEMS) {
        return false;
    }

    char *name_copy = dup_string(name);
    char *path_copy = dup_string(path);
    if (name_copy == NULL || path_copy == NULL) {
        free(name_copy);
        free(path_copy);
        return false;
    }

    s_items[s_count].name = name_copy;
    s_items[s_count].path = path_copy;
    s_items[s_count].type = type;
    s_count++;
    return true;
}

static bool reload_current_dir_internal(const char *select_name)
{
    clear_items();

    if (s_current_dir == NULL) {
        return false;
    }

    if (!is_at_root()) {
        char *parent = make_parent_path(s_current_dir);
        if (parent == NULL) {
            return false;
        }

        if (!add_item("..", parent, FILE_ITEM_PARENT)) {
            free(parent);
            return false;
        }

        free(parent);
    }

    DIR *dir = opendir(s_current_dir);
    if (dir == NULL) {
        clear_items();
        return false;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (s_count >= FILE_LIST_MAX_ITEMS) {
            break;
        }

        const char *name = ent->d_name;
        if (name == NULL || name[0] == '\0') {
            continue;
        }

        if (name[0] == '.') {
            continue;
        }

        char *full_path = join_path(s_current_dir, name);
        if (full_path == NULL) {
            continue;
        }

        struct stat st;
        if (stat(full_path, &st) != 0) {
            free(full_path);
            continue;
        }

        file_item_type_t type = FILE_ITEM_NONE;
        if (S_ISDIR(st.st_mode)) {
            type = FILE_ITEM_DIR;
        } else if (S_ISREG(st.st_mode) && has_wav_extension(name)) {
            type = FILE_ITEM_WAV;
        } else if (S_ISREG(st.st_mode) && has_tape_extension(name)) {
            type = FILE_ITEM_TAPE;
        } else {
            free(full_path);
            continue;
        }

        if (!add_item(name, full_path, type)) {
            free(full_path);
            break;
        }

        free(full_path);
    }

    closedir(dir);

    if (s_count > 1) {
        qsort(s_items, s_count, sizeof(s_items[0]), item_cmp);
    }

    clamp_scroll_bounds();
    select_item_by_name(select_name);
    return true;
}

// --------------------------------------------------------
// public API
// --------------------------------------------------------

bool file_list_reload_current_dir(void)
{
    return reload_current_dir_internal(NULL);
}

bool file_list_init(const char *root_path)
{
    clear_items();

    free(s_root_dir);
    free(s_current_dir);
    s_root_dir = NULL;
    s_current_dir = NULL;

    if (root_path == NULL) {
        return false;
    }

    s_root_dir = normalize_path(root_path);
    if (s_root_dir == NULL) {
        return false;
    }

    s_current_dir = dup_string(s_root_dir);
    if (s_current_dir == NULL) {
        free(s_root_dir);
        s_root_dir = NULL;
        return false;
    }

    return reload_current_dir_internal(NULL);
}

int file_list_count(void)
{
    return s_count;
}

bool file_list_next(void)
{
    if (s_count <= 0) {
        return false;
    }

    if (s_selected >= (s_count - 1)) {
        s_selected = 0;
        s_scroll_top = 0;
        clamp_scroll_bounds();
        return true;
    }

    s_selected++;

    if (s_count > FILE_LIST_VIEW_ROWS) {
        int preview_row = FILE_LIST_VIEW_ROWS - 2;   // předposlední viditelný řádek
        int max_scroll = s_count - FILE_LIST_VIEW_ROWS;
        if (max_scroll < 0) {
            max_scroll = 0;
        }

        // pokud ještě existuje další položka za výběrem,
        // drž výběr na předposledním řádku a nech jeden soubor dopředu vidět
        if (s_selected < (s_count - 1)) {
            int desired_top = s_selected - preview_row;
            if (desired_top < 0) {
                desired_top = 0;
            }
            if (desired_top > max_scroll) {
                desired_top = max_scroll;
            }
            if (desired_top > s_scroll_top) {
                s_scroll_top = desired_top;
            }
        } else {
            // poslední položka: už není co ukázat dopředu,
            // tak jen skoč na poslední možné okno
            s_scroll_top = max_scroll;
        }
    }

    clamp_scroll_bounds();
    return true;
}

bool file_list_prev(void)
{
    if (s_count <= 0) {
        return false;
    }

    if (s_selected <= 0) {
        s_selected = s_count - 1;
        s_scroll_top = s_count - FILE_LIST_VIEW_ROWS;
        clamp_scroll_bounds();
        return true;
    }

    s_selected--;

    if (s_count > FILE_LIST_VIEW_ROWS) {
        int preview_row = 1;   // druhý viditelný řádek = nech jeden soubor nad výběrem

        // pokud ještě existuje položka nad výběrem,
        // drž výběr na druhém řádku a nech jeden soubor nahoře vidět
        if (s_selected > 0) {
            int desired_top = s_selected - preview_row;
            if (desired_top < 0) {
                desired_top = 0;
            }
            if (desired_top < s_scroll_top) {
                s_scroll_top = desired_top;
            }
        } else {
            // první položka: nahoře už není co ukázat
            s_scroll_top = 0;
        }
    }

    clamp_scroll_bounds();
    return true;
}

const char *file_list_get_name(void)
{
    if (s_count <= 0) {
        return "<empty>";
    }

    return s_items[s_selected].name;
}

const char *file_list_get_path(void)
{
    if (s_count <= 0) {
        return NULL;
    }

    // pro současný PLAY flow vracej path jen když je vybraný přehratelný soubor
    if (s_items[s_selected].type != FILE_ITEM_WAV &&
        s_items[s_selected].type != FILE_ITEM_TAPE) {
        return NULL;
    }

    return s_items[s_selected].path;
}

file_item_type_t file_list_get_type(void)
{
    if (s_count <= 0) {
        return FILE_ITEM_NONE;
    }

    return s_items[s_selected].type;
}

bool file_list_enter_selected(void)
{
    if (s_count <= 0) {
        return false;
    }

    file_item_type_t type = s_items[s_selected].type;
    if (type != FILE_ITEM_DIR && type != FILE_ITEM_PARENT) {
        return false;
    }

    const char *select_name = NULL;
    char leaving_name[256];
    leaving_name[0] = '\0';

    if (type == FILE_ITEM_PARENT) {
        snprintf(leaving_name, sizeof(leaving_name), "%s",
                 basename_ptr_local(s_current_dir));
        select_name = leaving_name;
    }

    char *next_dir = dup_string(s_items[s_selected].path);
    if (next_dir == NULL) {
        return false;
    }

    free(s_current_dir);
    s_current_dir = next_dir;
    return reload_current_dir_internal(select_name);
}

bool file_list_go_parent(void)
{
    if (is_at_root()) {
        return false;
    }

    char leaving_name[256];
    snprintf(leaving_name, sizeof(leaving_name), "%s",
             basename_ptr_local(s_current_dir));

    char *parent = make_parent_path(s_current_dir);
    if (parent == NULL) {
        return false;
    }

    free(s_current_dir);
    s_current_dir = parent;
    return reload_current_dir_internal(leaving_name);
}

const char *file_list_get_current_dir(void)
{
    if (s_current_dir == NULL) {
        return "";
    }

    return s_current_dir;
}

int file_list_get_scroll_top(void)
{
    return s_scroll_top;
}

int file_list_get_selected_index(void)
{
    return s_selected;
}

const char *file_list_get_visible_name(int row)
{
    if (row < 0 || row >= FILE_LIST_VIEW_ROWS) {
        return "";
    }

    int index = s_scroll_top + row;
    if (index < 0 || index >= s_count) {
        return "";
    }

    return s_items[index].name;
}

file_item_type_t file_list_get_visible_type(int row)
{
    if (row < 0 || row >= FILE_LIST_VIEW_ROWS) {
        return FILE_ITEM_NONE;
    }

    int index = s_scroll_top + row;
    if (index < 0 || index >= s_count) {
        return FILE_ITEM_NONE;
    }

    return s_items[index].type;
}

bool file_list_is_visible_selected(int row)
{
    if (row < 0 || row >= FILE_LIST_VIEW_ROWS) {
        return false;
    }

    int index = s_scroll_top + row;
    return (index == s_selected);
}
