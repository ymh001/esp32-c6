#include "board_internal.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

// Pin/slot configuration follows Waveshare's C6 2.16 BoxAudioCodec.
static i2s_chan_handle_t tx, rx;
static esp_codec_dev_handle_t mic, speaker;
static bool attempted, ready;
static bool mic_open, speaker_open;

esp_err_t board_audio_init(void)
{
    if (attempted) return ready ? ESP_OK : ESP_FAIL;
    attempted = true;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 4;
    chan.dma_frame_num = 160;
    chan.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chan, &tx, &rx);
    if (err != ESP_OK) return err;
    i2s_std_config_t out = {};
    out.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
    out.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    out.gpio_cfg.mclk = GPIO_NUM_19; out.gpio_cfg.bclk = GPIO_NUM_20;
    out.gpio_cfg.ws = GPIO_NUM_22; out.gpio_cfg.dout = GPIO_NUM_23;
    out.gpio_cfg.din = I2S_GPIO_UNUSED;
    err = i2s_channel_init_std_mode(tx, &out);
    if (err != ESP_OK) return err;
    i2s_tdm_config_t in = {};
    in.clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(16000);
    in.slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
        (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3));
    in.gpio_cfg.mclk = GPIO_NUM_19; in.gpio_cfg.bclk = GPIO_NUM_20;
    in.gpio_cfg.ws = GPIO_NUM_22; in.gpio_cfg.dout = I2S_GPIO_UNUSED; in.gpio_cfg.din = GPIO_NUM_21;
    err = i2s_channel_init_tdm_mode(rx, &in);
    if (err != ESP_OK) return err;
    if ((err = i2s_channel_enable(tx)) != ESP_OK) return err;
    if ((err = i2s_channel_enable(rx)) != ESP_OK) return err;
    audio_codec_i2s_cfg_t data_cfg = {};
    data_cfg.port = I2S_NUM_0; data_cfg.rx_handle = rx; data_cfg.tx_handle = tx;
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&data_cfg);
    audio_codec_i2c_cfg_t ctrl_cfg = {};
    ctrl_cfg.port = I2C_NUM_0; ctrl_cfg.bus_handle = board_i2c_bus(); ctrl_cfg.addr = ES8311_CODEC_DEFAULT_ADDR;
    const audio_codec_ctrl_if_t *out_ctrl = audio_codec_new_i2c_ctrl(&ctrl_cfg);
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    if (!data || !out_ctrl || !gpio) return ESP_ERR_NO_MEM;
    es8311_codec_cfg_t dac = {};
    dac.ctrl_if = out_ctrl; dac.gpio_if = gpio; dac.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    dac.pa_pin = -1; dac.use_mclk = true; dac.hw_gain.pa_voltage = 5; dac.hw_gain.codec_dac_voltage = 3.3;
    const audio_codec_if_t *out_codec = es8311_codec_new(&dac);
    ctrl_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
    const audio_codec_ctrl_if_t *in_ctrl = audio_codec_new_i2c_ctrl(&ctrl_cfg);
    if (!out_codec || !in_ctrl) return ESP_FAIL;
    es7210_codec_cfg_t adc = {};
    adc.ctrl_if = in_ctrl; adc.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4;
    const audio_codec_if_t *in_codec = es7210_codec_new(&adc);
    if (!in_codec) return ESP_FAIL;
    esp_codec_dev_cfg_t dev = {};
    dev.dev_type = ESP_CODEC_DEV_TYPE_OUT; dev.codec_if = out_codec; dev.data_if = data;
    speaker = esp_codec_dev_new(&dev);
    dev.dev_type = ESP_CODEC_DEV_TYPE_IN; dev.codec_if = in_codec;
    mic = esp_codec_dev_new(&dev);
    ready = mic && speaker;
    ESP_LOGI("audio", "Codec initialization: %s", ready ? "ready" : "failed");
    return ready ? ESP_OK : ESP_FAIL;
}

void board_audio_stop(void)
{
    if (mic_open) { esp_codec_dev_close(mic); mic_open = false; }
    if (speaker_open) { esp_codec_dev_close(speaker); speaker_open = false; }
}

esp_err_t board_audio_record_start(void)
{
    esp_err_t err = board_audio_init();
    if (err != ESP_OK) return err;
    board_audio_stop();
    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16; fs.channel = 4;
    fs.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0); fs.sample_rate = 16000;
    if (esp_codec_dev_open(mic, &fs) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    mic_open = true;
    if (esp_codec_dev_set_in_channel_gain(mic, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0), 30) != ESP_CODEC_DEV_OK) {
        board_audio_stop(); return ESP_FAIL;
    }
    return ESP_OK;
}
esp_err_t board_audio_read(int16_t *samples, size_t count)
{
    if (!mic_open) return ESP_ERR_INVALID_STATE;
    return esp_codec_dev_read(mic, samples, count * sizeof(int16_t)) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}
esp_err_t board_audio_play_start(uint32_t rate)
{
    esp_err_t err = board_audio_init();
    if (err != ESP_OK) return err;
    board_audio_stop();
    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16; fs.channel = 1; fs.sample_rate = rate;
    if (esp_codec_dev_open(speaker, &fs) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    speaker_open = true;
    return esp_codec_dev_set_out_vol(speaker, 65) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}
esp_err_t board_audio_write(const int16_t *samples, size_t count)
{
    if (!speaker_open) return ESP_ERR_INVALID_STATE;
    return esp_codec_dev_write(speaker, (void *)samples, count * sizeof(int16_t)) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}
