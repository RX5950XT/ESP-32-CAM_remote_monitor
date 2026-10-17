#pragma once
#include "Arduino.h"
enum pixformat_t { PIXFORMAT_JPEG, PIXFORMAT_GRAYSCALE };
enum framesize_t { FRAMESIZE_96X96, FRAMESIZE_QQVGA, FRAMESIZE_QVGA, FRAMESIZE_CIF,
                  FRAMESIZE_VGA, FRAMESIZE_SVGA, FRAMESIZE_XGA, FRAMESIZE_UXGA };
enum gainceiling_t { GAINCEILING_2X, GAINCEILING_4X, GAINCEILING_8X, GAINCEILING_16X, GAINCEILING_32X };
constexpr int LEDC_CHANNEL_0=0, LEDC_TIMER_0=0, CAMERA_GRAB_LATEST=0;
constexpr int CAMERA_FB_IN_PSRAM=0, CAMERA_FB_IN_DRAM=1, ESP_OK=0;
using esp_err_t = int;
struct camera_config_t {
    int ledc_channel,ledc_timer,pin_d0,pin_d1,pin_d2,pin_d3,pin_d4,pin_d5,pin_d6,pin_d7;
    int pin_xclk,pin_pclk,pin_vsync,pin_href,pin_sccb_sda,pin_sccb_scl,pin_pwdn,pin_reset;
    int xclk_freq_hz,grab_mode,jpeg_quality,fb_count,fb_location;
    pixformat_t pixel_format;
    framesize_t frame_size;
};
struct camera_fb_t { uint8_t* buf; size_t len; size_t width,height; pixformat_t format; };
struct sensor_t {
    struct { framesize_t framesize; } status;
    int (*set_framesize)(sensor_t*,framesize_t);
    int (*set_brightness)(sensor_t*,int), (*set_contrast)(sensor_t*,int), (*set_saturation)(sensor_t*,int);
    int (*set_ae_level)(sensor_t*,int), (*set_quality)(sensor_t*,int), (*set_aec2)(sensor_t*,int);
    int (*set_exposure_ctrl)(sensor_t*,int), (*set_gain_ctrl)(sensor_t*,int), (*set_whitebal)(sensor_t*,int);
    int (*set_awb_gain)(sensor_t*,int), (*set_wb_mode)(sensor_t*,int), (*set_bpc)(sensor_t*,int);
    int (*set_wpc)(sensor_t*,int), (*set_raw_gma)(sensor_t*,int), (*set_lenc)(sensor_t*,int);
    int (*set_gainceiling)(sensor_t*,gainceiling_t);
};
sensor_t* esp_camera_sensor_get();
esp_err_t esp_camera_init(const camera_config_t*);
esp_err_t esp_camera_deinit();
camera_fb_t* esp_camera_fb_get();
void esp_camera_fb_return(camera_fb_t*);
