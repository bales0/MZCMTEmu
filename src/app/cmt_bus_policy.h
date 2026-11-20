#pragma once
#include "audio/player.h"
#include "audio/recorder.h"
typedef struct { bool sense_high, monitor_enabled, ul_owns_sense; } cmt_bus_outputs_t;
/* EXTERNAL disables TX only. RX capture and its monitor remain available. */
static inline cmt_bus_outputs_t cmt_bus_outputs(bool external,bool ul,bool wait_motor,
        player_state_t play,recorder_state_t record,bool tx_monitor) {
    cmt_bus_outputs_t out={true,false,false};
    if(external){out.monitor_enabled=record==RECORDER_STATE_RECORDING;return out;}
    if(ul) {out.ul_owns_sense=true;return out;}
    switch(record) {
    case RECORDER_STATE_WAIT_MOTOR:case RECORDER_STATE_PAUSED_USER:case RECORDER_STATE_PAUSED_MOTOR:
        out.sense_high=false;return out;
    case RECORDER_STATE_RECORDING:out.sense_high=false;out.monitor_enabled=true;return out;
    case RECORDER_STATE_FINALIZING:case RECORDER_STATE_FINISHED:case RECORDER_STATE_DELETED:case RECORDER_STATE_ERROR:return out;
    default:break;
    }
    out.sense_high=!(wait_motor || play==PLAYER_STATE_PLAYING || play==PLAYER_STATE_PAUSED);
    out.monitor_enabled=!wait_motor && play==PLAYER_STATE_PLAYING && tx_monitor;
    return out;
}
