#include "ai_monitor.h"
#include "camera.h"
#include "config.h"
#include "model_settings.h"
#include "person_detect_model_data.h"

#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>

#ifndef PERSON_SCORE_MARGIN
#define PERSON_SCORE_MARGIN 0.20f
#endif
#ifndef PERSON_MIN_SCORE
#define PERSON_MIN_SCORE 0.70f
#endif

namespace {
tflite::ErrorReporter* error_reporter = nullptr;
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
bool g_modelReady = false;

// Official example uses ~81–100 KB; allocate in PSRAM when available.
constexpr int kTensorArenaSize = 100 * 1024;
uint8_t* tensor_arena = nullptr;

// Scratch buffer for grayscale capture (uint8), converted to int8 for the model.
uint8_t gray_scratch[kMaxImageSize];

void grayStats(const uint8_t* buf, size_t n, int& meanOut, int& minOut, int& maxOut) {
    if (n == 0) {
        meanOut = minOut = maxOut = 0;
        return;
    }
    uint32_t sum = 0;
    uint8_t mn = 255;
    uint8_t mx = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t v = buf[i];
        sum += v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    meanOut = (int)(sum / n);
    minOut = (int)mn;
    maxOut = (int)mx;
}
}  // namespace

bool personDetectInit() {
    if (g_modelReady) return true;

    static tflite::MicroErrorReporter micro_error_reporter;
    error_reporter = &micro_error_reporter;

    model = tflite::GetModel(g_person_detect_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        Serial.printf("Model schema version mismatch: %lu vs %d\n",
                      (unsigned long)model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    if (tensor_arena == nullptr) {
        if (psramFound()) {
            tensor_arena = (uint8_t*)heap_caps_malloc(
                kTensorArenaSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (tensor_arena == nullptr) {
            tensor_arena = (uint8_t*)heap_caps_malloc(
                kTensorArenaSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    if (tensor_arena == nullptr) {
        Serial.printf("Failed to allocate tensor arena (%d bytes)\n", kTensorArenaSize);
        return false;
    }

    // Ops required by the official person_detection graph.
    static tflite::MicroMutableOpResolver<5> micro_op_resolver;
    micro_op_resolver.AddAveragePool2D();
    micro_op_resolver.AddConv2D();
    micro_op_resolver.AddDepthwiseConv2D();
    micro_op_resolver.AddReshape();
    micro_op_resolver.AddSoftmax();

    static tflite::MicroInterpreter static_interpreter(
        model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        Serial.println("AllocateTensors() failed");
        return false;
    }

    input = interpreter->input(0);
    if (input == nullptr) {
        Serial.println("Model input tensor is null");
        return false;
    }
    if (input->bytes != (size_t)kMaxImageSize) {
        Serial.printf("Unexpected input bytes: %d (expected %d)\n",
                      input->bytes, kMaxImageSize);
        return false;
    }
    if (input->data.raw == nullptr) {
        Serial.println("Model input data pointer is null");
        return false;
    }

    g_modelReady = true;
    Serial.printf("Local person_detection ready (model %d bytes, arena %d)\n",
                  g_person_detect_model_data_len, kTensorArenaSize);
    Serial.printf("Input type=%d dims=%d bytes=%d\n",
                  (int)input->type, input->dims ? input->dims->size : -1, input->bytes);
    Serial.printf("Detect thresholds: min_score=%.2f margin=%.2f cooldown=%ds\n",
                  (double)PERSON_MIN_SCORE, (double)PERSON_SCORE_MARGIN,
                  (int)ALERT_COOLDOWN_SEC);
    return true;
}

bool detectPersonLocal(String& debugInfo) {
    debugInfo = "";
    if (!g_modelReady) {
        debugInfo = "模型未初始化";
        Serial.println(debugInfo);
        return false;
    }
    if (input == nullptr || interpreter == nullptr) {
        debugInfo = "interpreter 無效";
        return false;
    }

    if (!captureGray96(gray_scratch, sizeof(gray_scratch))) {
        debugInfo = "灰階取像失敗";
        Serial.println(debugInfo);
        return false;
    }

    int grayMean = 0, grayMin = 0, grayMax = 0;
    grayStats(gray_scratch, (size_t)kMaxImageSize, grayMean, grayMin, grayMax);
    Serial.printf("Gray stats: mean=%d min=%d max=%d\n", grayMean, grayMin, grayMax);

    // Quantize unsigned grayscale [0,255] → signed int8 for the model.
    if (input->data.raw == nullptr || input->bytes < (size_t)kMaxImageSize) {
        debugInfo = "input tensor buffer 無效";
        Serial.println(debugInfo);
        return false;
    }
    if (input->type == kTfLiteInt8) {
        for (int i = 0; i < kMaxImageSize; i++) {
            input->data.int8[i] = (int8_t)(gray_scratch[i] ^ 0x80);
        }
    } else if (input->type == kTfLiteUInt8) {
        memcpy(input->data.uint8, gray_scratch, kMaxImageSize);
    } else {
        debugInfo = "不支援的 input type";
        Serial.printf("%s: %d\n", debugInfo.c_str(), (int)input->type);
        return false;
    }

    int64_t t0 = esp_timer_get_time();
    if (interpreter->Invoke() != kTfLiteOk) {
        debugInfo = "推論 Invoke 失敗";
        Serial.println(debugInfo);
        return false;
    }
    int64_t elapsedMs = (esp_timer_get_time() - t0) / 1000;

    TfLiteTensor* output = interpreter->output(0);
    if (output == nullptr) {
        debugInfo = "output tensor 無效";
        return false;
    }

    float person_score_f = 0.0f;
    float no_person_score_f = 0.0f;
    int8_t person_raw = 0;
    int8_t no_person_raw = 0;
    float out_scale = 0.0f;
    int out_zp = 0;

    if (output->type == kTfLiteInt8 || output->type == kTfLiteUInt8) {
        // Match official examples: read via uint8 then store as int8.
        person_raw = (int8_t)output->data.uint8[kPersonIndex];
        no_person_raw = (int8_t)output->data.uint8[kNotAPersonIndex];
        out_scale = output->params.scale;
        out_zp = output->params.zero_point;
        if (out_scale != 0.0f) {
            person_score_f = (person_raw - out_zp) * out_scale;
            no_person_score_f = (no_person_raw - out_zp) * out_scale;
        } else {
            // Fallback: map signed int8 to approx 0..1
            person_score_f = ((int)person_raw + 128) / 255.0f;
            no_person_score_f = ((int)no_person_raw + 128) / 255.0f;
        }
    } else if (output->type == kTfLiteFloat32) {
        person_score_f = output->data.f[kPersonIndex];
        no_person_score_f = output->data.f[kNotAPersonIndex];
    } else {
        debugInfo = "不支援的 output type";
        Serial.printf("%s: %d\n", debugInfo.c_str(), (int)output->type);
        return false;
    }

    const bool beatMargin =
        (person_score_f > (no_person_score_f + PERSON_SCORE_MARGIN));
    const bool aboveMin = (person_score_f >= PERSON_MIN_SCORE);
    const bool personDetected = beatMargin && aboveMin;

    const char* reason = "PERSON";
    if (!personDetected) {
        if (!beatMargin && !aboveMin) {
            reason = "no person (margin+min)";
        } else if (!beatMargin) {
            reason = "no person (margin)";
        } else {
            reason = "no person (min_score)";
        }
    }

    char buf[220];
    snprintf(buf, sizeof(buf),
             "person=%.3f no_person=%.3f raw=%d/%d zp=%d scale=%.5f "
             "min=%.2f margin=%.2f gray_mean=%d time=%lldms -> %s",
             person_score_f, no_person_score_f, (int)person_raw, (int)no_person_raw,
             out_zp, (double)out_scale, (double)PERSON_MIN_SCORE,
             (double)PERSON_SCORE_MARGIN, grayMean, (long long)elapsedMs, reason);
    debugInfo = buf;
    Serial.println(debugInfo);

    // Compact caption for Telegram when alerting.
    if (personDetected) {
        char cap[96];
        snprintf(cap, sizeof(cap),
                 "🚨 本地偵測到有人\nperson=%.2f no_person=%.2f",
                 person_score_f, no_person_score_f);
        debugInfo = cap;
    }

    return personDetected;
}

bool analyzePhotoForHuman(camera_fb_t* /*fb*/, String& aiResponse) {
    // Local model captures its own 96x96 grayscale frame; ignore JPEG fb.
    return detectPersonLocal(aiResponse);
}
