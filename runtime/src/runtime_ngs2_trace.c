#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#endif

typedef uint64_t (ABI *TraceCall)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static struct {
    const char *name;
    uintptr_t target;
    _Atomic unsigned calls;
} traced[]={
    {"sceNgs2SystemCreateWithAllocator",0,0},
    {"sceNgs2RackCreateWithAllocator",0,0},
    {"sceNgs2RackCreate",0,0},
    {"sceNgs2RackQueryBufferSize",0,0},
    {"sceNgs2RackGetVoiceHandle",0,0},
    {"sceNgs2VoiceControl",0,0},
    {"sceNgs2VoiceGetState",0,0},
    {"sceNgs2SystemRender",0,0}
};

static int read_trace(void *out, uintptr_t address, size_t size) {
#ifdef _WIN32
    SIZE_T copied=0;
    return address && ReadProcessMemory(GetCurrentProcess(),(const void *)address,out,size,&copied) && copied==size;
#else
    (void)out; (void)address; (void)size;
    return 0;
#endif
}

/* The initial 128 controls are mostly volume/matrix updates. Keep a second,
 * small budget per command kind so later playback/stream events are visible.
 * Unknown kinds share a budget; neither parameter values nor pointers can
 * create an unbounded number of buckets. */
static _Atomic unsigned command_samples[64];
static int trace_late_control(uintptr_t address) {
    struct { uint16_t size; int16_t next; uint32_t id, event; } p;
    if (!read_trace(&p,address,8)) return 0;
    unsigned kind=63;
    if (p.id<=7) kind=p.id;
    if ((p.id&0xfffffff0u)==0x10000000u) kind=8+(p.id&15);
    if ((p.id&0xfffffff0u)==0x30000000u) kind=24+(p.id&15);
    if (p.id==6 && p.size>=12 && read_trace(&p,address,12)) {
        if (p.event<16) kind=40+p.event;
        else if (p.event==16) kind=56;
        else if (p.event==32) kind=57;
        else kind=58;
    }
    return atomic_fetch_add_explicit(&command_samples[kind],1,memory_order_relaxed)<4;
}

static void trace_bytes(const char *label, uintptr_t address, size_t size) {
    unsigned char bytes[64];
    if (size>sizeof(bytes)) size=sizeof(bytes);
    if (!read_trace(bytes,address,size)) {
        fprintf(stderr,"NGS2 trace: %s unreadable at %p\n",label,(void *)address);
        return;
    }
    char hex[sizeof(bytes)*2+1];
    const char digits[]="0123456789abcdef";
    for (size_t i=0;i<size;++i) {
        hex[i*2]=digits[bytes[i]>>4]; hex[i*2+1]=digits[bytes[i]&15];
    }
    hex[size*2]=0;
    fprintf(stderr,"NGS2 trace: %s @%p bytes=%s\n",label,(void *)address,hex);
}

static uint64_t trace_call(unsigned index, uintptr_t a, uintptr_t b, uintptr_t c,
                           uintptr_t d, uintptr_t e, uintptr_t f) {
    unsigned count=atomic_fetch_add_explicit(&traced[index].calls,1,memory_order_relaxed);
    const unsigned limit=index==5 ? 128 : index==4 || index==6 || index==7 ? 4 : 16;
    int saved_errno=errno;
    int log_call=count<limit;
    if (index==5) {
        int sample=trace_late_control(b);
        log_call=log_call || sample;
    }
    if (log_call) {
        fprintf(stderr,"NGS2 trace: %s #%u args=%p,%p,%p,%p,%p\n",traced[index].name,
                count+1,(void *)a,(void *)b,(void *)c,(void *)d,(void *)e);
        if (index==0) { trace_bytes("system option",a,64); trace_bytes("allocator",b,24); }
        if (index==1 || index==2) {
            trace_bytes("rack option",c,64);
            trace_bytes(index==1 ? "allocator" : "context",d,index==1 ? 24 : 64);
        }
        if (index==3) trace_bytes("rack query option",b,64);
        if (index==5) {
            uintptr_t param=b;
            for (unsigned n=0;param && n<32;++n) {
                struct { uint16_t size; int16_t next; uint32_t id; } header;
                if (!read_trace(&header,param,sizeof(header))) break;
                fprintf(stderr,"NGS2 trace: command %u id=0x%08x size=%u next=%d\n",
                        n,header.id,header.size,header.next);
                if (header.size<sizeof(header)) break;
                trace_bytes("voice param",param,header.size);
                if (header.id==1 && header.size>=24) {
                    struct { uint64_t header; uint32_t matrix,count; uintptr_t values; } p;
                    if (read_trace(&p,param,sizeof(p)))
                        trace_bytes("matrix levels",p.values,(size_t)(p.count<16 ? p.count : 16)*4);
                }
                if (header.id==0x10000006 && header.size>=24) {
                    struct { uint64_t header; uint32_t forward,release; uintptr_t points; } p;
                    if (read_trace(&p,param,sizeof(p))) {
                        uint64_t points=(uint64_t)p.forward+p.release;
                        trace_bytes("envelope points",p.points,(size_t)(points<5 ? points : 5)*12);
                    }
                }
                if (header.id==0x10000001 && header.size>=32) {
                    struct { uint64_t header, data; uint32_t flags, count; uintptr_t blocks; } p;
                    if (read_trace(&p,param,sizeof(p))) {
                        for (unsigned block=0;block<p.count && block<4;++block) {
                            struct { uint32_t offset,size,repeat,skip,samples,reserved; uint64_t user; } w;
                            uintptr_t address=p.blocks+block*sizeof(w);
                            if (!read_trace(&w,address,sizeof(w))) break;
                            trace_bytes("waveform block",address,sizeof(w));
                            trace_bytes("waveform prefix",p.data+w.offset,w.size);
                        }
                    }
                }
                if (!header.next) break;
                uintptr_t next=param+(intptr_t)header.next;
                if (next==param) break;
                param=next;
            }
        }
        if (index==7 && c) {
            for (uintptr_t n=0;n<c && n<16;++n) {
                char label[40];
                snprintf(label,sizeof(label),"render buffer %u",(unsigned)n);
                trace_bytes(label,b+n*24,24);
            }
        }
    }
    errno=saved_errno;
    uint64_t result=((TraceCall)traced[index].target)(a,b,c,d,e,f);
    saved_errno=errno;
    if (log_call) {
        fprintf(stderr,"NGS2 trace: %s returned 0x%llx\n",traced[index].name,(unsigned long long)result);
        if (index==0 || index==4) trace_bytes("output handle",c,8);
        if (index==1 || index==2) trace_bytes("output handle",e,8);
        if (index==3) trace_bytes("buffer size result",c,64);
        if (index==6 && c) trace_bytes("voice state result",b,c);
    }
    errno=saved_errno;
    return result;
}

#define WRAPPER(n) static ABI uint64_t trace_##n(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e,uintptr_t f) { return trace_call(n,a,b,c,d,e,f); }
WRAPPER(0) WRAPPER(1) WRAPPER(2) WRAPPER(3) WRAPPER(4) WRAPPER(5) WRAPPER(6) WRAPPER(7)
static TraceCall wrappers[]={trace_0,trace_1,trace_2,trace_3,trace_4,trace_5,trace_6,trace_7};

uintptr_t runtime_ngs2_trace_wrap(const char *name, uintptr_t target) {
    const char *enabled=getenv("LIAN_NGS2_TRACE");
    if (!enabled || strcmp(enabled,"1") || !name || !target) return target;
    for (size_t i=0;i<sizeof(traced)/sizeof(*traced);++i) {
        if (strcmp(name,traced[i].name)) continue;
        if (traced[i].target && traced[i].target!=target) return target;
        traced[i].target=target;
        return (uintptr_t)wrappers[i];
    }
    return target;
}
