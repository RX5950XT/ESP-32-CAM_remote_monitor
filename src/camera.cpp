#include "camera.h"
#include "model_settings.h"
#include <Arduino.h>
#include <string.h>

static bool g_cameraReady = false;
static pixformat_t g_currentFormat = PIXFORMAT_JPEG;
static int g_initFailStreak = 0;

static void applyLowLightSettings() {
    sensor_t* sensor = esp_camera_sensor_get();
    if (!sensor) return;

    sensor->set_brightness(sensor, 1);
    sensor->set_contrast(sensor, 1);
    sensor->set_saturation(sensor, 0);
    sensor->set_aec2(sensor, 1);
    sensor->set_ae_level(sensor, 2);
    sensor->set_exposure_ctrl(sensor, 1);
    sensor->set_gain_ctrl(sensor, 1);
    sensor->set_gainceiling(sensor, (gainceiling_t)6);
}

static bool cameraInitWithFormat(pixformat_t format, framesize_t frameSize) {
    // Already in the requested format: only adjust framesize if possible.
    if (g_cameraReady && g_currentFormat == format) {
        if (format == PIXFORMAT_JPEG) {
            sensor_t* sensor = esp_camera_sensor_get();
            if (sensor) {
                sensor->set_framesize(sensor, frameSize);
            }
        }
        return true;
    }

    if (g_cameraReady) {
        esp_camera_deinit();
        g_cameraReady = false;
        delay(50);
    }

    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 10000000;
    config.pixel_format = format;
    config.frame_size = frameSize;
    config.grab_mode = CAMERA_GRAB_LATEST;

    if (format == PIXFORMAT_JPEG) {
        if (psramFound()) {
            config.jpeg_quality = 10;
            config.fb_count = 2;
            config.fb_location = CAMERA_FB_IN_PSRAM;
        } else {
            config.jpeg_quality = 12;
            config.fb_count = 1;
            config.fb_location = CAMERA_FB_IN_DRAM;
        }
    } else {
        // Grayscale / RGB for local inference — keep frame buffer small.
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed (fmt=%d): 0x%x\n", (int)format, err);
        g_initFailStreak++;
        if (g_initFailStreak >= 5) {
            Serial.println("Camera init failed 5 times, restarting...");
            delay(1000);
            ESP.restart();
        }
        return false;
    }

    g_initFailStreak = 0;
    applyLowLightSettings();
    g_cameraReady = true;
    g_currentFormat = format;
    Serial.printf("Camera ready: format=%d framesize=%d\n", (int)format, (int)frameSize);
    return true;
}

bool cameraInit() {
    // Default path: JPEG for Telegram / monitor photos.
    framesize_t size = psramFound() ? FRAMESIZE_SVGA : FRAMESIZE_CIF;
    return cameraInitWithFormat(PIXFORMAT_JPEG, size);
}

camera_fb_t* capturePhoto(framesize_t frameSize) {
    const bool needReinit = !g_cameraReady || g_currentFormat != PIXFORMAT_JPEG;
    if (needReinit) {
        if (!cameraInit()) {
            Serial.println("Failed to enter JPEG camera mode");
            return nullptr;
        }
    }

    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor == nullptr) {
        Serial.println("Failed to get camera sensor");
        return nullptr;
    }

    sensor->set_framesize(sensor, frameSize);

    // After format switch, settle AE longer; same-format continuous shots need less.
    const int discardCount = needReinit ? 3 : 1;
    const int discardDelayMs = needReinit ? 100 : 30;
    const int settleMs = needReinit ? 400 : 50;

    for (int i = 0; i < discardCount; i++) {
        camera_fb_t* discard = esp_camera_fb_get();
        if (discard) {
            esp_camera_fb_return(discard);
        }
        delay(discardDelayMs);
    }
    delay(settleMs);

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return nullptr;
    }

    Serial.printf("Photo captured: %u bytes (%dx%d)\n", fb->len, fb->width, fb->height);
    return fb;
}

bool captureGray96(uint8_t* outBuf, size_t outBufLen) {
    if (!outBuf || outBufLen < (size_t)kMaxImageSize) {
        Serial.println("captureGray96: invalid buffer");
        return false;
    }

    const bool needReinit =
        !g_cameraReady || g_currentFormat != PIXFORMAT_GRAYSCALE;

    // Switch to grayscale only when not already in that mode.
    // Leave gray mode after capture (lazy restore) so continuous AI avoids thrashing.
    if (needReinit) {
        if (!cameraInitWithFormat(PIXFORMAT_GRAYSCALE, FRAMESIZE_96X96)) {
            Serial.println("Failed to enter grayscale camera mode");
            return false;
        }
    }

    const int discardCount = needReinit ? 5 : 1;
    const int discardDelayMs = needReinit ? 80 : 30;
    const int settleMs = needReinit ? 200 : 30;

    for (int i = 0; i < discardCount; i++) {
        camera_fb_t* discard = esp_camera_fb_get();
        if (discard) {
            esp_camera_fb_return(discard);
        }
        delay(discardDelayMs);
    }
    delay(settleMs);

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Grayscale capture failed");
        return false;
    }

    if (fb->format != PIXFORMAT_GRAYSCALE) {
        Serial.printf("Unexpected pixel format: %d\n", (int)fb->format);
        esp_camera_fb_return(fb);
        return false;
    }

    if (fb->width == kNumCols && fb->height == kNumRows) {
        size_t n = fb->len < (size_t)kMaxImageSize ? fb->len : (size_t)kMaxImageSize;
        memcpy(outBuf, fb->buf, n);
        if (n < (size_t)kMaxImageSize) {
            memset(outBuf + n, 0, (size_t)kMaxImageSize - n);
        }
    } else if (fb->width > kNumCols && fb->height > kNumRows) {
        // Box-downsample larger grayscale frame to 96x96.
        for (int y = 0; y < kNumRows; y++) {
            int y0 = (y * fb->height) / kNumRows;
            int y1 = ((y + 1) * fb->height) / kNumRows;
            if (y1 <= y0) y1 = y0 + 1;
            for (int x = 0; x < kNumCols; x++) {
                int x0 = (x * fb->width) / kNumCols;
                int x1 = ((x + 1) * fb->width) / kNumCols;
                if (x1 <= x0) x1 = x0 + 1;
                uint32_t sum = 0;
                int count = 0;
                for (int sy = y0; sy < y1; sy++) {
                    for (int sx = x0; sx < x1; sx++) {
                        sum += fb->buf[sy * fb->width + sx];
                        count++;
                    }
                }
                outBuf[y * kNumCols + x] = (uint8_t)(sum / (count > 0 ? count : 1));
            }
        }
    } else {
        int startx = (fb->width - kNumCols) / 2;
        int starty = (fb->height - kNumRows) / 2;
        if (startx < 0) startx = 0;
        if (starty < 0) starty = 0;

        int post = 0;
        for (int y = 0; y < kNumRows; y++) {
            for (int x = 0; x < kNumCols; x++) {
                int srcY = starty + y;
                int srcX = startx + x;
                if (srcY >= fb->height) srcY = fb->height - 1;
                if (srcX >= fb->width) srcX = fb->width - 1;
                outBuf[post++] = fb->buf[srcY * fb->width + srcX];
            }
        }
    }

    Serial.printf("Gray96 captured: %dx%d len=%u reinit=%d (stay gray until JPEG needed)\n",
                  fb->width, fb->height, fb->len, needReinit ? 1 : 0);
    esp_camera_fb_return(fb);
    // Intentionally do NOT restore JPEG here — capturePhoto()/cameraInit() switch on demand.
    return true;
}
