/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/runtime_ngs2_codec.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Optional local-data integration test: pass an extracted bgm05.sxd2. The
 * original dump is read-only; no copyrighted fixture is stored in the repo. */
int main(int argc, char **argv) {
    assert(!ngs_at9_create(0,2,48000));
    assert(!ngs_at9_create(0xfe740df0u,1,48000));
    assert(!ngs_at9_create(0xfe740df0u,2,44100));
    NgsAt9 *d=ngs_at9_create(0xfe740df0u,2,48000);
    assert(d);
    NgsAt9Info info=ngs_at9_info(d);
    assert(info.channels==2 && info.sample_rate==48000);
    size_t used=99,written=99;
    assert(ngs_at9_process(d,NULL,1,&used,NULL,0,&written)==-1);
    assert(used==0 && written==0);
    assert(ngs_at9_process(d,NULL,0,&used,NULL,0,&written)==0);
    assert(used==0 && written==0);
    printf("ATRAC9: %u bytes/superframe, %u samples/superframe\n",
           info.superframe_bytes,info.superframe_samples);
    if (argc>1) {
        FILE *f=fopen(argv[1],"rb"); assert(f);
        unsigned char header[96];
        assert(fread(header,1,sizeof(header),f)==sizeof(header));
        assert(!memcmp(header,"SXDS",4));
        size_t bytes=(size_t)info.superframe_bytes*16;
        size_t frames=(size_t)info.superframe_samples*16;
        unsigned char *input=malloc(bytes);
        float *whole=calloc(frames*2,sizeof(float)),*split=calloc(frames*2,sizeof(float));
        assert(input && whole && split);
        assert(fread(input,1,bytes,f)==bytes); fclose(f);
        assert(!ngs_at9_process(d,input,bytes,&used,whole,frames,&written));
        assert(used==bytes && written==frames);
        double energy=0; float peak=0;
        for (size_t i=0;i<frames*2;++i) {
            assert(isfinite(whole[i]));
            energy+=(double)whole[i]*whole[i];
            if (fabsf(whole[i])>peak) peak=fabsf(whole[i]);
        }
        assert(energy>0.000001 && peak<=1);
        assert(!ngs_at9_reset(d));
        size_t in_pos=0,out_pos=0;
        while (out_pos<frames) {
            size_t chunk=bytes-in_pos; if (chunk>37) chunk=37;
            size_t room=frames-out_pos; if (room>71) room=71;
            assert(!ngs_at9_process(d,input+in_pos,chunk,&used,split+out_pos*2,room,&written));
            assert(used || written);
            in_pos+=used; out_pos+=written;
        }
        assert(in_pos==bytes && !memcmp(whole,split,frames*2*sizeof(float)));
        assert(!ngs_at9_reset(d));
        assert(!ngs_at9_process(d,input,info.superframe_bytes-1,&used,split,frames,&written));
        assert(used==info.superframe_bytes-1 && written==0);
        assert(!ngs_at9_process(d,input+used,1,&used,split,frames,&written));
        assert(used==1 && written==info.superframe_samples);
        assert(!memcmp(whole,split,written*2*sizeof(float)));
        printf("PASS: local bgm05 ATRAC9 decodes nonzero audio (RMS %.6f, peak %.6f); arbitrary input/output splits and reset match; truncation waits\n",
               sqrt(energy/(frames*2)),peak);
        free(input); free(whole); free(split);
    }
    ngs_at9_destroy(d);
    puts("PASS: NGS2 codec config/channel/rate validation and empty input");
    return 0;
}
