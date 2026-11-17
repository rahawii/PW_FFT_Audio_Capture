#ifndef AUDIO_FFT_H
#define AUDIO_FFT_H

#include <stdint.h>
#include <fftw3.h>
#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#define FFT_SIZE 1024
#define NUM_BINS (FFT_SIZE / 2 + 1)
#define SAMPLE_RATE 48000.0f

typedef struct {
    float *in_windowed;
    fftwf_complex *out_complex;
    fftwf_plan plan;
    float window[FFT_SIZE];

    float magnitudes[NUM_BINS];
    float decibels[NUM_BINS];
} FFTContext;

typedef struct {
    struct pw_main_loop *loop;
    struct pw_stream *stream;
    struct spa_audio_info format;

    float monos[FFT_SIZE];
    FFTContext fft_ctx;
    uint32_t mono_index;
} Data;

void fft_init(FFTContext *ctx);
void fft_process(FFTContext *ctx, const float *pw_monos);
float fft_bin_to_hz(int bin_index);
void fft_cleanup(FFTContext *ctx);

#endif // AUDIO_FFT_H
