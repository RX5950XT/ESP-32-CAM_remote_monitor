#include "camera.h"
#include "model_settings.h"
#include <Arduino.h>
#include <string.h>
#include "esp_jpg_decode.h"

static bool g_cameraReady = false;
static pixformat_t g_currentFormat = PIXFORMAT_JPEG;
static int g_initFailStreak = 0;
static CameraSettings g_settings;
static bool g_settingsChanged = true;
static int g_lastJpegLevels[3] = {};
static bool g_haveJpegLevels = false;

static bool applyCameraSettings(const CameraSettings& settings) {
    sensor_t* sensor = esp_camera_sensor_get();
    if (!sensor) return false;
    int err = 0;
    err |= sensor->set_brightness(sensor, settings.brightness);
    err |= sensor->set_contrast(sensor, settings.contrast);
    err |= sensor->set_saturation(sensor, settings.saturation);
    err |= sensor->set_ae_level(sensor, settings.exposureLevel);
    err |= sensor->set_gainceiling(sensor, (gainceiling_t)settings.gainCeiling);
    err |= sensor->set_quality(sensor, settings.jpegQuality);
    err |= sensor->set_aec2(sensor, 1);
    err |= sensor->set_exposure_ctrl(sensor, 1);
    err |= sensor->set_gain_ctrl(sensor, 1);
    err |= sensor->set_whitebal(sensor, 1);
    err |= sensor->set_awb_gain(sensor, 1);
    err |= sensor->set_wb_mode(sensor, 0);
    err |= sensor->set_bpc(sensor, 1);
    err |= sensor->set_wpc(sensor, 1);
    err |= sensor->set_raw_gma(sensor, 1);
    err |= sensor->set_lenc(sensor, 1);
    if (err) Serial.println("Failed to apply camera settings");
    return err == 0;
}

CameraSettings cameraSettings() { return g_settings; }

bool cameraSetSettings(const CameraSettings& settings) {
    if (settings.brightness < -2 || settings.brightness > 2 ||
        settings.contrast < -2 || settings.contrast > 2 ||
        settings.saturation < -2 || settings.saturation > 2 ||
        settings.exposureLevel < -2 || settings.exposureLevel > 2 ||
        settings.gainCeiling < GAINCEILING_2X || settings.gainCeiling > GAINCEILING_32X ||
        settings.jpegQuality < 8 || settings.jpegQuality > 20) return false;
    if (g_cameraReady && !applyCameraSettings(settings)) {
        // A partial register write must not leave an unknown active configuration.
        esp_camera_deinit();
        g_cameraReady = false;
        return false;
    }
    g_settings = settings;
    g_settingsChanged = true;
    return true;
}

static bool cameraInitWithFormat(pixformat_t format, framesize_t frameSize) {
    // Already in the requested format: only adjust framesize if possible.
    if (g_cameraReady && g_currentFormat == format) {
        if (format == PIXFORMAT_JPEG) {
            sensor_t* sensor = esp_camera_sensor_get();
            if (sensor) {
                return sensor->status.framesize == frameSize ||
                       sensor->set_framesize(sensor, frameSize) == 0;
            }
            return false;
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
    // Allocate for the largest allowed JPEG, including later MCP resolution changes.
    config.frame_size = format == PIXFORMAT_JPEG && psramFound() ? FRAMESIZE_UXGA : frameSize;
    config.grab_mode = CAMERA_GRAB_LATEST;

    if (format == PIXFORMAT_JPEG) {
        if (psramFound()) {
            config.jpeg_quality = g_settings.jpegQuality;
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
    if (!applyCameraSettings(g_settings)) {
        esp_camera_deinit();
        return false;
    }
    g_cameraReady = true;
    g_currentFormat = format;
    Serial.printf("Camera ready: format=%d framesize=%d\n", (int)format, (int)frameSize);
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor->status.framesize != frameSize && sensor->set_framesize(sensor, frameSize) != 0) {
        esp_camera_deinit();
        g_cameraReady = false;
        return false;
    }
    return true;
}

bool cameraInit() {
    // Default path: JPEG for Telegram / monitor photos.
    framesize_t size = psramFound() ? FRAMESIZE_SVGA : FRAMESIZE_CIF;
    return cameraInitWithFormat(PIXFORMAT_JPEG, size);
}

struct JpegLightSample {
    const camera_fb_t* frame;
    uint32_t sum[3] = {};
    uint32_t pixels = 0;
};

static size_t readJpegSample(void* arg, size_t index, uint8_t* buffer, size_t length) {
    const camera_fb_t* frame = ((JpegLightSample*)arg)->frame;
    if (index > frame->len) return 0;
    if (length > frame->len - index) length = frame->len - index;
    if (buffer) memcpy(buffer, frame->buf + index, length);
    return length;
}

static bool sumJpegSample(void* arg, uint16_t, uint16_t, uint16_t width, uint16_t height, uint8_t* data) {
    if (!data) return true;
    JpegLightSample& sample = *(JpegLightSample*)arg;
    const size_t pixels = (size_t)width * height;
    for (size_t i = 0; i < pixels; ++i) {
        for (int channel = 0; channel < 3; ++channel) sample.sum[channel] += data[i * 3 + channel];
    }
    sample.pixels += pixels;
    return true;
}

static bool jpegLightLevels(const camera_fb_t* frame, int levels[3]) {
    JpegLightSample sample = {};
    sample.frame = frame;
    // Decode tiles at 1/8 size; no full RGB image allocation in RAM/PSRAM.
    if (esp_jpg_decode(frame->len, JPG_SCALE_8X, readJpegSample, sumJpegSample, &sample) != ESP_OK ||
        !sample.pixels) return false;
    for (int c = 0; c < 3; ++c) levels[c] = (sample.sum[c] * 16) / sample.pixels;
    return true;
}

static camera_fb_t* captureSettledJpeg() {
    const unsigned long start = millis();
    unsigned long sampledAt = start - 500;
    int previous[3] = {-4096, -4096, -4096};
    int stable = 0;
    while (true) {
        camera_fb_t* frame = esp_camera_fb_get();
        if (!frame) return nullptr;
        if (frame->format != PIXFORMAT_JPEG) { esp_camera_fb_return(frame); return nullptr; }
        if (millis() - sampledAt >= 500) {
            int current[3];
            if (!jpegLightLevels(frame, current)) {
                esp_camera_fb_return(frame);
                Serial.println("Camera exposure sample decode failed");
                return nullptr;
            }
            bool unchanged = true;
            for (int c = 0; c < 3; ++c) unchanged &= abs(current[c] - previous[c]) <= 16;
            stable = unchanged ? stable + 1 : 0;
            memcpy(previous, current, sizeof(previous));
            sampledAt = millis();
        }
        const unsigned long elapsed = millis() - start;
        if ((elapsed >= 2000 && stable >= 3) || elapsed >= 10000) {
            Serial.printf("Camera AE/AWB %s after %lums: RGB=%d/%d/%d\n",
                          stable >= 3 ? "stable" : "time limit", elapsed,
                          previous[0] / 16, previous[1] / 16, previous[2] / 16);
            memcpy(g_lastJpegLevels, previous, sizeof(previous));
            g_haveJpegLevels = true;
            return frame;
        }
        esp_camera_fb_return(frame);
        delay(100);
    }
}

camera_fb_t* capturePhoto(framesize_t frameSize) {
    if (frameSize < FRAMESIZE_QVGA || frameSize > FRAMESIZE_UXGA) return nullptr;
    if (!psramFound() && frameSize > FRAMESIZE_CIF) frameSize = FRAMESIZE_CIF;
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

    const bool sizeChanged = sensor->status.framesize != frameSize;
    if (sizeChanged && sensor->set_framesize(sensor, frameSize) != 0) {
        Serial.println("Failed to set photo resolution");
        return nullptr;
    }

    // Fixed waits left the first image dark; wait for measured brightness/color convergence.
    const bool needsSettle = needReinit || sizeChanged || g_settingsChanged;
    const int discardCount = 3;
    const int discardDelayMs = 80;
    const int settleMs = 50;

    for (int i = 0; i < discardCount; i++) {
        camera_fb_t* discard = esp_camera_fb_get();
        if (!discard) {
            Serial.println("JPEG camera warmup failed");
            return nullptr;
        }
        esp_camera_fb_return(discard);
        delay(discardDelayMs);
    }
    delay(settleMs);

    camera_fb_t* fb = needsSettle ? captureSettledJpeg() : esp_camera_fb_get();
    if (!fb || fb->format != PIXFORMAT_JPEG) {
        if (fb) esp_camera_fb_return(fb);
        Serial.println("Camera capture failed");
        return nullptr;
    }

    if (!needsSettle && g_haveJpegLevels) {
        int current[3];
        const bool sampled = jpegLightLevels(fb, current);
        bool lightChanged = false;
        if (sampled) {
            for (int c = 0; c < 3; ++c) lightChanged |= abs(current[c] - g_lastJpegLevels[c]) > 160;
        }
        if (!sampled || lightChanged) {
            esp_camera_fb_return(fb);
            if (!sampled) { Serial.println("Camera light sample failed"); return nullptr; }
            fb = captureSettledJpeg();
            if (!fb) return nullptr;
        } else memcpy(g_lastJpegLevels, current, sizeof(current));
    }

    Serial.printf("Photo captured: %u bytes (%dx%d)\n", fb->len, fb->width, fb->height);
    g_settingsChanged = false;
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

    const bool needsSettle = needReinit || g_settingsChanged;
    const int discardCount = needsSettle ? 20 : 1;
    const int discardDelayMs = needsSettle ? 100 : 30;
    const int settleMs = needsSettle ? 100 : 30;

    for (int i = 0; i < discardCount; i++) {
        camera_fb_t* discard = esp_camera_fb_get();
        if (!discard) {
            Serial.println("Grayscale camera warmup failed");
            return false;
        }
        esp_camera_fb_return(discard);
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
    g_settingsChanged = false;
    // Intentionally do NOT restore JPEG here — capturePhoto()/cameraInit() switch on demand.
    return true;
}
