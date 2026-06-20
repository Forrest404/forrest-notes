#include <stdio.h>
#include "audio_bsp.h"
#include "../../config.h"
#include "freertos/FreeRTOS.h"
#include "../codec_board/codec_board.h"
#include "../codec_board/codec_init.h"
#include "../esp_codec_dev/include/esp_codec_dev.h"
#include "esp_heap_caps.h"

static esp_codec_dev_handle_t playback = NULL;
static esp_codec_dev_handle_t record   = NULL;

void audio_bsp_init(void)
{
    set_codec_board_type("S3_ePaper_1_54");
    codec_init_cfg_t codec_cfg = {
        .in_mode   = CODEC_I2S_MODE_STD,
        .out_mode  = CODEC_I2S_MODE_STD,
        .in_use_tdm  = false,
        .reuse_dev   = false,
    };
    ESP_ERROR_CHECK(init_codec(&codec_cfg));
    playback = get_playback_handle();
    record   = get_record_handle();
}

void audio_play_init(void)
{
    esp_codec_dev_set_out_vol(playback, SPK_VOL_MAX);
    esp_codec_dev_set_in_gain(record, MIC_GAIN_DB);
    esp_codec_dev_sample_info_t fs = {
        .sample_rate    = SAMPLE_RATE,
        .channel        = 2,
        .bits_per_sample = 16,
    };
    esp_codec_dev_open(playback, &fs);
    esp_codec_dev_open(record, &fs);
}

void audio_playback_set_vol(uint8_t vol)
{
    esp_codec_dev_set_out_vol(playback, vol);
}

void audio_playback_read(void *data_ptr, uint32_t len)
{
    esp_codec_dev_read(record, data_ptr, len);
}

void audio_playback_write(void *data_ptr, uint32_t len)
{
    esp_codec_dev_write(playback, data_ptr, len);
}
