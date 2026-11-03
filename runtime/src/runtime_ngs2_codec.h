/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LIAN_NGS2_CODEC_H
#define LIAN_NGS2_CODEC_H
#include <stddef.h>
#include <stdint.h>

/* Stateful ATRAC9 input for the NGS2 sampler. Input may end anywhere, including
 * inside a superframe; output is interleaved float PCM. No guest pointers are
 * retained. The voice/mixer owns block lifetime, looping and sample trimming. */
typedef struct NgsAt9 NgsAt9;
typedef struct {
    unsigned channels, sample_rate, superframe_bytes, superframe_samples;
} NgsAt9Info;
NgsAt9 *ngs_at9_create(uint32_t config, unsigned channels, unsigned sample_rate);
void ngs_at9_destroy(NgsAt9 *decoder);
int ngs_at9_reset(NgsAt9 *decoder);
NgsAt9Info ngs_at9_info(const NgsAt9 *decoder);
/* Returns 0 on success, -1 for invalid arguments, -2 for codec failure. Input
 * consumption and output frames are always reported, including on failure.
 * A short final superframe stays buffered and never reaches LibAtrac9. */
int ngs_at9_process(NgsAt9 *decoder, const void *input, size_t input_bytes,
                    size_t *consumed, float *output, size_t capacity_frames,
                    size_t *written_frames);
#endif
