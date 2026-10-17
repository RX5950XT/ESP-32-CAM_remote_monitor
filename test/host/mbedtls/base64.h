#pragma once
#include <cstddef>
inline int mbedtls_base64_encode(unsigned char* out,size_t capacity,size_t* written,
                                 const unsigned char* data,size_t size) {
    const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    *written=0;
    if(capacity <= ((size+2)/3)*4) return -1;
    for(size_t i=0;i<size;i+=3) {
        unsigned value=unsigned(data[i])<<16;
        if(i+1<size) value|=unsigned(data[i+1])<<8;
        if(i+2<size) value|=data[i+2];
        out[(*written)++]=alphabet[(value>>18)&63];
        out[(*written)++]=alphabet[(value>>12)&63];
        out[(*written)++]=i+1<size?alphabet[(value>>6)&63]:'=';
        out[(*written)++]=i+2<size?alphabet[value&63]:'=';
    }
    return 0;
}
