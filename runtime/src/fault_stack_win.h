/* Bounded, best-effort evidence for faults in drivers without an rbp chain.
 * These are executable pointers found on the stack, not unwound call frames. */
#ifndef LIAN_FAULT_STACK_WIN_H
#define LIAN_FAULT_STACK_WIN_H
#ifdef _WIN32
#include <windows.h>
#include <stdint.h>
#include <stddef.h>

typedef struct { uintptr_t address; size_t offset; } FaultStackCandidate;

static size_t fault_stack_candidates(uintptr_t stack, FaultStackCandidate *out, size_t capacity) {
    if (!stack || !out || !capacity) return 0;
    uintptr_t words[128];
    SIZE_T got=0;
    ReadProcessMemory(GetCurrentProcess(),(const void *)stack,words,sizeof(words),&got);
    size_t count=0;
    for (size_t i=0;i<got/sizeof(words[0]) && count<capacity;++i) {
        MEMORY_BASIC_INFORMATION memory;
        const uintptr_t address=words[i];
        if (!address || !VirtualQuery((const void *)address,&memory,sizeof(memory))) continue;
        const DWORD executable=PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY;
        if (memory.State!=MEM_COMMIT || (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
            !(memory.Protect&executable)) continue;
        HMODULE module=NULL;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)address,&module)) continue;
        out[count++]=(FaultStackCandidate){address,i*sizeof(words[0])};
    }
    return count;
}
#endif
#endif
