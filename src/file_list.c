#include "storage/fs.h"
#include "file_list.h"
#include "esp_heap_caps.h"
#include <dirent.h>
#include <strings.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>

#define FILE_LIST_MAX_ITEMS 65535U
#define FILE_LIST_NAME_BYTES (2U*1024U*1024U)
#define PATH_BYTES 1024U
/* A lifetime PSRAM index and name arena. Navigation resets offsets rather
 * than allocating/freeing every filename and full path. ISR never reads it. */
typedef struct { const char *name;file_item_type_t type; } file_item_t;
static file_item_t *s_items;
static char *names;
static size_t names_used;
static int s_count,s_selected,s_scroll_top;
static char s_root_dir[PATH_BYTES],s_current_dir[PATH_BYTES],selected_path[PATH_BYTES];
static const char *error_text="";
static file_list_cancel_fn cancel_fn;
static void *cancel_context;
void file_list_set_scan_control(file_list_cancel_fn cancel,void *context) { cancel_fn=cancel;cancel_context=context; }
const char *file_list_last_error(void) { return error_text; }
static void clear_items(void) { s_count=s_selected=s_scroll_top=0;names_used=0; }
static bool fail(const char *message) { clear_items();error_text=message;return false; }
static bool is_at_root(void) { return !strcmp(s_root_dir,s_current_dir); }
static bool join_path(char *out,size_t capacity,const char *directory,const char *name) {
    size_t n=strlen(directory);int length=snprintf(out,capacity,"%s%s%s",directory,n && directory[n-1]=='/' ? "":"/",name);
    return length>=0 && (size_t)length<capacity;
}
static bool parent_path(char out[PATH_BYTES]) {
    if(is_at_root()) return false;
    snprintf(out,PATH_BYTES,"%s",s_current_dir);char *slash=strrchr(out,'/');
    if(!slash) return false;
    if(slash==out) slash[1]=0;else *slash=0;
    size_t root_length=strlen(s_root_dir);
    if(strncmp(out,s_root_dir,root_length) || (root_length>1 && out[root_length] && out[root_length]!='/'))
        snprintf(out,PATH_BYTES,"%s",s_root_dir);
    return true;
}
static bool add_item(const char *name,file_item_type_t type) {
    size_t n=strlen(name)+1;
    if((unsigned)s_count==FILE_LIST_MAX_ITEMS || n>FILE_LIST_NAME_BYTES-names_used) return false;
    memcpy(names+names_used,name,n);s_items[s_count++]=(file_item_t){names+names_used,type};names_used+=n;return true;
}
static int rank(file_item_type_t type) { return type==FILE_ITEM_PARENT ? 0:type==FILE_ITEM_DIR ? 1:2; }
static const char *extension(const char *name) { const char *dot=strrchr(name,'.');return dot && dot!=name ? dot:name+strlen(name); }
static int item_cmp(const void *left,const void *right) {
    const file_item_t *a=left,*b=right;int ar=rank(a->type),br=rank(b->type);
    if(ar!=br) return ar-br;
    if(ar!=2) return strcasecmp(a->name,b->name);
    const char *ae=extension(a->name),*be=extension(b->name);size_t an=(size_t)(ae-a->name),bn=(size_t)(be-b->name);
    int result=strncasecmp(a->name,b->name,an<bn ? an:bn);
    if(result) return result;
    if(an!=bn) return an<bn ? -1:1;
    return strcasecmp(ae,be);
}
static file_item_type_t playable(const char *name) {
    const char *ext=extension(name);
    if(!strcasecmp(ext,".wav")) return FILE_ITEM_WAV;
    static const char *const types[]={".mzf",".mzt",".m12",".mzq",".qdf",".qd",".lep",".l16",".tap"};
    for(unsigned i=0;i<sizeof(types)/sizeof(types[0]);++i)if(!strcasecmp(ext,types[i]))return FILE_ITEM_TAPE;
    return FILE_ITEM_NONE;
}
static void clamp_scroll_bounds(void) {
    if(!s_count) {s_selected=s_scroll_top=0;return;}
    if(s_selected<0)s_selected=0;
    if(s_selected>=s_count)s_selected=s_count-1;
    int maximum=s_count>FILE_LIST_VIEW_ROWS ? s_count-FILE_LIST_VIEW_ROWS:0;
    if(s_scroll_top<0)s_scroll_top=0;
    if(s_scroll_top>maximum)s_scroll_top=maximum;
}
static void reveal_selected(void) {
    if(s_selected<s_scroll_top)s_scroll_top=s_selected;
    if(s_selected>=s_scroll_top+FILE_LIST_VIEW_ROWS)s_scroll_top=s_selected-FILE_LIST_VIEW_ROWS+1;
    clamp_scroll_bounds();
}
static bool load(const char *select_name) {
    error_text="";
    DIR *directory=cmt_opendir(s_current_dir);
    if(!directory){error_text="DIRECTORY ERROR";return false;}
    clear_items();
    bool ok=true;unsigned polled=0;
    if(!is_at_root() && !add_item("..",FILE_ITEM_PARENT))ok=false;
    struct dirent *entry;
    while(ok) {
        errno=0;entry=cmt_readdir(directory);
        if(!entry) { if(errno) {error_text="DIRECTORY READ";ok=false;} break; }
        if((polled++&31U)==0 && cancel_fn && cancel_fn(cancel_context)) {error_text="CANCELLED";ok=false;break;}
        const char *name=entry->d_name;
        if(!name[0] || name[0]=='.')continue;
        char path[PATH_BYTES];struct stat st;
        if(!join_path(path,sizeof(path),s_current_dir,name)) {error_text="PATH TOO LONG";ok=false;break;}
        file_item_type_t type=FILE_ITEM_NONE;bool known=false;
#if defined(DT_DIR) && defined(DT_REG) && (defined(ESP_PLATFORM) || defined(_DIRENT_HAVE_D_TYPE))
        if(entry->d_type==DT_DIR){type=FILE_ITEM_DIR;known=true;}
        else if(entry->d_type==DT_REG){type=playable(name);known=true;}
#endif
        if(!known){
            if(cmt_stat(path,&st)){error_text="DIRECTORY READ";ok=false;break;}
            type=S_ISDIR(st.st_mode) ? FILE_ITEM_DIR:S_ISREG(st.st_mode) ? playable(name):FILE_ITEM_NONE;
        }
        if(type!=FILE_ITEM_NONE && !add_item(name,type)) {error_text="DIRECTORY TOO LARGE";ok=false;break;}
    }
    if(cmt_closedir(directory)) {error_text="DIRECTORY CLOSE";ok=false;}
    if(!ok)return fail(error_text[0] ? error_text:"DIRECTORY TOO LARGE");
    if(s_count>1)qsort(s_items,(size_t)s_count,sizeof(*s_items),item_cmp);
    if(select_name)for(int i=0;i<s_count;++i)if(!strcmp(s_items[i].name,select_name)) {s_selected=i;break;}
    reveal_selected();return true;
}
bool file_list_init(const char *root) {
    clear_items();error_text="";
    if(!s_items)s_items=heap_caps_malloc(FILE_LIST_MAX_ITEMS*sizeof(*s_items),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!names)names=heap_caps_malloc(FILE_LIST_NAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s_items || !names)return fail("BROWSER PSRAM");
    if(!root || !root[0] || strlen(root)>=PATH_BYTES)return fail("BROWSER PATH");
    snprintf(s_root_dir,sizeof(s_root_dir),"%s",root);size_t n=strlen(s_root_dir);
    while(n>1 && s_root_dir[n-1]=='/')s_root_dir[--n]=0;
    snprintf(s_current_dir,sizeof(s_current_dir),"%s",s_root_dir);return load(NULL);
}
static bool navigate(const char *,const char *);
bool file_list_reload_current_dir(void) {
    char selected[256];snprintf(selected,sizeof(selected),"%s",s_count ? s_items[s_selected].name:"");
    int index=s_selected,top=s_scroll_top;
    if(!navigate(s_current_dir,selected))return false;
    if(s_count && strcmp(s_items[s_selected].name,selected))s_selected=index;
    s_scroll_top=top;clamp_scroll_bounds();reveal_selected();return true;
}
int file_list_count(void) { return s_count; }
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

const char *file_list_get_name(void) { return s_count ? s_items[s_selected].name:"<empty>"; }
file_item_type_t file_list_get_type(void) { return s_count ? s_items[s_selected].type:FILE_ITEM_NONE; }
const char *file_list_get_path(void) {
    file_item_type_t type=file_list_get_type();
    if(type!=FILE_ITEM_WAV && type!=FILE_ITEM_TAPE)return NULL;
    return join_path(selected_path,sizeof(selected_path),s_current_dir,s_items[s_selected].name) ? selected_path:NULL;
}
static bool navigate(const char *target,const char *select_name) {
    char previous[PATH_BYTES],selected[256];
    snprintf(previous,sizeof(previous),"%s",s_current_dir);
    snprintf(selected,sizeof(selected),"%s",s_count ? s_items[s_selected].name:"");
    int index=s_selected,top=s_scroll_top;
    if(strcmp(s_current_dir,target))snprintf(s_current_dir,sizeof(s_current_dir),"%s",target);
    if(load(select_name))return true;
    const char *original_error=error_text;
    snprintf(s_current_dir,sizeof(s_current_dir),"%s",previous);
    /* One lifetime arena: rebuild old index without cancellation rather than
     * duplicating 2.5MiB or exposing a partial directory after a failed scan. */
    file_list_cancel_fn saved_cancel=cancel_fn;cancel_fn=NULL;
    bool restored=load(selected);cancel_fn=saved_cancel;
    if(restored){
        if(s_count && strcmp(s_items[s_selected].name,selected))s_selected=index;
        s_scroll_top=top;clamp_scroll_bounds();reveal_selected();error_text=original_error;
    } else (void)fail("DIRECTORY RECOVERY");
    return false;
}
bool file_list_go_parent(void) {
    char target[PATH_BYTES],leaving[256];
    if(!parent_path(target))return false;
    const char *slash=strrchr(s_current_dir,'/');
    const char *name=slash ? slash+1:s_current_dir;
    size_t length=strlen(name);
    if(length>=sizeof(leaving)){error_text="NAME TOO LONG";return false;}
    memcpy(leaving,name,length+1);
    return navigate(target,leaving);
}
bool file_list_enter_selected(void) {
    if(file_list_get_type()==FILE_ITEM_PARENT)return file_list_go_parent();
    if(file_list_get_type()!=FILE_ITEM_DIR)return false;
    char target[PATH_BYTES];
    if(!join_path(target,sizeof(target),s_current_dir,s_items[s_selected].name)){error_text="PATH TOO LONG";return false;}
    return navigate(target,NULL);
}
const char *file_list_get_current_dir(void) { return s_current_dir; }
int file_list_get_scroll_top(void) { return s_scroll_top; }
int file_list_get_selected_index(void) { return s_selected; }
const char *file_list_get_visible_name(int row) {
    int i=s_scroll_top+row;return row>=0 && row<FILE_LIST_VIEW_ROWS && i<s_count ? s_items[i].name:"";
}
file_item_type_t file_list_get_visible_type(int row) {
    int i=s_scroll_top+row;return row>=0 && row<FILE_LIST_VIEW_ROWS && i<s_count ? s_items[i].type:FILE_ITEM_NONE;
}
bool file_list_is_visible_selected(int row) { return row>=0 && row<FILE_LIST_VIEW_ROWS && s_count && s_scroll_top+row==s_selected; }

bool file_list_error_requires_reinit(void){return !strcmp(error_text,"DIRECTORY READ") || !strcmp(error_text,"DIRECTORY CLOSE") || !strcmp(error_text,"DIRECTORY RECOVERY");}
