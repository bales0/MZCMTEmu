#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "formats/sidecar_edit.h"
#include "formats/sidecar.h"
#include "ui/sidecar_editor.h"
#include "ui/key_icons.h"
#include "storage/fs.h"
#include "drivers/oled.h"
#include "drivers/i2c.h"
#include "freertos/task.h"
void vTaskDelay(TickType_t ticks){(void)ticks;}
esp_err_t i2c_write_to_device(uint8_t address,const uint8_t *bytes,size_t n,uint32_t timeout){(void)address;(void)bytes;(void)n;(void)timeout;return ESP_OK;}
static unsigned cut;
bool sc_test_cut(unsigned stage){return stage==cut;}
static void put(const char *name,const char *text){FILE *f=fopen(name,"wb");assert(f);assert(fwrite(text,1,strlen(text),f)==strlen(text));assert(!fclose(f));}
static bool present(const char *name){struct stat st;return !stat(name,&st);}
static void remove_if(const char *name){if(present(name))assert(!remove(name));}
static void clean(const char *target){char aux[1024];remove_if(target);const char *suffix[]={".SCN",".SCB",".SCJ"};for(unsigned i=0;i<3;++i){snprintf(aux,sizeof(aux),"%s%s",target,suffix[i]);remove_if(aux);}}
static uint32_t hash(const char *name){FILE *f=fopen(name,"rb");assert(f);uint32_t h=2166136261U;int c;while((c=fgetc(f))!=EOF)h=(h^(unsigned char)c)*16777619U;assert(!fclose(f));return h;}
static void paths(void){char out[1024]="UNCHANGED";
 assert(sidecar_build_path("Games.mZf",out,sizeof(out)) && !strcmp(out,"Games.MFI"));
 assert(sidecar_build_path("Games.m12",out,sizeof(out)) && !strcmp(out,"Games.M2I"));
 assert(sidecar_build_path("Games.mZt",out,sizeof(out)) && !strcmp(out,"Games.MTI"));
 const char *qd[]={"Games.QD","Games.QDF","Games.MZQ"};
 for(unsigned i=0;i<3;++i){char expected[64];snprintf(expected,sizeof(expected),"%s.QDI",qd[i]);assert(sidecar_build_path(qd[i],out,sizeof(out)) && !strcmp(out,expected));}
 strcpy(out,"UNCHANGED");assert(!sidecar_build_path("no.wav",out,sizeof(out)) && !strcmp(out,"UNCHANGED"));
 assert(!sidecar_build_path("x.qd",out,8));char huge[1024];memset(huge,'a',sizeof(huge));huge[1019]='.';huge[1020]='m';huge[1021]='z';huge[1022]='q';huge[1023]=0;assert(!sidecar_build_path(huge,out,sizeof(out)));
 char maximum[1024];memset(maximum,'x',1023);maximum[1023]=0;maximum[200]=maximum[401]=maximum[602]=maximum[803]='/';memcpy(maximum+1019,".mzf",4);assert(sidecar_build_path(maximum,out,sizeof(out)) && strlen(out)==1023);
 for(unsigned f=0;f<7;++f)for(unsigned speed=0;speed<4;++speed){uint8_t mode=sc_make_mode(f,speed);if(mode!=SC_NONE)assert(sc_mode_fields(mode,NULL,NULL));}
 assert(sc_make_mode(2,3)==SC_NONE && sc_make_mode(3,1)==SC_NONE && sc_make_mode(3,3)==SC_NONE);
 puts("PASS sidecar names MFI/M2I/MTI and collision-free appended QDI; path limits and supported families");
}
static void finder_paths(void){
#ifdef _WIN32
 assert(!_mkdir("lookup"));
#else
 assert(!mkdir("lookup",0700));
#endif
 char out[1024],absolute[1024],directory[1024];bool exists;
 put("lookup/Game.qd","SOURCE");put("lookup/Game.qd.qDi","RECORD=1\nTYPE=IC\nSPEED=1:4\n");
 uint32_t source_hash=hash("lookup/Game.qd"),profile_hash=hash("lookup/Game.qd.qDi");
 assert(sidecar_find("lookup/Game.qd",out,sizeof(out),&exists) && exists && !strcmp(out,"lookup/Game.qd.qDi"));
 assert(sidecar_find("./lookup/Game.qd",out,sizeof(out),&exists) && exists && !strcmp(out,"./lookup/Game.qd.qDi"));
 assert(getcwd(directory,sizeof(directory)));
 for(char *c=directory;*c;++c)if(*c=='\\')*c='/';
 assert(snprintf(absolute,sizeof(absolute),"%s/lookup/Game.qd",directory)>0);
 assert(sidecar_find(absolute,out,sizeof(out),&exists) && exists && !strcmp(out+strlen(directory),"/lookup/Game.qd.qDi"));
 assert(!strcmp(absolute+strlen(directory),"/lookup/Game.qd")); /* Input is untouched. */
 strcpy(out,"lookup/Game.qd");assert(sidecar_find(out,out,sizeof(out),&exists) && exists && !strcmp(out,"lookup/Game.qd.qDi"));
 const char *absent="./lookup/absent.qd";size_t exact=strlen(absent)+5;
 assert(sidecar_find(absent,out,exact,&exists) && !exists && !strcmp(out,"./lookup/absent.qd.QDI"));
 assert(!sidecar_find(absent,out,exact-1,&exists));
 assert(!sidecar_find("lookup/Game.wav",out,sizeof(out),&exists));
 assert(!sidecar_find("missing-directory/absent.qd",out,sizeof(out),&exists));
 assert(hash("lookup/Game.qd")==source_hash && hash("lookup/Game.qd.qDi")==profile_hash);
 remove_if("lookup/Game.qd.qDi");remove_if("lookup/Game.qd");assert(!rmdir("lookup"));
 puts("PASS low-stack lookup: relative/absolute/dot paths, mixed extension case, in-place output, exact capacity, absent files/directories and unchanged input/SD data");
}
static void roundtrip(void){
 const char *sources[]={"game.mzf","game.m12","game.mzt","game.qd","game.qdf","game.mzq"};
 for(unsigned format=0;format<6;++format){char target[1024];assert(sidecar_build_path(sources[format],target,sizeof(target)));clean(target);put(sources[format],"SOURCE IMAGE DO NOT MODIFY");uint32_t original=hash(sources[format]);
  sc_document_t d;unsigned records=format<2 ? 1:format==2 ? 12:50;
  assert(sc_load(&d,sources[format],(uint16_t)records) && !d.existed && sc_profile(&d,1)==SC_NONE);
  assert(!sc_changes(&d) && !present(target));
  for(unsigned r=1;r<=records;++r)assert(sc_accept(&d,(uint16_t)r,LOADER_MODE_NORMAL_1_1));
  assert(sc_save(&d) && !sc_changes(&d));
  assert(sc_load(&d,sources[format],(uint16_t)records));
  unsigned a=records==1 ? 1:3,b=records==1 ? 1:7;
  assert(sc_accept(&d,(uint16_t)a,LOADER_MODE_IC_1_4));assert(sc_accept(&d,(uint16_t)b,LOADER_MODE_TC_1_3));
  assert(sc_save(&d));
  for(unsigned r=1;r<=records;++r){loader_mode_t mode;assert(sidecar_read(sources[format],(uint16_t)r,&mode));assert(mode==(r==b ? LOADER_MODE_TC_1_3:r==a ? LOADER_MODE_IC_1_4:LOADER_MODE_NORMAL_1_1));}
  bool used=false;assert(sidecar_resolve(sources[format],(uint16_t)b,LOADER_MODE_AUTO,&used)==LOADER_MODE_TC_1_3 && used);
  assert(sidecar_resolve(sources[format],(uint16_t)b,LOADER_MODE_IC_1_2,&used)==LOADER_MODE_IC_1_2 && !used);
  assert(hash(sources[format])==original);clean(target);remove_if(sources[format]);
 }
 put("sparse.qd","image");sc_document_t d;assert(sc_load(&d,"sparse.qd",50));assert(sc_accept(&d,3,LOADER_MODE_UL_MZ800) && sc_accept(&d,7,LOADER_MODE_UL_MZ700) && sc_accept(&d,50,LOADER_MODE_UL));assert(sc_save(&d));assert(sc_load(&d,"sparse.qd",50) && d.count==3 && sc_profile(&d,1)==SC_NONE);clean("sparse.qd.QDI");remove_if("sparse.qd");
#ifndef _WIN32
 put("žluťoučký.mzf","SOURCE");assert(sc_load(&d,"žluťoučký.mzf",1) && sc_accept(&d,1,LOADER_MODE_NORMAL_1_2) && sc_save(&d));assert(sc_load(&d,"žluťoučký.mzf",1) && sc_profile(&d,1)==LOADER_MODE_NORMAL_1_2);clean("žluťoučký.MFI");remove_if("žluťoučký.mzf");
#endif
 puts("PASS all sidecar formats CREATE/UPDATE; 12-record MTI and 50-record QDI preserve other profiles; sparse sections, UL without SPEED, AUTO/manual and immutable images");
}
static void invalid(void){sc_document_t d;put("bad.mzt","source");
 const char *bad[]={"RECORD=1\nTYPE=IC\nSPEED=1:4\nRECORD=1\nTYPE=TC\nSPEED=1:3\n","RECORD=1\nTYPE=TC\nSPEED=1:4\n","RECORD=1\nTYPE=IC\nTYPE=NORMAL\nSPEED=1:4\n","RECORD=1\nTYPE=IC\nSPEED=1:4\nUNKNOWN=DATA\n","RECORD=13\nTYPE=IC\nSPEED=1:4\n","RECORD=1\nTYPE=IC\n",""};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i){put("bad.MTI",bad[i]);assert(!sc_load(&d,"bad.mzt",12) && d.error);}
 put("bad.MTI","RECORD=1\nTYPE=UL_MZ800\n");assert(sc_load(&d,"bad.mzt",12));
 assert(sc_accept(&d,3,LOADER_MODE_IC_1_4));put("bad.MTI","RECORD=1\nTYPE=TC\nSPEED=1:4\n");assert(!sc_save(&d));
 remove_if("bad.MTI");put("bad.mTi","RECORD=1\nTYPE=IC\nSPEED=1:4\n");assert(sc_load(&d,"bad.mzt",12) && !strcmp(d.target,"bad.mTi"));assert(sc_accept(&d,7,LOADER_MODE_TC_1_2) && sc_save(&d));assert(present("bad.mTi"));clean("bad.mTi");remove_if("bad.mzt");
 put("big.mzf","source");FILE *f=fopen("big.MFI","wb");assert(f);for(unsigned i=0;i<SC_MAX_BYTES+1;++i)fputc('\n',f);assert(!fclose(f));assert(!sc_load(&d,"big.mzf",1));clean("big.MFI");remove_if("big.mzf");
#ifndef _WIN32
 put("alias.mzf","source");put("alias.MFI","TYPE=IC\nSPEED=1:4\n");put("alias.mfi","TYPE=NORMAL\nSPEED=1:1\n");
 DIR *aliases=opendir(".");assert(aliases);unsigned alias_count=0;struct dirent *de;while((de=readdir(aliases))!=NULL)if(!strcmp(de->d_name,"alias.MFI") || !strcmp(de->d_name,"alias.mfi"))++alias_count;assert(!closedir(aliases));
 if(alias_count==2)assert(!sc_load(&d,"alias.mzf",1));else assert(sc_load(&d,"alias.mzf",1));
 remove_if("alias.MFI");remove_if("alias.mfi");remove_if("alias.mzf");
#endif
 puts("PASS malformed/unknown/duplicate/out-of-range/oversized sidecars rejected; mixed case spelling preserved; stale document cannot overwrite changed SD metadata");
}
static void recovery(void){
 for(unsigned existing=0;existing<2;++existing)for(unsigned stage=1;stage<=5;++stage){
  clean("power.QDI");clean("power.qd.QDI");put("power.qd","image");
  if(existing)put("power.qd.QDI","RECORD=1\nTYPE=NORMAL\nSPEED=1:1\n");
  sc_document_t d;assert(sc_load(&d,"power.qd",3));assert(sc_accept(&d,1,LOADER_MODE_IC_1_4));cut=stage;assert(!sc_save(&d));cut=0;
  assert(!sc_load(&d,"power.qd",3) && d.recovery);assert(sc_recover(&d));assert(sc_load(&d,"power.qd",3));
  uint8_t expected=stage>=4 ? LOADER_MODE_IC_1_4:existing ? LOADER_MODE_NORMAL_1_1:SC_NONE;
  assert(sc_profile(&d,1)==expected);clean("power.qd.QDI");remove_if("power.qd");
 }
 puts("PASS power interruption after journal/new/backup/install/commit for CREATE and UPDATE: old or verified new recovered without changing image");
}
static ui_model_t model;
static void (*task)(void *);static void *task_context;
bool cmt_storage_execute_async(void (*fn)(void *),void *ctx){if(task)return false;task=fn;task_context=ctx;return true;}
static void event(btn_event_t e){if(e==BTN_NONE && task){void (*fn)(void *)=task;task=NULL;fn(task_context);}sc_editor_event(e,true);sc_editor_render(&model,0);
 if(!strcmp(model.line[2],"CHOOSE SPEED") || !strcmp(model.line[2],"CHOOSE LOADER") || !strcmp(model.line[2],"SELECT RECORD") || !strcmp(model.line[2],"DISCARD CHANGES?") || !strcmp(model.line[2],"INVALID / SD ERROR") || !strcmp(model.line[2],"CONFIRM RECOVERY")){
  assert(!model.line[6][0] && strlen(model.line[7])<=21 && strchr(model.line[7],UI_KEY_STOP_CODE));
  for(unsigned row=0;row<8;++row)assert(!strstr(model.line[row],"PLAY:") && !strstr(model.line[row],"STOP:"));
 }
}
static void snapshot(const char *name){
#ifdef SC_RENDER_DIR
 oled_clear();for(unsigned row=0;row<8;++row)oled_text_xy(0,(int)row*8,model.line[row],1,true);
 char path[1024];snprintf(path,sizeof(path),"%s/%s.pbm",SC_RENDER_DIR,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P4\n128 64\n");
 for(unsigned y=0;y<64;++y)for(unsigned x=0;x<128;x+=8){uint8_t bits=0;for(unsigned b=0;b<8;++b)if(oled_framebuffer()[y/8*128+x+b]&(1U<<(y%8)))bits|=(uint8_t)(0x80U>>b);assert(fputc(bits,f)!=EOF);}
 assert(!fclose(f));
#else
 (void)name;
#endif
}
static void until_closed(void){for(unsigned i=0;i<10 && sc_editor_active();++i){event(BTN_STOP);if(sc_editor_active())event(BTN_PLAY);}assert(!sc_editor_active());}
static void ui(void){
 put("ui.mzt","source");clean("ui.MTI");assert(sc_editor_open("ui.mzt",12,3));event(BTN_NONE);
 program_metadata_t preview={0};preview.valid=PROGRAM_TITLE;preview.confidence=PROGRAM_HEADER_VALID;strcpy(preview.title,"THIRD PROGRAM");sc_editor_metadata(&preview);event(BTN_NONE);
 assert(strstr(model.line[2],"NO PROFILE"));snapshot("sidecar-new");event(BTN_NEXT);event(BTN_PLAY);
 assert(!strcmp(model.line[2],"CHOOSE LOADER") && !strcmp(model.line[4],"NORMAL"));snapshot("sidecar-loader-only");
 event(BTN_NEXT);assert(!strcmp(model.line[4],"IC"));event(BTN_PLAY);
 assert(!strcmp(model.line[2],"CHOOSE SPEED") && !present("ui.MTI") && !strcmp(model.line[7],UI_HELP_OK_CANCEL) && !model.line[6][0]);
 snapshot("sidecar-speed-footer");event(BTN_STOP);assert(strstr(model.line[4],"--") && !present("ui.MTI"));
 event(BTN_PLAY);event(BTN_NEXT);event(BTN_PLAY);
 assert(!strcmp(model.line[2],"CHOOSE SPEED"));event(BTN_NEXT);event(BTN_NEXT);event(BTN_NEXT);
 assert(!strcmp(model.line[2],"CHOOSE SPEED") && strstr(model.line[4],"IC 1:4") && !present("ui.MTI"));event(BTN_PLAY);
 assert(strstr(model.line[2],"NEW") && strstr(model.line[4],"IC 1:4") && !present("ui.MTI"));
 assert(strstr(model.line[4],"IC") && !present("ui.MTI") && !model.line[6][0] && !model.line[7][0] && !strcmp(model.line[5]," " UI_KEY_PLAY " SAVE  " UI_KEY_STOP " CANCEL"));snapshot("sidecar-accepted");
 /* Candidate title follows navigation before selection; no profile or SD edits. */
 event(BTN_PREV);event(BTN_PLAY);assert(!strcmp(model.line[3],"THIRD PROGRAM"));
 event(BTN_NEXT);assert(sc_editor_preview_record()==4 && sc_editor_record()==3 && !strcmp(model.line[3],"LOADING NAME..."));
 strcpy(preview.title,"FOURTH PROGRAM");sc_editor_metadata(&preview);event(BTN_NONE);
 assert(!strcmp(model.line[3],"FOURTH PROGRAM") && !present("ui.MTI"));snapshot("sidecar-record-name");
 event(BTN_STOP);assert(sc_editor_record()==3 && sc_editor_preview_record()==3 && strstr(model.line[4],"IC") && !strcmp(model.line[3],"LOADING NAME..."));
 strcpy(preview.title,"THIRD PROGRAM");sc_editor_metadata(&preview);event(BTN_NONE);assert(!strcmp(model.line[3],"THIRD PROGRAM"));
 event(BTN_PLAY);event(BTN_NEXT);strcpy(preview.title,"FOURTH PROGRAM");sc_editor_metadata(&preview);event(BTN_PLAY);
 assert(sc_editor_record()==4 && strstr(model.line[2],"NO PROFILE") && !strcmp(model.line[3],"FOURTH PROGRAM"));
 event(BTN_PLAY);event(BTN_PREV);strcpy(preview.title,"THIRD PROGRAM");sc_editor_metadata(&preview);event(BTN_PLAY);
 assert(sc_editor_record()==3 && strstr(model.line[4],"IC") && !strcmp(model.line[3],"THIRD PROGRAM"));
 event(BTN_NEXT);assert(!strcmp(model.line[4],"}LOADER: IC 1:4"));
 event(BTN_NEXT);assert(!model.line[6][0] && !strcmp(model.line[5],"}" UI_KEY_PLAY " SAVE  " UI_KEY_STOP " CANCEL") && !model.line[7][0] && !present("ui.MTI"));
 for(unsigned row=0;row<8;++row)assert(!strstr(model.line[row],"SPEED:"));
 snapshot("sidecar-direct-save");
 event(BTN_NEXT);assert(strstr(model.line[2],"}REC"));event(BTN_PREV);assert(!strcmp(model.line[5],"}" UI_KEY_PLAY " SAVE  " UI_KEY_STOP " CANCEL") && !model.line[7][0]);
 event(BTN_STOP);assert(!strcmp(model.line[2],"DISCARD CHANGES?") && !present("ui.MTI"));event(BTN_STOP);
 event(BTN_PLAY);assert(sc_editor_busy() && !present("ui.MTI") && !strcmp(model.line[3],"SAVING..."));
 snapshot("sidecar-saving");event(BTN_NEXT);assert(sc_editor_busy());event(BTN_NONE);assert(strstr(model.line[2],"EXISTING") && strstr(model.line[4],"IC 1:4") && !model.line[7][0]);snapshot("sidecar-after-save");event(BTN_PLAY);assert(!sc_editor_busy());until_closed();
 loader_mode_t mode;assert(sidecar_read("ui.mzt",3,&mode) && mode==LOADER_MODE_IC_1_4);assert(!sidecar_read("ui.mzt",4,&mode));
 uint32_t saved_hash=hash("ui.MTI");
 assert(sc_editor_open("ui.mzt",12,3));assert(sc_editor_busy());event(BTN_PLAY);assert(sc_editor_busy() && hash("ui.MTI")==saved_hash);event(BTN_NONE);
 event(BTN_NEXT);event(BTN_PLAY);event(BTN_PLAY);event(BTN_NEXT);event(BTN_PLAY);
 assert(hash("ui.MTI")==saved_hash);event(BTN_STOP);assert(!strcmp(model.line[2],"DISCARD CHANGES?"));event(BTN_STOP);
 assert(sc_editor_active());
 sc_editor_event(BTN_STOP,false);sc_editor_render(&model,0);sc_editor_event(BTN_PLAY,false);
 assert(!sc_editor_active() && hash("ui.MTI")==saved_hash);
 clean("ui.MTI");remove_if("ui.mzt");
 puts("PASS real editor: navigation/proposals/STOP do not modify profiles or SD; record candidate titles before selection and STOP restores title/profile; loader-only choice, OK accepts SPEED directly, STOP cancels proposal, marked SAVE/CANCEL immediately under LOADER, persistent key icons, SAVING then editor without confirmation, three-choice wrap and one SAVE starts CREATE");
}
static void profile_steps(void){
 put("steps.mzf","source");put("steps.MFI","TYPE=IC\nSPEED=1:4\n");uint32_t original=hash("steps.MFI");
 assert(sc_editor_open("steps.mzf",1,1));event(BTN_NONE);event(BTN_NEXT);event(BTN_PLAY);event(BTN_NEXT);event(BTN_PLAY);
 assert(!strcmp(model.line[2],"CHOOSE SPEED") && strstr(model.line[4],"TC 1:1"));
 event(BTN_NEXT);event(BTN_NEXT);assert(!strcmp(model.line[2],"CHOOSE SPEED") && strstr(model.line[4],"TC 1:3"));
 event(BTN_STOP);assert(strstr(model.line[4],"IC 1:4"));
 event(BTN_PLAY);event(BTN_NEXT);event(BTN_NEXT);event(BTN_PLAY);assert(!strcmp(model.line[2],"CHOOSE SPEED") && strstr(model.line[4],"MZ700 1:1"));
 event(BTN_NEXT);assert(strstr(model.line[4],"MZ700 1:3"));event(BTN_STOP);
 event(BTN_PLAY);event(BTN_NEXT);event(BTN_NEXT);event(BTN_NEXT);event(BTN_PLAY);
 assert(strstr(model.line[4],"UL N/A") && strcmp(model.line[2],"CONFIRM PROFILE"));snapshot("sidecar-ul-accepted");
 assert(strstr(model.line[4],"UL N/A"));
 for(unsigned row=0;row<8;++row)assert(!strstr(model.line[row],"SPEED:"));
 event(BTN_NEXT);assert(!model.line[6][0] && !strcmp(model.line[5],"}" UI_KEY_PLAY " SAVE  " UI_KEY_STOP " CANCEL") && !model.line[7][0] && hash("steps.MFI")==original);
 event(BTN_PLAY);assert(sc_editor_busy() && hash("steps.MFI")==original);event(BTN_NONE);assert(strstr(model.line[2],"EXISTING") && strstr(model.line[4],"UL N/A"));
 loader_mode_t updated;assert(sidecar_read("steps.mzf",1,&updated) && updated==LOADER_MODE_UL && hash("steps.MFI")!=original);
 until_closed();clean("steps.MFI");remove_if("steps.mzf");
 puts("PASS direct profile OK: TC allows 1/2/3 after IC1:4, MZ700 skips invalid speeds, UL skips SPEED, STOP cancels both fields; one SAVE starts UPDATE without extra confirmation");
}
static void error_footers(void){
 put("footer.mzf","source");put("footer.MFI","UNKNOWN=UNSUPPORTED\n");
 assert(sc_editor_open("footer.mzf",1,1));event(BTN_NONE);assert(!strcmp(model.line[2],"INVALID / SD ERROR") && !strchr(model.line[7],UI_KEY_PLAY_CODE));snapshot("sidecar-error-footer");until_closed();clean("footer.MFI");
 put("footer.MFI","TYPE=NORMAL\nSPEED=1:1\n");uint32_t original=hash("footer.MFI");
 sc_document_t document;assert(sc_load(&document,"footer.mzf",1) && sc_accept(&document,1,LOADER_MODE_IC_1_4));cut=2;assert(!sc_save(&document));cut=0;
 assert(sc_editor_open("footer.mzf",1,1));event(BTN_NONE);assert(!strcmp(model.line[2],"INVALID / SD ERROR") && strchr(model.line[7],UI_KEY_PLAY_CODE));
 event(BTN_PLAY);assert(!strcmp(model.line[2],"CONFIRM RECOVERY"));snapshot("sidecar-recover-footer");event(BTN_STOP);assert(hash("footer.MFI")==original);
 event(BTN_PLAY);event(BTN_PLAY);assert(sc_editor_busy());event(BTN_NONE);assert(strstr(model.line[4],"NORMAL 1:1") && hash("footer.MFI")==original);
 until_closed();clean("footer.MFI");remove_if("footer.mzf");
 puts("PASS single icon footer on error/recovery/discard dialogs, fits 21 cells; explicit recovery consent and original metadata retained");
}
int main(void){paths();finder_paths();roundtrip();invalid();recovery();ui();profile_steps();error_footers();return 0;}
