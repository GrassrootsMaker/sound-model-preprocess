/* SPDX-License-Identifier: Apache-2.0 */

/**
 * esp-dsp FFT backend (ESP32 / ESP-IDF).
 *
 * Build inside ESP-IDF with component dependency on esp-dsp, or:
 *   -DSF_FFT_ESP_DSP -I${IDF_PATH}/components/esp-dsp/modules/fft/include ...
 *   link libesp-dsp
 */
#include "sf_fft.h"

#include <string.h>

#include "esp_dsp.h"

typedef struct {
    int n_fft;
    float *complex_buf;
    float *twiddle;
} sf_fft_esp_state_t;

const char *sf_fft_backend_name(void) {
    return "esp-dsp";
}

size_t sf_fft_work_bytes(int n_fft) {
    /* interleaved complex (2*n_fft) + esp-dsp twiddle table (2*n_fft floats) */
    return sizeof(sf_fft_esp_state_t) + (size_t)n_fft * 4U * sizeof(float);
}

int sf_fft_init(void *work, int n_fft) {
    sf_fft_esp_state_t *st = (sf_fft_esp_state_t *)work;
    uint8_t *cursor = (uint8_t *)work + sizeof(sf_fft_esp_state_t);

    st->n_fft = n_fft;
    st->complex_buf = (float *)cursor;
    cursor += (size_t)n_fft * 2U * sizeof(float);
    st->twiddle = (float *)cursor;

    if (dsps_fft2r_init_fc32(st->twiddle, n_fft) != ESP_OK) {
        return -1;
    }
    return 0;
}

void sf_fft_power_spectrum(
    void *work,
    const float *windowed_frame,
    int n_fft,
    int n_freq_bins,
    float *power_out
) {
    sf_fft_esp_state_t *st = (sf_fft_esp_state_t *)work;
    int i;

    for (i = 0; i < n_fft; ++i) {
        st->complex_buf[2 * i] = windowed_frame[i];
        st->complex_buf[2 * i + 1] = 0.0f;
    }

    dsps_fft2r_fc32(st->complex_buf, n_fft);
    dsps_bit_rev_fc32(st->complex_buf, n_fft);
    dsps_cplx2reC_fc32(st->complex_buf, n_fft);

    for (i = 0; i < n_freq_bins; ++i) {
        const float re = st->complex_buf[2 * i];
        const float im = st->complex_buf[2 * i + 1];
        power_out[i] = re * re + im * im;
    }
}
