#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "timing.h"

#define DR_WAV_IMPLEMENTATION

#include "dr_wav.h"
#include "vad/include/vad.h"

#include <emscripten/bind.h>
using namespace emscripten;

#ifndef nullptr
#define nullptr 0
#endif

#ifndef MIN
#define  MIN(A, B)        ((A) < (B) ? (A) : (B))
#endif

#ifndef MAX
#define  MAX(A, B)        ((A) > (B) ? (A) : (B))
#endif


// wav 파일 읽기
int16_t *wavRead_int16(char *filename, uint32_t *sampleRate, uint64_t *totalSampleCount) {
    unsigned int channels;
    int16_t *buffer = drwav_open_file_and_read_pcm_frames_s16(filename, &channels, sampleRate, totalSampleCount, NULL);
    if (buffer == nullptr) {
        printf("wav 파일을 읽을 수 없음.");
    }
    // 단일 채널 오디오만 처리
    if (channels != 1) {
        drwav_free(buffer, NULL);
        buffer = nullptr;
        *sampleRate = 0;
        *totalSampleCount = 0;
    }
    return buffer;
}


int vadProcess(int16_t *buffer, uint32_t sampleRate, size_t samplesCount, int16_t vad_mode, int per_ms_frames) {
    if (buffer == nullptr) return -1;
    if (samplesCount == 0) return -1;
    // kValidRates : 8000, 16000, 32000, 48000
    // 10, 20 or 30 ms frames
    per_ms_frames = MAX(MIN(30, per_ms_frames), 10);
    size_t samples = sampleRate * per_ms_frames / 1000;
    if (samples == 0) return -1;
    int16_t *input = buffer;
    size_t nCount = (samplesCount / samples);

    size_t sum = 0;

    VadInst *vadInst = WebRtcVad_Create();
    if (vadInst == NULL) return -1;
    int status = WebRtcVad_Init(vadInst);
    if (status != 0) {
        printf("WebRtcVad_Init fail\n");
        WebRtcVad_Free(vadInst);
        return -1;
    }
    status = WebRtcVad_set_mode(vadInst, vad_mode);
    if (status != 0) {
        printf("WebRtcVad_set_mode fail\n");
        WebRtcVad_Free(vadInst);
        return -1;
    }
    printf("Activity : \n");
    for (size_t i = 0; i < nCount; i++) {
        int nVadRet = WebRtcVad_Process(vadInst, sampleRate, input, samples);
        if (nVadRet == -1) {
            printf("failed in WebRtcVad_Process\n");
            WebRtcVad_Free(vadInst);
            return -1;
        } else {
            // output result
            printf(" %d \t", nVadRet);
            sum += nVadRet;
        }
        input += samples;
    }
    printf("\n");
    printf("sum: %d, nCount: %d\n", sum, nCount);
    printf("voice probability: %f %%\n", 100 * (double)sum / (double)nCount);
    WebRtcVad_Free(vadInst);
    return 1;
}

void vad(char *in_file) {
    // 오디오 샘플링 속도
    uint32_t sampleRate = 0;
    // 총 오디오 샘플링 수
    uint64_t inSampleCount = 0;
    int16_t *inBuffer = wavRead_int16(in_file, &sampleRate, &inSampleCount);
    // 불러오면
    if (inBuffer != nullptr) {
        // Aggressiveness mode (0, 1, 2, or 3)
        int16_t mode = 1;
        int per_ms = 30;
        double startTime = now();
        vadProcess(inBuffer, sampleRate, inSampleCount, mode, per_ms);
        // double time_interval = calcElapsed(startTime, now());
        // printf("time interval: %d ms\n ", (int) (time_interval * 1000));
        free(inBuffer);
    }
}



int vadProcess_wasm(int16_t *buffer, uint32_t sampleRate, size_t samplesCount, int16_t vad_mode, int per_ms_frames, int* voice) {
    if (buffer == nullptr) return -1;
    if (samplesCount == 0) return -1;
    // kValidRates : 8000, 16000, 32000, 48000
    // 10, 20 or 30 ms frames
    per_ms_frames = MAX(MIN(30, per_ms_frames), 10);
    size_t samples = sampleRate * per_ms_frames / 1000;
    if (samples == 0) return -1;
    int16_t *input = buffer;
    size_t nCount = (samplesCount / samples);

    size_t sum = 0;

    VadInst *vadInst = WebRtcVad_Create();
    if (vadInst == NULL) return -1;
    int status = WebRtcVad_Init(vadInst);
    if (status != 0) {
        printf("WebRtcVad_Init fail\n");
        WebRtcVad_Free(vadInst);
        return -1;
    }
    status = WebRtcVad_set_mode(vadInst, vad_mode);
    if (status != 0) {
        printf("WebRtcVad_set_mode fail\n");
        WebRtcVad_Free(vadInst);
        return -1;
    }
    // printf("Activity : \n");
    for (size_t i = 0; i < nCount; i++) {
        int nVadRet = WebRtcVad_Process(vadInst, sampleRate, input, samples);
        if (nVadRet == -1) {
            printf("failed in WebRtcVad_Process\n");
            WebRtcVad_Free(vadInst);
            return -1;
        } else {
            // output result
            // printf(" %d \t", nVadRet);
            sum += nVadRet;
        }
        input += samples;
    }
    // printf("\n");
    // printf("sum: %d, nCount: %d\n", sum, nCount);
    // printf("voice probability: %f %%\n", 100 * (double)sum / (double)nCount);
    *voice = sum;
    WebRtcVad_Free(vadInst);
    return 1;
}

int vad_wasm(val jsArray) {
    // 오디오 샘플링 속도
    uint32_t sampleRate = 0;
    // 총 오디오 샘플링 수
    uint64_t inSampleCount = 0;

    int voice;

    std::vector<int16_t> buffer = convertJSArrayToNumberVector<int16_t>(jsArray);
    FILE* pFile = fopen("audio.wav", "w");
    fwrite(&buffer[0], sizeof(std::vector<int16_t>), buffer.size(), pFile);
    fclose(pFile);
    int16_t *inBuffer = wavRead_int16("audio.wav", &sampleRate, &inSampleCount);
    // sampleRate = 48000;
    // inSampleCount = 480;
    // int16_t *inBuffer = &buffer[0];

    // 불러오면
    if (inBuffer != nullptr) {
        // Aggressiveness mode (0, 1, 2, or 3)
        int16_t mode = 3;
        // int per_ms = 30;
        int per_ms = 10;
        double startTime = now();
        vadProcess_wasm(inBuffer, sampleRate, inSampleCount, mode, per_ms, &voice);
        free(inBuffer);
    }

    return voice;
}

EMSCRIPTEN_BINDINGS(module) {
    function("vad_wasm", vad_wasm);
}

// int main(int argc, char *argv[]) {
//     printf("WebRTC Voice Activity Detector\n");
//     printf("blog:http://cpuimage.cnblogs.com/\n");
//     printf("usage : vad speech.wav\n");
//     if (argc < 2)
//         return -1;
//     char *in_file = argv[1];
//     vad(in_file);
//     // printf("press any key to exit. \n");
//     // getchar();
//     return 0;
// }
