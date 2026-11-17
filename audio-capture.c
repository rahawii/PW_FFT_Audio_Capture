#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include "audio-capture.h"
#include <fftw3.h>
#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

void fft_init(FFTContext *ctx)
{
    ctx->in_windowed = fftwf_alloc_real(FFT_SIZE);
    ctx->out_complex = fftwf_alloc_complex(NUM_BINS);

    // Plan Creation: 1D, R2C
    ctx->plan = fftwf_plan_dft_r2c_1d(
        FFT_SIZE,
        ctx->in_windowed,
        ctx->out_complex,
        FFTW_ESTIMATE
    );
    
    // Hann window
    for (int i = 0; i < FFT_SIZE; i++) {
        ctx->window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
    }
}

void fft_process(FFTContext *ctx, const float *pw_monos)
{
    // Apply Hann window on samples
    for (int i = 0; i < FFT_SIZE; i++) {
        ctx->in_windowed[i] = pw_monos[i] * ctx->window[i];
    }

    fftwf_execute(ctx->plan);

    for (int k = 0; k < NUM_BINS; k++) {
        float real = ctx->out_complex[k][0];
        float imag = ctx->out_complex[k][1];

        float mag = sqrtf(real * real + imag * imag) / (float)FFT_SIZE;
        ctx->magnitudes[k] = mag;

        ctx->decibels[k] = 20.0f * log10f(mag + 1e-12f);
    }
}

// Helper
float fft_bin_to_hz(int bin_index)
{
    return (float)bin_index * (SAMPLE_RATE / (float)FFT_SIZE);
}

void fft_cleanup(FFTContext *ctx)
{
    fftwf_destroy_plan(ctx->plan);
    fftwf_free(ctx->in_windowed);
    fftwf_free(ctx->out_complex);
}

static void on_process(void *user_data)
{
    Data *data = user_data;
    FFTContext *fft_ctx = &data->fft_ctx;
    struct pw_buffer *b;
    struct spa_buffer *buf;
    float *samples;
    uint32_t n, n_channels, n_samples;
    
    if ((b = pw_stream_dequeue_buffer(data->stream)) == NULL) {
        pw_log_warn("out of buffers: %m");
        return;
    }
    buf = b->buffer;
    if ((samples = buf->datas[0].data) == NULL) {
        return;
    }

    n_channels = data->format.info.raw.channels;
    n_samples = buf->datas[0].chunk->size / sizeof(float);

    for (n = 0; n < n_samples; n += n_channels) {
        float mono = 0.0f;

        float left = samples[n];
        float right = samples[n + 1];

        mono = (left + right) * 0.5f;

        data->monos[data->mono_index++] = mono;

        if (data->mono_index == FFT_SIZE) { 
            fft_process(fft_ctx, data->monos);
            data->mono_index = 0;

            int peak_bin = 1;
            for (int k = 2; k < NUM_BINS; k++) {
                if (fft_ctx->magnitudes[k] > fft_ctx->magnitudes[peak_bin])
                    peak_bin = k;
            }
            fprintf(stdout, "Peak Freq: %6.1f Hz | Mag: %f | dB: %6.1f dB\r",
                    fft_bin_to_hz(peak_bin),
                    fft_ctx->magnitudes[peak_bin],
                    fft_ctx->decibels[peak_bin]);
            fflush(stdout);
        }
    }

    pw_stream_queue_buffer(data->stream, b);
}

static void on_stream_param_change(void *_data, uint32_t id, const struct spa_pod *param)
{
    Data *data = _data;

    if (param == NULL || id != SPA_PARAM_Format) {
        return;
    }
    if (spa_format_parse(param, &data->format.media_type, &data->format.media_subtype) < 0) {
        return;
    }

    if (data->format.media_type != SPA_MEDIA_TYPE_audio ||
        data->format.media_subtype != SPA_MEDIA_SUBTYPE_raw)
        return;

    spa_format_audio_raw_parse(param, &data->format.info.raw);

    fprintf(stdout, "capturing rate: %d channels: %d\n",
            data->format.info.raw.rate, data->format.info.raw.channels);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .param_changed = on_stream_param_change,
    .process = on_process,
};

static void do_quit(void *userdata, int signal_number)
{
    Data *data = userdata;
    printf("\n\n");
    pw_main_loop_quit(data->loop);
}

int main(int argc, char **argv)
{ 
    Data data = {0};
    const struct spa_pod *params[1];
    uint32_t n_params = 0;
    uint8_t buffer[1024];
    struct pw_properties *props;
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    pw_init(&argc, &argv);
    fft_init(&data.fft_ctx);

    /* make a main loop. If you already have another main loop, you can add
     * the fd of this pipewire mainloop to it. */
    data.loop = pw_main_loop_new(NULL);
    
    pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGINT, do_quit, &data);
    pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGTERM, do_quit, &data);

    props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                            PW_KEY_MEDIA_CATEGORY, "Capture",
                            PW_KEY_MEDIA_ROLE, "Music",
                            NULL);
    if (argc > 1) {
        pw_properties_set(props, PW_KEY_TARGET_OBJECT, argv[1]);
    }

    data.stream = pw_stream_new_simple(
        pw_main_loop_get_loop(data.loop),
        "audio-capture",
        props,
        &stream_events,
        &data);

    params[n_params++] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32));

    pw_stream_connect(data.stream, PW_DIRECTION_INPUT, PW_ID_ANY, PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS, params, n_params);

    pw_main_loop_run(data.loop);

    fft_cleanup(&data.fft_ctx);
    pw_stream_destroy(data.stream);
    pw_main_loop_destroy(data.loop);
    pw_deinit();

    return 0;
}
