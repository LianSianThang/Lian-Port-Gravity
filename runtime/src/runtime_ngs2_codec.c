/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime_ngs2_codec.h"
#include "third_party/LibAtrac9/C/src/libatrac9.h"
#include <stdlib.h>
#include <string.h>

struct NgsAt9 {
    void *handle;
    unsigned char config[4];
    NgsAt9Info info;
    unsigned frames_per_superframe, samples_per_frame;
    unsigned char *compressed;
    int16_t *pcm;
    size_t fill, pcm_frames, cursor;
    int failed;
};

NgsAt9Info ngs_at9_info(const NgsAt9 *d) {
    return d ? d->info : (NgsAt9Info){0};
}

void ngs_at9_destroy(NgsAt9 *d) {
    if (!d) return;
    if (d->handle) Atrac9ReleaseHandle(d->handle);
    free(d->compressed);
    free(d->pcm);
    free(d);
}

int ngs_at9_reset(NgsAt9 *d) {
    if (!d) return -1;
    /* Reinitialize predictor/overlap state as well as the byte/sample queues. */
    d->fill=d->pcm_frames=d->cursor=0;
    /* LibAtrac9's InitDecoder does not clear the overlap buffers on a reused
     * handle. A fresh handle is required to make seeking/replay deterministic. */
    if (d->handle) Atrac9ReleaseHandle(d->handle);
    d->handle=Atrac9GetHandle();
    d->failed=!d->handle || Atrac9InitDecoder(d->handle,d->config)!=0;
    return d->failed ? -2 : 0;
}

NgsAt9 *ngs_at9_create(uint32_t config, unsigned channels, unsigned rate) {
    if (!channels || channels>8 || !rate) return NULL;
    NgsAt9 *d=calloc(1,sizeof(*d));
    if (!d) return NULL;
    /* The NGS2 config word uses the stream's network byte order. This is also
     * confirmed by the live 0x10000000 setup and local SXD WAVE metadata. */
    for (unsigned i=0;i<4;++i) d->config[i]=(unsigned char)(config>>(24-i*8));
    Atrac9CodecInfo info={0};
    if (ngs_at9_reset(d) || Atrac9GetCodecInfo(d->handle,&info) ||
        info.channels!=(int)channels || info.samplingRate!=(int)rate ||
        info.superframeSize<=0 || info.superframeSize>65536 ||
        info.framesInSuperframe<=0 || info.framesInSuperframe>4 ||
        info.frameSamples<=0 || info.frameSamples>256) {
        ngs_at9_destroy(d);
        return NULL;
    }
    d->frames_per_superframe=(unsigned)info.framesInSuperframe;
    d->samples_per_frame=(unsigned)info.frameSamples;
    d->info=(NgsAt9Info){channels,rate,(unsigned)info.superframeSize,
                       (unsigned)(info.framesInSuperframe*info.frameSamples)};
    /* The decoder's bit reader can look beyond the current codeword. Only
     * decode complete superframes, with private zero padding past their end. */
    d->compressed=calloc(1,d->info.superframe_bytes+64);
    d->pcm=calloc((size_t)d->info.superframe_samples*channels,sizeof(*d->pcm));
    if (!d->compressed || !d->pcm) { ngs_at9_destroy(d); return NULL; }
    return d;
}

static int decode_superframe(NgsAt9 *d) {
    size_t offset=0;
    for (unsigned i=0;i<d->frames_per_superframe;++i) {
        int used=0;
        int result=Atrac9Decode(d->handle,d->compressed+offset,
                               d->pcm+(size_t)i*d->samples_per_frame*d->info.channels,&used);
        if (result || used<=0 || (size_t)used>d->info.superframe_bytes-offset) {
            d->failed=1;
            return -2;
        }
        offset+=(size_t)used;
    }
    d->fill=0;
    d->cursor=0;
    d->pcm_frames=d->info.superframe_samples;
    return 0;
}

int ngs_at9_process(NgsAt9 *d, const void *input, size_t bytes, size_t *consumed,
                    float *output, size_t capacity, size_t *written) {
    if (consumed) *consumed=0;
    if (written) *written=0;
    if (!d || !consumed || !written || (bytes && !input) || (capacity && !output) ||
        capacity>SIZE_MAX/d->info.channels) return -1;
    if (d->failed) return -2;
    const unsigned char *src=input;
    while (*written<capacity) {
        if (d->cursor<d->pcm_frames) {
            size_t frames=d->pcm_frames-d->cursor;
            if (frames>capacity-*written) frames=capacity-*written;
            size_t count=frames*d->info.channels;
            const int16_t *pcm=d->pcm+d->cursor*d->info.channels;
            float *dst=output+*written*d->info.channels;
            for (size_t i=0;i<count;++i) dst[i]=(float)pcm[i]/32768.0f;
            d->cursor+=frames;
            *written+=frames;
            continue;
        }
        size_t take=d->info.superframe_bytes-d->fill;
        if (take>bytes-*consumed) take=bytes-*consumed;
        if (take) memcpy(d->compressed+d->fill,src+*consumed,take);
        d->fill+=take;
        *consumed+=take;
        if (d->fill<d->info.superframe_bytes) break;
        if (decode_superframe(d)) return -2;
    }
    return 0;
}
