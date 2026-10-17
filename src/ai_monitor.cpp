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
#include <math.h>

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
    return true;
}

PersonDetectionResult runPersonDetection(float minimum, float margin) {
    PersonDetectionResult result;
    String& debugInfo = result.error;
    const unsigned long started = millis();
    const auto failed = [&]() {
        result.durationMs = millis() - started;
        return result;
    };
    if (!isfinite(minimum) || !isfinite(margin) || minimum < 0 || minimum > 1 ||
        margin < 0 || margin > 1) {
        debugInfo = "偵測門檻無效";
        return failed();
    }
    if (!g_modelReady) {
        debugInfo = "模型未初始化";
        Serial.println(debugInfo);
        return failed();
    }
    if (input == nullptr || interpreter == nullptr) {
        debugInfo = "interpreter 無效";
        return failed();
    }

    if (!captureGray96(gray_scratch, sizeof(gray_scratch))) {
        debugInfo = "灰階取像失敗";
        Serial.println(debugInfo);
        return failed();
    }

    int grayMean = 0, grayMin = 0, grayMax = 0;
    grayStats(gray_scratch, (size_t)kMaxImageSize, grayMean, grayMin, grayMax);
    Serial.printf("Gray stats: mean=%d min=%d max=%d\n", grayMean, grayMin, grayMax);

    // Quantize unsigned grayscale [0,255] → signed int8 for the model.
    if (input->data.raw == nullptr || input->bytes < (size_t)kMaxImageSize) {
        debugInfo = "input tensor buffer 無效";
        Serial.println(debugInfo);
        return failed();
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
        return failed();
    }

    int64_t t0 = esp_timer_get_time();
    if (interpreter->Invoke() != kTfLiteOk) {
        debugInfo = "推論 Invoke 失敗";
        Serial.println(debugInfo);
        return failed();
    }
    int64_t elapsedMs = (esp_timer_get_time() - t0) / 1000;

    TfLiteTensor* output = interpreter->output(0);
    if (output == nullptr || output->data.raw == nullptr ||
        output->bytes < ((output->type == kTfLiteFloat32) ? 2 * sizeof(float) : 2)) {
        debugInfo = "output tensor 無效";
        return failed();
    }

    float person_score_f = 0.0f;
    float no_person_score_f = 0.0f;
    int8_t person_raw = 0;
    int8_t no_person_raw = 0;
    float out_scale = 0.0f;
    int out_zp = 0;

    if (output->type == kTfLiteInt8 || output->type == kTfLiteUInt8) {
        person_raw = (int8_t)output->data.uint8[kPersonIndex];
        no_person_raw = (int8_t)output->data.uint8[kNotAPersonIndex];
        out_scale = output->params.scale;
        out_zp = output->params.zero_point;
        if (!isfinite(out_scale) || out_scale <= 0.0f) {
            debugInfo = "模型輸出比例無效";
            return failed();
        }
        const int person = output->type == kTfLiteInt8
            ? output->data.int8[kPersonIndex] : output->data.uint8[kPersonIndex];
        const int noPerson = output->type == kTfLiteInt8
            ? output->data.int8[kNotAPersonIndex] : output->data.uint8[kNotAPersonIndex];
        person_score_f = (person - out_zp) * out_scale;
        no_person_score_f = (noPerson - out_zp) * out_scale;
    } else if (output->type == kTfLiteFloat32) {
        person_score_f = output->data.f[kPersonIndex];
        no_person_score_f = output->data.f[kNotAPersonIndex];
    } else {
        debugInfo = "不支援的 output type";
        Serial.printf("%s: %d\n", debugInfo.c_str(), (int)output->type);
        return failed();
    }

    if (!isfinite(person_score_f) || !isfinite(no_person_score_f) ||
        person_score_f < 0 || person_score_f > 1 ||
        no_person_score_f < 0 || no_person_score_f > 1) {
        debugInfo = "模型輸出分數無效";
        return failed();
    }
    const bool beatMargin = (person_score_f > (no_person_score_f + margin));
    const bool aboveMin = (person_score_f >= minimum);
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
             out_zp, (double)out_scale, (double)minimum,
             (double)margin, grayMean, (long long)elapsedMs, reason);
    result.summary = buf;
    Serial.println(result.summary);

    // Compact caption for Telegram when alerting.
    if (personDetected) {
        char cap[96];
        snprintf(cap, sizeof(cap),
                 "🚨 本地偵測到有人\nperson=%.2f no_person=%.2f",
                 person_score_f, no_person_score_f);
        result.summary = cap;
    }

    result.success = true;
    result.personDetected = personDetected;
    result.personScore = person_score_f;
    result.noPersonScore = no_person_score_f;
    result.durationMs = millis() - started;
    return result;
}

bool detectPersonLocal(String& debugInfo) {
    const PersonDetectionResult result = runPersonDetection(PERSON_MIN_SCORE, PERSON_SCORE_MARGIN);
    debugInfo = result.success ? result.summary : result.error;
    return result.success && result.personDetected;
}

bool analyzePhotoForHuman(camera_fb_t* /*fb*/, String& aiResponse) {
    // Local model captures its own 96x96 grayscale frame; ignore JPEG fb.
    return detectPersonLocal(aiResponse);
}
