set(cmt_tx_source "audio/raw_pulse_rmt.c")
if(CONFIG_MZ_TX_GPTIMER_FALLBACK)
    set(cmt_tx_source "audio/raw_pulse.c")
endif()

set(cmt_capture_source "audio/edge_capture.c")
if(CONFIG_MZ_CAPTURE_GPIO_FALLBACK)
    set(cmt_capture_source "audio/edge_capture_gpio.c")
endif()

idf_component_register(
    SRCS
        "main.c"
        "storage/fs.c"
        "drivers/i2c.c"
        "drivers/oled.c"
        "drivers/sdcard.c"
        "drivers/buttons.c"
        "ui/ui.c"
        "audio/raw_timer.c"
        ${cmt_tx_source}
        "audio/wav_reader.c"
        "audio/wav_pulse_stream.c"
        "audio/mzf_stream.c"
        "audio/edge_stream.c"
        "audio/tap_stream.c"
        "audio/sharp_stream.c"
        "formats/quickdisk.c"
        "formats/file_format.c"
        "formats/mz_tape_profiles.c"
        "formats/mz_loader_profiles.c"
        "formats/sidecar.c"
        "play/mzf_loader.c"
        "play/mz700_fast3.c"
        "config/settings_codec.c"
        "config/nvs_settings.c"
        "app/route_switch.c"
        "audio/player.c"
        "audio/raw_out.c"
        "file_list.c"
        "audio/recorder.c"
        "audio/mzf_writer.c"
        "formats/mz_tape_decoder.c"
        "formats/mz_title.c"
        "record/record_autoname.c"
        "record/record_rename.c"
        "gpio_raw.c"
        "audio/wav_writer.c"
        "audio/edge_writer.c"
        ${cmt_capture_source}
        "audio/edge_capture_rmt.c"
        "audio/raw_capture.c"

    INCLUDE_DIRS
        "."
        "drivers"
        "ui"
        "audio"
)
