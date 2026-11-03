/* SPDX-License-Identifier: GPL-2.0-or-later
 * Baseline adapted from gminustwo/minustwo; see NGS2_UPSTREAM.md.
 * libSceNgs2 (src/runtime_ngs2.c): the waveform parser's blocks and a sampler voice mixed through
 * a mastering voice into a render buffer, as GODZILLA drives them. */
#undef NDEBUG /* the checks are asserts; release builds define NDEBUG */
#include "../src/runtime_ngs2.c"
#include <assert.h>

uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) { (void)table; (void)count; (void)nid; return 0; }
/* One file for sceNgs2ParseWaveformFile, "/app0/long.wav": the bytes of file_data. */
static unsigned char file_data[200000];
static size_t file_size;
int64_t runtime_file_open(const char *path, int flags, int mode) { (void)flags; (void)mode; return strcmp(path,"/app0/long.wav") ? -1 : 3; }
int64_t runtime_file_pread(int fd, void *buffer, uint64_t size, int64_t offset) {
    if (fd!=3 || offset<0 || (uint64_t)offset>file_size) return -1;
    const uint64_t n=size<file_size-(uint64_t)offset ? size : file_size-(uint64_t)offset;
    memcpy(buffer,file_data+offset,n);
    return (int64_t)n;
}
int64_t runtime_file_close(int fd) { (void)fd; return 0; }

static void put32(unsigned char *p, uint32_t v) { memcpy(p,&v,4); }
static void put16(unsigned char *p, uint16_t v) { memcpy(p,&v,2); }

/* A mono 16-bit 48 kHz RIFF with `samples` samples and, when loop_end, a smpl loop. */
static size_t make_wave(unsigned char *out, const int16_t *pcm, uint32_t samples, uint32_t loop_begin, uint32_t loop_end) {
    unsigned char *p=out;
    memcpy(p,"RIFF",4); memcpy(p+8,"WAVE",4); p+=12;
    memcpy(p,"fmt ",4); put32(p+4,16); put16(p+8,1); put16(p+10,1); put32(p+12,48000); put32(p+16,96000);
    put16(p+20,2); put16(p+22,16); p+=24;
    if (loop_end) {
        memcpy(p,"smpl",4); put32(p+4,60); memset(p+8,0,60); put32(p+8+28,1); put32(p+8+36+8,loop_begin); put32(p+8+36+12,loop_end);
        p+=68;
    }
    memcpy(p,"data",4); put32(p+4,samples*2); memcpy(p+8,pcm,samples*2); p+=8+samples*2;
    put32(out+4,(uint32_t)(p-out-8));
    return (size_t)(p-out);
}

static void param(Handle voice, const void *block) { assert(voice_control(voice,block)==0); }

static unsigned allocations,releases;
static ABI int32_t test_allocate(BufferInfo *info) {
    assert(info->user==0x12345678 && info->size);
    info->buffer=calloc(1,(size_t)info->size);
    if (!info->buffer) return ERR_FAIL;
    ++allocations;
    return 0;
}
static ABI int32_t test_release(BufferInfo *info) {
    assert(info->user==0x12345678 && info->buffer);
    free(info->buffer); ++releases;
    return 0;
}
static ABI int32_t test_allocate_failure(BufferInfo *info) { (void)info; return ERR_FAIL; }

static void test_gravity(const char *path) {
    BufferAllocator allocator={test_allocate,test_release,0x12345678};
    Handle system=0,sampler=0,mastering=0,voice=0,master=0;
    assert(system_create_allocator(NULL,&allocator,&system)==0);
    unsigned char options[168]={0};
    put32(options,sizeof(options)); put32(options+32,4); put32(options+40,1); put32(options+44,8);
    assert(rack_create_allocator(system,RACK_SAMPLER,options,&allocator,&sampler)==0);
    put32(options+32,1);
    assert(rack_create_allocator(system,RACK_MASTERING,options,&allocator,&mastering)==0);
    assert(allocations==3 && releases==0);
    assert(rack_voice(sampler,0,&voice)==0 && rack_voice(mastering,0,&master)==0);
    assert(voice!=master && voice!=system);
    BufferAllocator failing={test_allocate_failure,test_release,0};
    Handle unchanged=0;
    assert(system_create_allocator(NULL,&failing,&unchanged)==ERR_FAIL && unchanged==0);
    assert(rack_create_allocator(0,RACK_SAMPLER,options,&allocator,&unchanged)==ERR_INVALID_SYSTEM_HANDLE);
    assert(allocations==3);
    /* The observed Gravity SXD sequence: data with MORE, then an empty close
     * command before playback. The queued samples must survive the close. */
    int16_t beep[512]; for (unsigned i=0;i<512;++i) beep[i]=8192;
    struct { ParamHeader h; WaveFormat f; uint32_t flags,reserved; } pcm_setup={
        {40,0,0x10000000},{WAVE_PCM_I16L,1,48000,0,0,0},0,0};
    WaveBlock beep_block={0,sizeof(beep),0,0,512,0,0};
    struct { ParamHeader h; const void *data; uint32_t flags,count; const WaveBlock *blocks; } beep_blocks={
        {32,0,0x10000001},beep,BLOCKS_STREAM,1,&beep_block};
    struct { ParamHeader h; const void *data; uint32_t flags,count; const WaveBlock *blocks; } close_blocks={
        {32,0,0x10000001},NULL,0,0,NULL};
    param(voice,&pcm_setup); param(voice,&beep_blocks); param(voice,&close_blocks);
    Voice *queued=find_voice(voice);
    assert(queued && queued->block_count==1 && queued->data==(const unsigned char *)beep && queued->stream_end);
    struct { ParamHeader h; uint32_t event; } beep_play={{12,0,PARAM_EVENT},EVENT_PLAY};
    param(voice,&beep_play);
    float beep_pcm[256]; sampler_render(queued,beep_pcm,256,48000);
    assert(beep_pcm[10]==0.25f);
    puts("PASS: Gravity empty waveform-list close preserves queued audio");
    /* Gravity supplies full successive block descriptors with flag 1. They
     * extend the stream; the first descriptor's count is not its total length. */
    param(voice,&pcm_setup);
    beep_block.samples=256; beep_block.size=256*2;
    beep_blocks.flags=BLOCKS_STREAM;
    param(voice,&beep_blocks); param(voice,&beep_play);
    sampler_render(queued,beep_pcm,256,48000);
    unsigned char streamed_state[48];
    assert(voice_state(voice,streamed_state,sizeof(streamed_state))==0);
    assert(read32(streamed_state,0)&STATE_IN_USE);
    /* Refilled storage is allowed to reuse the same address after consumption. */
    param(voice,&beep_blocks); param(voice,&close_blocks);
    sampler_render(queued,beep_pcm,256,48000);
    assert(beep_pcm[10]==0.25f);
    sampler_render(queued,beep_pcm,256,48000);
    assert(queued->decoded_samples==512 && queued->flags==0);
    puts("PASS: Gravity flag-1 stream blocks retain per-block sample counts and resume after refill");
    if (path) {
        FILE *f=fopen(path,"rb"); assert(f);
        unsigned char header[96],compressed[448*16];
        assert(fread(header,1,sizeof(header),f)==sizeof(header) && !memcmp(header,"SXDS",4));
        assert(fread(compressed,1,sizeof(compressed),f)==sizeof(compressed)); fclose(f);
        struct { ParamHeader h; WaveFormat f; uint32_t flags,reserved; } setup={
            {40,0,0x10000000},{WAVE_ATRAC9,2,48000,0xfe740df0u,0,0},0,0};
        struct { ParamHeader h; uint32_t channels,flags; } master_setup={{16,0,0x30000000},2,0};
        param(voice,&setup); param(master,&master_setup);
        WaveBlock block={0,sizeof(compressed),0,0,1024*16,0,0};
        struct { ParamHeader h; const void *data; uint32_t flags,count; const WaveBlock *blocks; } blocks={
            {32,0,0x10000001},compressed,0,1,&block};
        /* Gravity supplies a rack handle and its default input, not a voice
         * handle. This previously rejected every dry/audio bus connection. */
        struct { ParamHeader h; uint32_t port,input; Handle dest; } patch={{24,0,PARAM_PATCH},0,0,mastering};
        struct { ParamHeader h; uint32_t event; } play={{12,0,PARAM_EVENT},EVENT_PLAY};
        param(voice,&blocks); param(voice,&patch); param(master,&play); param(voice,&play);
        float pcm[256*2]; RenderBuffer render={pcm,sizeof(pcm),WAVE_PCM_F32L,2};
        size_t errors_before=decode_errors;
        double energy=0; float peak=0;
        for (unsigned grain=0;grain<66;++grain) {
            assert(system_render(system,&render,1)==0);
            for (unsigned i=0;i<256*2;++i) {
                assert(isfinite(pcm[i])); energy+=(double)pcm[i]*pcm[i]; peak=fmaxf(peak,fabsf(pcm[i]));
            }
        }
        assert(energy>0.000001 && peak>0 && peak<=1 && decode_errors==errors_before);
        unsigned char state[48]; assert(voice_state(voice,state,sizeof(state))==0);
        assert(read32(state,0)==0);
        uint64_t samples; memcpy(&samples,state+16,8); assert(samples==16384);
        printf("PASS: Gravity numeric ATRAC9 config renders 16384 samples through a rack-target patch (peak %.6f, no decode errors)\n",peak);
        /* Actual compressed bytes delivered as successive flag-1 descriptors. */
        param(voice,&setup);
        WaveBlock slices[16];
        for (unsigned i=0;i<16;++i) slices[i]=(WaveBlock){i*448,448,0,0,1024,0,0};
        blocks.flags=BLOCKS_STREAM; blocks.count=1;
        for (unsigned i=0;i<16;++i) { blocks.blocks=&slices[i]; param(voice,&blocks); }
        param(voice,&close_blocks); param(voice,&play);
        energy=0; errors_before=decode_errors;
        for (unsigned grain=0;grain<66;++grain) {
            assert(system_render(system,&render,1)==0);
            for (unsigned i=0;i<256*2;++i) energy+=(double)pcm[i]*pcm[i];
        }
        assert(energy>0.000001 && decode_errors==errors_before);
        assert(voice_state(voice,state,sizeof(state))==0 && read32(state,0)==0);
        memcpy(&samples,state+16,8); assert(samples==16384);
        puts("PASS: real ATRAC9 stream advances across sixteen flag-1 waveform blocks without decoder errors");
        /* Decoder overlap must survive aligned full descriptors and byte-only
         * continuations. Compare every output sample against an unsplit decode,
         * including a continuation boundary inside a compressed superframe. */
        static float reference[16386*2], continued[16386*2];
        param(voice,&setup);
        blocks.flags=0; blocks.count=1; blocks.blocks=&block;
        param(voice,&blocks); param(voice,&play);
        sampler_render(queued,reference,16386,48000);
        param(voice,&setup);
        blocks.flags=BLOCKS_STREAM; blocks.count=16; blocks.blocks=slices;
        param(voice,&blocks); param(voice,&close_blocks); param(voice,&play);
        sampler_render(queued,continued,16386,48000);
        assert(!memcmp(reference,continued,sizeof(reference)));
        WaveBlock fragments[MAX_BLOCKS];
        unsigned fragment_count=0;
        for (unsigned offset=0;offset<sizeof(compressed);offset+=113) {
            assert(fragment_count<MAX_BLOCKS);
            const unsigned left=(unsigned)sizeof(compressed)-offset;
            fragments[fragment_count++]=(WaveBlock){offset,left<113 ? left : 113,0,0,
                                                   offset ? 0 : 16384,0,0};
        }
        param(voice,&setup);
        blocks.flags=BLOCKS_STREAM|BLOCKS_APPEND;
        blocks.count=fragment_count; blocks.blocks=fragments;
        param(voice,&blocks); param(voice,&close_blocks); param(voice,&play);
        errors_before=decode_errors;
        sampler_render(queued,continued,16386,48000);
        assert(decode_errors==errors_before && !memcmp(reference,continued,sizeof(reference)));
        puts("PASS: real ATRAC9 aligned and 113-byte fragmented streams match unsplit PCM exactly");
        /* A trimmed full descriptor can finish mid-superframe. The next
         * descriptor starts a fresh superframe, like the captured 0x82000000
         * failure (first-frame marker with the decoder still at frame 2). */
        param(voice,&setup);
        WaveBlock trimmed[2]={{0,448,0,0,300,0,0},{448,448,0,0,1024,0,0}};
        blocks.flags=BLOCKS_STREAM; blocks.count=2; blocks.blocks=trimmed;
        param(voice,&blocks); param(voice,&close_blocks); param(voice,&play);
        errors_before=decode_errors;
        for (unsigned grain=0;grain<7;++grain) assert(system_render(system,&render,1)==0);
        assert(decode_errors==errors_before);
        assert(voice_state(voice,state,sizeof(state))==0 && read32(state,0)==0);
        memcpy(&samples,state+16,8); assert(samples==1324);
        puts("PASS: trimmed ATRAC9 block hands off to a fresh superframe without losing the next block");
    }
    assert(system_destroy(system,NULL)==0 && releases==3);
    assert(rack_voice(sampler,0,&voice)==ERR_INVALID_RACK_HANDLE);
    puts("PASS: NGS2 guest allocator lifecycle, distinct handles, failure and destroy callbacks");
}

int main(int argc, char **argv) {
    static int16_t pcm[1000];
    for (int i=0;i<1000;++i) pcm[i]=(int16_t)(i*16);
    static unsigned char wave[4096];

    /* Parsing: no loop is one block; a loop is intro, endless loop and tail, in samples of the data. */
    WaveInfo info;
    size_t size=make_wave(wave,pcm,1000,0,0);
    assert(parse_data(wave,size,&info)==0);
    assert(info.format.type==WAVE_PCM_I16L && info.format.channels==1 && info.format.rate==48000);
    assert(info.samples==1000 && info.block_count==1 && info.blocks[0].offset==info.data_offset && info.blocks[0].samples==1000);
    size=make_wave(wave,pcm,1000,100,599);
    assert(parse_data(wave,size,&info)==0 && info.block_count==3);
    assert(info.loop_begin==100 && info.loop_end==599);
    assert(info.blocks[0].samples==100 && info.blocks[0].repeats==0);
    assert(info.blocks[1].offset==info.data_offset+200 && info.blocks[1].samples==500 && info.blocks[1].repeats==INFINITE_REPEATS);
    assert(info.blocks[2].offset==info.data_offset+1200 && info.blocks[2].samples==400);
    assert(parse_data((const unsigned char *)"RIFF\0\0\0\0WAVX",12,&info)==ERR_INVALID_WAVEFORM_DATA);

    /* A file is parsed from its first 64 KiB, but its block covers the whole data chunk: GODZILLA
     * streams a voice line by its block's size (cut to 64 KiB, long lines broke off). */
    {
        static int16_t long_pcm[60000];
        for (int i=0;i<60000;++i) long_pcm[i]=(int16_t)i;
        file_size=make_wave(file_data,long_pcm,60000,0,0);
        assert(file_size>64*1024);
        assert(parse_file("/app0/long.wav",0,&info)==0);
        assert(info.samples==60000 && info.data_size==120000 && info.block_count==1);
        assert(info.blocks[0].offset==info.data_offset && info.blocks[0].size==120000 && info.blocks[0].samples==60000);
    }

    /* A system with a sampler rack (default options: SAMPLER_DEFAULT_VOICES voices) and a stereo
     * mastering voice. */
    BufferInfo buffer={0};
    Handle system=0, sampler_rack=0, mastering_rack=0, sampler=0, master=0;
    assert(system_query_size(NULL,&buffer)==0 && buffer.size);
    assert(system_create(NULL,&buffer,&system)==0);
    assert(rack_create(system,RACK_SAMPLER,NULL,&buffer,&sampler_rack)==0);
    unsigned char option[136]={0};
    put32(option,sizeof(option)); put32(option+32,1);
    assert(rack_create(system,RACK_MASTERING,option,&buffer,&mastering_rack)==0);
    assert(rack_create(system,0x9999,NULL,&buffer,&sampler_rack)==ERR_INVALID_RACK_ID);
    assert(rack_voice(sampler_rack,SAMPLER_DEFAULT_VOICES-1,&sampler)==0 &&
           rack_voice(sampler_rack,SAMPLER_DEFAULT_VOICES,&sampler)==ERR_INVALID_VOICE_INDEX);
    assert(rack_voice(sampler_rack,0,&sampler)==0 && rack_voice(mastering_rack,0,&master)==0);

    struct { ParamHeader h; uint32_t a, b; } setup={{16,0,0x30000000},2,0}, output={{16,0,0x30000005},0,0};
    struct { ParamHeader h; uint32_t event; } play_event={{12,0,PARAM_EVENT},EVENT_PLAY};
    param(master,&setup); param(master,&output); param(master,&play_event);

    size=make_wave(wave,pcm,1000,0,0);
    assert(parse_data(wave,size,&info)==0);
    struct { ParamHeader h; WaveFormat format; uint32_t flags, reserved; } sampler_setup={{40,0,0x10000000},info.format,0,0};
    struct { ParamHeader h; const void *data; uint32_t flags, count; const WaveBlock *blocks; } blocks=
        {{32,0,0x10000001},wave,0,info.block_count,info.blocks};
    struct { ParamHeader h; uint32_t port, input; Handle dest; } patch={{24,0,PARAM_PATCH},0,0,master};
    param(sampler,&sampler_setup); param(sampler,&blocks); param(sampler,&patch); param(sampler,&play_event);

    /* One render is one grain: mono to both channels, a sample late (the resampler's first step). */
    static float out[256*2];
    RenderBuffer render={out,sizeof(out),WAVE_PCM_F32L,2};
    assert(system_render(system,&render,1)==0);
    for (int f=1;f<256;++f) assert(out[2*f]==pcm[f-1]/32768.0f && out[2*f+1]==out[2*f]);

    /* Port volume (parameter 2) scales what the port sends. */
    struct { ParamHeader h; uint32_t port; float level; } volume={{16,0,PARAM_PORT_VOLUME},0,0.5f};
    param(sampler,&volume);
    assert(system_render(system,&render,1)==0);
    assert(fabsf(out[2]-0.5f*pcm[256]/32768.0f)<1e-6f);

    /* When the waveform has played the voice is free again. */
    unsigned char state[48];
    assert(voice_state(sampler,state,sizeof(state))==0 && (state[0] & STATE_PLAYING));
    for (int i=0;i<4;++i) assert(system_render(system,&render,1)==0);
    assert(voice_state(sampler,state,sizeof(state))==0 && state[0]==0);
    uint64_t decoded; memcpy(&decoded,state+16,8);
    assert(decoded==1000);

    /* A stream (GODZILLA's voice lines): played and paused before its data, then its ring buffer's
     * first part (blocks flag 1, the whole stream's sample count); it waits empty when that part is
     * played, and the part the game appends (flag 3, its own data address, the same skip and count
     * again) goes on with it until the stream's count. */
    Handle stream=0;
    assert(rack_voice(sampler_rack,1,&stream)==0);
    struct { ParamHeader h; uint32_t event; } pause_event={{12,0,PARAM_EVENT},EVENT_PAUSE},
        resume_event={{12,0,PARAM_EVENT},EVENT_RESUME};
    param(stream,&sampler_setup); param(stream,&patch); param(stream,&play_event); param(stream,&pause_event);
    assert(voice_state(stream,state,sizeof(state))==0 &&
           state[0]==(STATE_IN_USE|STATE_PLAYING|STATE_PAUSED|STATE_EMPTY));
    WaveBlock part={0,200*2,0,0,600,0,0};
    blocks.data=pcm; blocks.flags=BLOCKS_STREAM; blocks.count=1; blocks.blocks=&part;
    param(stream,&blocks); param(stream,&resume_event);
    assert(voice_state(stream,state,sizeof(state))==0 && state[0]==(STATE_IN_USE|STATE_PLAYING));
    assert(system_render(system,&render,1)==0);
    /* (The resampler needs the next sample for a frame: the wait starts one frame early, and the
     * first frame after it is the sample it held back.) */
    for (int f=1;f<200;++f) assert(out[2*f]==pcm[f-1]/32768.0f);
    for (int f=200;f<256;++f) assert(out[2*f]==0.0f);
    assert(voice_state(stream,state,sizeof(state))==0 && (state[0] & STATE_EMPTY) && (state[0] & STATE_PLAYING));
    WaveBlock next={0,400*2,0,256,600,0,0};
    blocks.data=pcm+200; blocks.flags=BLOCKS_STREAM|BLOCKS_APPEND; blocks.blocks=&next;
    param(stream,&blocks);
    assert(voice_state(stream,state,sizeof(state))==0 && !(state[0] & STATE_EMPTY));
    assert(system_render(system,&render,1)==0);
    for (int f=0;f<256;++f) assert(out[2*f]==pcm[199+f]/32768.0f);
    for (int i=0;i<2;++i) assert(system_render(system,&render,1)==0);
    assert(voice_state(stream,state,sizeof(state))==0 && state[0]==0);
    memcpy(&decoded,state+16,8);
    assert(decoded==600);

    /* Its last part (appended without the stream flag) ends it when played, even short of the
     * stream's count: waiting for more kept the voice playing and the game waiting for it. */
    param(stream,&sampler_setup); param(stream,&play_event);
    part.samples=next.samples=900;
    blocks.data=pcm; blocks.flags=BLOCKS_STREAM; blocks.blocks=&part;
    param(stream,&blocks);
    blocks.data=pcm+200; blocks.flags=BLOCKS_APPEND; blocks.blocks=&next;
    param(stream,&blocks);
    for (int i=0;i<4;++i) assert(system_render(system,&render,1)==0);
    assert(voice_state(stream,state,sizeof(state))==0 && state[0]==0);
    memcpy(&decoded,state+16,8);
    assert(decoded==600);

    /* A voice played again reports what it decoded since: a stream's reader refills its ring by the
     * decoded size, and the sizes of earlier sounds made it overwrite what was still to play. */
    WaveBlock whole={0,1000*2,0,0,1000,0,0};
    blocks.data=pcm; blocks.flags=0; blocks.blocks=&whole;
    param(stream,&blocks); param(stream,&play_event);
    assert(system_render(system,&render,1)==0);
    assert(voice_state(stream,state,sizeof(state))==0);
    uint64_t decoded_bytes;
    memcpy(&decoded,state+16,8); memcpy(&decoded_bytes,state+24,8);
    assert(decoded==256 && decoded_bytes==512);

    /* The mastering matrix and limiter: GODZILLA mixes its buses at -18 dB ({0.126, 0, 0, 0.126})
     * with the limiter at that level; without them its sound drowned the movies. */
    struct { ParamHeader h; uint32_t event; } stop_event={{12,0,PARAM_EVENT},EVENT_STOP_IMMEDIATE};
    param(stream,&stop_event);
    Handle loud=0;
    assert(rack_voice(sampler_rack,3,&loud)==0);
    static const float half[4]={0.5f,0.0f,0.0f,0.5f};
    struct { ParamHeader h; uint32_t type, count; const float *levels; } master_matrix={{24,0,0x30000001},5,4,half};
    param(master,&master_matrix);
    blocks.data=wave; blocks.flags=0; blocks.count=info.block_count; blocks.blocks=info.blocks;
    param(loud,&sampler_setup); param(loud,&blocks); param(loud,&patch); param(loud,&play_event);
    assert(system_render(system,&render,1)==0);
    for (int f=1;f<256;++f) assert(fabsf(out[2*f]-0.5f*pcm[f-1]/32768.0f)<1e-6f && out[2*f+1]==out[2*f]);
    struct { ParamHeader h; uint32_t on; float threshold; } limiter={{16,0,0x30000003},1,0.1f};
    param(master,&limiter);
    float loudest=0.0f;
    for (int i=0;i<2;++i) {
        assert(system_render(system,&render,1)==0);
        for (int k=0;k<256*2;++k) loudest=fmaxf(loudest,fabsf(out[k]));
    }
    assert(loudest>0.09f && loudest<=0.1f+1e-6f);

    /* Destroying hands the game's buffers back. */
    BufferInfo back={0};
    assert(rack_destroy(sampler_rack,&back)==0 && system_destroy(system,&back)==0);
    assert(system_render(system,&render,1)==ERR_INVALID_SYSTEM_HANDLE);
    test_gravity(argc>1 ? argv[1] : NULL);
    puts("ngs2 test: ok");
    return 0;
}
