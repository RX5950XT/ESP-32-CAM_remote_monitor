#pragma once
#include "esp_camera.h"
#include <array>
#include <vector>
enum jpg_scale_t { JPG_SCALE_8X };
using jpg_reader_cb = size_t (*)(void*,size_t,uint8_t*,size_t);
using jpg_writer_cb = bool (*)(void*,uint16_t,uint16_t,uint16_t,uint16_t,uint8_t*);
inline bool failJpegDecode=false, alternatingJpegLight=false;
inline size_t jpegSampleIndex=0;
inline std::vector<std::array<uint8_t,3>> jpegLightSequence;
inline std::array<uint8_t,3> lastJpegLight;
inline esp_err_t esp_jpg_decode(size_t length,jpg_scale_t,jpg_reader_cb read,jpg_writer_cb write,void* context) {
    if(failJpegDecode) return -1;
    uint8_t buffer[4];
    if(read(context,0,buffer,4)!=4 || read(context,length+1,nullptr,4)!=0) return -1;
    lastJpegLight={100,100,100};
    if(alternatingJpegLight) lastJpegLight.fill(jpegSampleIndex%2?180:20);
    else if(!jpegLightSequence.empty()) lastJpegLight=jpegLightSequence[std::min(jpegSampleIndex,jpegLightSequence.size()-1)];
    ++jpegSampleIndex;
    return write(context,0,0,1,1,lastJpegLight.data())?ESP_OK:-1;
}
