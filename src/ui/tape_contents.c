/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui/tape_contents.h"
#include "ui/key_icons.h"
#include "formats/mz_tape_profiles.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static void timestamp(char *out,size_t size,uint64_t us) {
    uint64_t seconds=us/1000000;
    if(seconds<3600)snprintf(out,size,"%02u:%02u",(unsigned)(seconds/60),(unsigned)(seconds%60));
    else if(seconds<360000)snprintf(out,size,"%u:%02u:%02u",(unsigned)(seconds/3600),(unsigned)(seconds/60%60),(unsigned)(seconds%60));
    else snprintf(out,size,"%uh%02u",(unsigned)(seconds/3600),(unsigned)(seconds/60%60));
}
void tape_contents_open(tape_contents_t *v){memset(v,0,sizeof(*v));v->page=TAPE_UI_SCAN;}
bool tape_previous_click(tape_previous_click_t *state,btn_event_t event,uint32_t now,bool enabled,
                          const tape_previous_request_t *request,tape_previous_request_t *out) {
    if(!enabled){state->pending=false;return false;}
    if(event==BTN_PREV && request) {
        if(state->pending && (uint32_t)(now-state->pressed_at)<=TAPE_PREV_DOUBLE_MS) {
            *out=state->request;out->previous=true;state->pending=false;return true;
        }
        bool due=state->pending;
        if(due){*out=state->request;out->previous=false;}
        state->request=*request;state->pressed_at=now;state->pending=true;
        return due;
    }
    if(event!=BTN_NONE){state->pending=false;return false;}
    if(state->pending && (uint32_t)(now-state->pressed_at)>=TAPE_PREV_DOUBLE_MS) {
        *out=state->request;out->previous=false;state->pending=false;return true;
    }
    return false;
}
tape_gap_result_t tape_previous_target(uint64_t origin,bool previous,tape_previous_find_fn before,
                                       void *context,uint64_t *target) {
    uint64_t current=0;
    if(origin) {
        tape_gap_result_t result=before(context,origin==UINT64_MAX ? origin:origin+1,&current);
        if(result!=TAPE_GAP_FOUND && result!=TAPE_GAP_NONE)return result;
        if(result==TAPE_GAP_NONE)current=0;
    }
    if(!previous){*target=current;return TAPE_GAP_FOUND;}
    if(!current)return TAPE_GAP_NONE;
    tape_gap_result_t result=before(context,current,target);
    if(result==TAPE_GAP_NONE){*target=0;return TAPE_GAP_FOUND;}
    return result;
}
tape_ui_action_t tape_contents_event(tape_contents_t *v,const tape_index_t *idx,btn_event_t e,
                                     uint32_t now,const tape_transport_view_t *tx) {
    if(v->page==TAPE_UI_SCAN) {
        if(e==BTN_STOP || e==BTN_STOP_LONG)return TAPE_ACTION_CANCEL_SCAN;
        return TAPE_ACTION_NONE;
    }
    if(v->page==TAPE_UI_LIST) {
        unsigned count=idx->program_count_recognized+1;
        if(e==BTN_NEXT)v->selected=(v->selected+1)%count;
        else if(e==BTN_PREV)v->selected=(v->selected+count-1)%count;
        else if(e==BTN_PLAY){v->start_unavailable=false;v->page=TAPE_UI_DETAIL;}
        else if(e==BTN_STOP || e==BTN_STOP_LONG)return TAPE_ACTION_CLOSE;
    } else if(v->page==TAPE_UI_DETAIL) {
        if(e==BTN_STOP || e==BTN_STOP_LONG)v->page=TAPE_UI_LIST;
        else if(e==BTN_PLAY) {
            if(v->selected && !idx->programs[v->selected-1].seekable) {
                const tape_program_t *p=&idx->programs[v->selected-1];
                if(!(p->range_flags&TAPE_RANGE_TIME_VALID) || p->chain.time_begin_us>=idx->waveform_us){v->start_unavailable=true;return TAPE_ACTION_NONE;}
                v->seek_us=p->chain.time_begin_us;v->seek_unverified=true;
                v->page=TAPE_UI_TRANSPORT;return TAPE_ACTION_START_TIME;
            }
            v->seek_unverified=false;
            v->page=TAPE_UI_TRANSPORT;return TAPE_ACTION_START;
        }
    } else if(v->page==TAPE_UI_TRANSPORT) {
        if(e==BTN_STOP || e==BTN_STOP_LONG){v->page=TAPE_UI_LIST;return TAPE_ACTION_STOP;}
        if(e==BTN_PLAY)return tx->playing ? TAPE_ACTION_PAUSE:TAPE_ACTION_RESUME;
        if(!tx->waiting && (tx->playing || tx->user_paused || tx->finished)) {
            if(e==BTN_NEXT)return TAPE_ACTION_GAP_NEXT;
            if(e==BTN_PREV)return TAPE_ACTION_GAP_PREV;
        }
        if((e==BTN_NEXT_LONG || e==BTN_PREV_LONG) && tx->playing)return TAPE_ACTION_BEGIN_SEEK;
        if((e==BTN_NEXT_LONG || e==BTN_PREV_LONG) && (tx->user_paused || tx->finished)) {
            v->seek_by_byte=!idx->duration_known || !tx->absolute_time;
            v->seek_origin_offset=tx->source_offset>idx->source.size_bytes ? idx->source.size_bytes:tx->source_offset;
            v->seek_offset=v->seek_origin_offset;
            v->page=TAPE_UI_SEEK;v->seek_origin_us=idx->duration_known && tx->tape_position_us>idx->waveform_us ? idx->waveform_us:tx->tape_position_us;v->seek_us=v->seek_origin_us;
            v->seek_origin_unverified=v->seek_unverified;
            v->held_at=now;
        } else return TAPE_ACTION_NONE;
    } else if(v->page==TAPE_UI_SEEK) {
        if(e==BTN_STOP || e==BTN_STOP_LONG){v->seek_us=v->seek_origin_us;v->seek_offset=v->seek_origin_offset;v->seek_unverified=v->seek_origin_unverified;v->page=TAPE_UI_TRANSPORT;return TAPE_ACTION_NONE;}
        if(e==BTN_PLAY){v->page=TAPE_UI_TRANSPORT;v->selected=0;return TAPE_ACTION_SEEK;}
    } else if(v->page==TAPE_UI_ERROR && (e==BTN_STOP || e==BTN_STOP_LONG))return TAPE_ACTION_CLOSE;
    if(v->page==TAPE_UI_SEEK) {
        bool forward=e==BTN_NEXT || e==BTN_NEXT_LONG || e==BTN_NEXT_REPEAT;
        bool backward=e==BTN_PREV || e==BTN_PREV_LONG || e==BTN_PREV_REPEAT;
        if(forward || backward) {
            v->seek_direction=forward ? 1:-1;
            if(e==BTN_NEXT_LONG || e==BTN_PREV_LONG)v->held_at=now;
            uint32_t held=(e==BTN_NEXT || e==BTN_PREV) ? 0:now-v->held_at;
            if(v->seek_by_byte) {
                unsigned percent=held>=6000 ? 10:held>=2000 ? 5:1;
                uint64_t size=idx->source.size_bytes;
                uint64_t step=size/100*percent+((size%100)*percent+99)/100;
                if(forward)v->seek_offset=size-v->seek_offset<step ? size:v->seek_offset+step;
                else v->seek_offset=v->seek_offset<step ? 0:v->seek_offset-step;
                v->seek_unverified=true;return TAPE_ACTION_NONE;
            }
            uint64_t step=(held>=6000 ? 60:held>=2000 ? 15:5)*1000000ULL;
            uint64_t limit=idx->duration_known ? idx->waveform_us:UINT64_MAX/16;
            if(forward)v->seek_us=limit-v->seek_us<step ? limit:v->seek_us+step;
            else v->seek_us=v->seek_us<step ? 0:v->seek_us-step;
            v->seek_unverified=true;
        }
        if(e==BTN_NEXT_RELEASE || e==BTN_PREV_RELEASE)v->seek_direction=0;
        /* Release stays paused on PLAY. One PLAY commits the target. */
    }
    return TAPE_ACTION_NONE;
}
void tape_contents_apply_transport(const tape_contents_t *v,const tape_index_t *idx,ui_transport_view_t *view) {
    if(v->page!=TAPE_UI_SEEK)return;
    /* The displayed target can be in another program. Never associate the
     * frozen live decoder or the original selection with an arbitrary time. */
    const tape_program_t *match=NULL;
    for(unsigned i=0;i<idx->program_count_recognized;++i) {
        const tape_program_t *p=&idx->programs[i];
        bool contains=v->seek_by_byte ? (p->range_flags&TAPE_RANGE_OFFSET_VALID) && v->seek_offset>=p->chain.offset_begin && v->seek_offset<p->chain.offset_end:
            (p->range_flags&TAPE_RANGE_TIME_VALID) && v->seek_us>=p->chain.time_begin_us && v->seek_us<p->chain.time_end_us;
        if(p->seekable && p->validity==TAPE_VALIDITY_VERIFIED && contains) {
            if(match){match=NULL;break;} /* Ambiguous overlapping chains. */
            match=p;
        }
    }
    program_metadata_reset(&view->program,idx->source.format);
    if(match)view->program=match->metadata;
    else view->program.confidence=PROGRAM_UNKNOWN;
    view->preview_metadata=match!=NULL;view->seek_preview=true;
    view->filename=strrchr(idx->source.path,'/');
    view->filename=view->filename ? view->filename+1:idx->source.path;
    view->status=v->seek_direction>0 ? "PAUSE FF":v->seek_direction<0 ? "PAUSE REW":"PAUSE SEEK";
    view->seconds=v->seek_by_byte ? 0:(uint32_t)(v->seek_us/1000000);
    view->total_seconds=idx->duration_known && !v->seek_by_byte ? (uint32_t)(idx->waveform_us/1000000):0;
    if(v->seek_by_byte)view->progress=idx->source.size_bytes ? (uint8_t)(v->seek_offset*100/idx->source.size_bytes):0;
    else if(idx->duration_known)view->progress=idx->waveform_us ? (uint8_t)(v->seek_us*100/idx->waveform_us):0;
    view->tx_expected=false;
    view->warning=UI_KEY_PLAY " START " UI_KEY_STOP " CANCEL";
}
void tape_contents_render(tape_contents_t *v,const tape_index_t *idx,ui_model_t *model,
                          uint32_t now,const tape_transport_view_t *tx) {
    for(unsigned i=0;i<8;++i){v->rows[i][0]=0;model->line[i]=v->rows[i];}
    char a[20],b[20];
    const char *path=strrchr(idx->source.path,'/');path=path ? path+1:idx->source.path;
    if(v->page==TAPE_UI_SCAN) {
        snprintf(v->rows[0],22,"ANALYZING %u%%",tape_analyzer_percent());
        snprintf(v->rows[1],22,"%.21s",path);
        unsigned verified=0;for(unsigned i=0;i<idx->program_count_recognized;++i)if(idx->programs[i].validity==TAPE_VALIDITY_VERIFIED)++verified;
        snprintf(v->rows[2],22,"VERIFIED %u",verified);
        snprintf(v->rows[3],22,"UNCERTAIN %u",(unsigned)(idx->program_count_recognized-verified));
        program_metadata_t m;tape_analyzer_preview(&m);
        snprintf(v->rows[4],22,"%.21s",m.title);
        snprintf(v->rows[7],22,UI_KEY_STOP " CANCEL");return;
    }
    if(v->page==TAPE_UI_LIST) {
        snprintf(v->rows[0],22,"TAPE CONTENTS %u/%u",v->selected%1000,(unsigned)(idx->program_count_recognized%1000));
        snprintf(v->rows[1],22,"%.21s",path);
        unsigned count=idx->program_count_recognized+1,first=v->selected>2 ? v->selected-2:0;
        if(count>5 && first>count-5)first=count-5;
        for(unsigned row=0;row<5 && first+row<count;++row) {
            unsigned item=first+row;const char *name=item ? idx->programs[item-1].metadata.title:"PLAY WHOLE TAPE";
            if(!name[0])name="UNKNOWN";
            size_t offset=item==v->selected ? ui_marquee_offset(&v->title_marquee,name,19,now,item):0;
            snprintf(v->rows[row+2],22,"%c%.19s",item==v->selected ? '}':' ',name+offset);
        }
        snprintf(v->rows[7],22,UI_KEY_PLAY " OPEN " UI_KEY_STOP " BACK");
        return;
    }
    if(v->page==TAPE_UI_DETAIL) {
        const tape_program_t *p=v->selected ? &idx->programs[v->selected-1]:NULL;
        const program_metadata_t *m=p ? &p->metadata:NULL;
        if(p)snprintf(v->rows[0],22,"PROGRAM %u/%u",v->selected%1000,(unsigned)(idx->program_count_recognized%1000));
        else snprintf(v->rows[0],22,"WHOLE TAPE");
        const char *name=m && m->title[0] ? m->title:path;
        size_t offset=ui_marquee_offset(&v->title_marquee,name,21,now,v->selected);
        snprintf(v->rows[1],22,"%.21s",name+offset);
        if(m) {
            snprintf(v->rows[2],22,"TYPE %.15s",program_metadata_type(m));
            snprintf(v->rows[3],22,"%.21s",m->valid&PROGRAM_RECORDED_MODE ? loader_mode_name(m->recorded_loader_mode):"LOADER --");
            if(m->valid&PROGRAM_SIZE)snprintf(v->rows[4],22,"SIZE %" PRIu32,m->declared_size);else snprintf(v->rows[4],22,"SIZE --");
            char load[5]="--",exec[5]="--";
            if(m->valid&PROGRAM_LOAD)snprintf(load,sizeof(load),"%04X",m->load);
            if(m->valid&PROGRAM_EXEC)snprintf(exec,sizeof(exec),"%04X",m->exec);
            snprintf(v->rows[5],22,"LOAD %s EXEC %s",load,exec);
            if(v->start_unavailable)snprintf(v->rows[6],22,"START --");
            else if(p->range_flags&TAPE_RANGE_TIME_VALID){timestamp(a,sizeof(a),p->chain.time_begin_us);snprintf(v->rows[6],22,"START %s%.9s",p->seekable ? "":"~",a);}
            else snprintf(v->rows[6],22,"START --");
        } else {timestamp(a,sizeof(a),idx->waveform_us);snprintf(v->rows[2],22,"DURATION %.12s",a);snprintf(v->rows[6],22,"ORIGINAL WAVEFORM");}
        snprintf(v->rows[7],22,p && !p->seekable && !(p->range_flags&TAPE_RANGE_TIME_VALID) ? UI_KEY_STOP " BACK":UI_KEY_PLAY " PLAY " UI_KEY_STOP " BACK");return;
    }
    if(v->page==TAPE_UI_ERROR) {
        snprintf(v->rows[0],22,"ANALYZER ERROR");
        snprintf(v->rows[2],22,"%s",idx->state==TAPE_INDEX_CANCELLED ? "ANALYSIS CANCELLED":"SOURCE / INDEX ERROR");
        snprintf(v->rows[7],22,UI_KEY_STOP " BACK");return;
    }
    snprintf(v->rows[0],22,"%s",tx->error ? "PLAY ERROR":tx->playing ? UI_KEY_PLAY " TAPE PLAY":tx->waiting || tx->motor_paused ? "PAUSE MOTOR":tx->user_paused ? "PAUSE USER":tx->finished ? "FINISHED":"TAPE READY");
    snprintf(v->rows[1],22,"%.21s",path);
    if(v->selected)snprintf(v->rows[2],22,"%.21s",idx->programs[v->selected-1].metadata.title);
    snprintf(v->rows[3],22,"%s",v->selected ? "PROGRAM TIME":"WHOLE TAPE TIME");
    if(tx->error)snprintf(v->rows[5],22,"%.21s",tx->error_message ? tx->error_message:"SOURCE / TX ERROR");
    else snprintf(v->rows[5],22,UI_KEY_PLAY " PAUSE " UI_KEY_STOP " LIST");
    timestamp(a,sizeof(a),tx->position_us);timestamp(b,sizeof(b),tx->total_us);
    if(strlen(a)>5 || strlen(b)>5) {
        snprintf(v->rows[6],22,"             BUF %3u%%",tx->buffer_percent>100 ? 100:tx->buffer_percent);
        snprintf(v->rows[7],22,"%.9s/%.9s",a,b);
    } else snprintf(v->rows[7],22,"%.5s/%.5s BUF %3u%%",a,b,tx->buffer_percent>100 ? 100:tx->buffer_percent);
}
