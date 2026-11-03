#include <assert.h>
#include "../src/runtime_ngs2_trace.c"

static unsigned forwarded;
static ABI uint64_t original(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e,uintptr_t f) {
    assert(a==11 && b==1 && c==33 && d==44 && e==55 && f==66);
    ++forwarded;
    errno=EAGAIN;
    return UINT64_C(0x123456789abcdef0);
}

int main(void) {
    struct { uint16_t size; int16_t next; uint32_t id,event; } event={12,0,6,0};
    for (unsigned i=0;i<4;++i) assert(trace_late_control((uintptr_t)&event));
    assert(!trace_late_control((uintptr_t)&event));
    event.event=16;
    assert(trace_late_control((uintptr_t)&event));
    event.event=32;
    assert(trace_late_control((uintptr_t)&event));
    assert(!trace_late_control(1));
    _putenv_s("LIAN_NGS2_TRACE","");
    assert(runtime_ngs2_trace_wrap("sceNgs2VoiceControl",(uintptr_t)original)==(uintptr_t)original);
    _putenv_s("LIAN_NGS2_TRACE","1");
    assert(runtime_ngs2_trace_wrap("unrelated",(uintptr_t)original)==(uintptr_t)original);
    TraceCall wrapped=(TraceCall)runtime_ngs2_trace_wrap("sceNgs2VoiceControl",(uintptr_t)original);
    assert(wrapped!=original);
    assert(wrapped(11,1,33,44,55,66)==UINT64_C(0x123456789abcdef0));
    assert(forwarded==1 && errno==EAGAIN);
    assert(runtime_ngs2_trace_wrap("sceNgs2VoiceControl",(uintptr_t)original)==(uintptr_t)wrapped);
    puts("PASS: optional NGS2 trace preserves ABI arguments, return value and errno; invalid metadata read is safe; late event budgets stay independent and bounded");
    return 0;
}
