// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
/* Minimal libSceFiber HLE for the Gravity Rush import set.
 * Context layout and switching semantics follow shadPS4's fiber implementation.
 * This uses the PS4 SysV ABI throughout; Windows fibers are intentionally not used.
 */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#if !defined(__x86_64__)
#error "The PS4 Fiber context implementation requires x86-64."
#endif

#define FIBER_ERR_NULL      ((int32_t)UINT32_C(0x80590001))
#define FIBER_ERR_ALIGNMENT ((int32_t)UINT32_C(0x80590002))
#define FIBER_ERR_RANGE     ((int32_t)UINT32_C(0x80590003))
#define FIBER_ERR_INVALID   ((int32_t)UINT32_C(0x80590004))
#define FIBER_ERR_PERMISSION ((int32_t)UINT32_C(0x80590005))
#define FIBER_ERR_STATE     ((int32_t)UINT32_C(0x80590006))

#define FIBER_MAGIC_START UINT32_C(0xdef1649c)
#define FIBER_MAGIC_END   UINT32_C(0xb37592a0)
#define FIBER_OPT_MAGIC   UINT32_C(0xbb40e64d)
#define FIBER_STACK_MAGIC UINT64_C(0x7149f2ca7149f2ca)
#define FIBER_MIN_CONTEXT 512u
#define FIBER_MAX_NAME 31u
#define FIBER_SET_FPU 0x100u

typedef void (ABI *FiberEntry)(uint64_t arg_on_initialize, uint64_t arg_on_run);
typedef struct Fiber Fiber;

typedef struct FiberContext {
    uint64_t rax, rcx, rdx, rbx, rsp, rbp, r8, r9, r10, r11, r12, r13, r14, r15;
    uint16_t fpucw;
    uint32_t mxcsr;
    Fiber *current_fiber;
    Fiber *prev_fiber;
    uint64_t arg_on_run_to;
    uint64_t arg_on_return;
    uint64_t return_val;
} FiberContext;

struct Fiber {
    uint32_t magic_start;
    _Atomic uint32_t state;
    FiberEntry entry;
    uint64_t arg_on_initialize;
    void *addr_context;
    uint64_t size_context;
    char name[FIBER_MAX_NAME + 1];
    FiberContext *context;
    uint32_t flags;
    void *context_start;
    void *context_end;
    uint32_t magic_end;
};

typedef struct FiberOptParam { uint32_t magic; } FiberOptParam;
typedef struct FiberData {
    FiberEntry entry;
    uint64_t arg_on_initialize;
    uint64_t arg_on_run_to;
    void *stack_addr;
    uint32_t *state;
    uint16_t fpucw;
    uint8_t pad[2];
    uint32_t mxcsr;
} FiberData;

enum { FIBER_RUN = 1, FIBER_IDLE = 2, FIBER_TERMINATED = 3 };
static _Thread_local FiberContext *thread_fiber_context;

_Static_assert(offsetof(FiberContext, current_fiber) == 120, "Fiber context ABI layout");
_Static_assert(offsetof(FiberData, stack_addr) == 24, "Fiber entry ABI layout");
_Static_assert(offsetof(FiberData, fpucw) == 40, "Fiber FPU ABI layout");

/* Register save/restore and initial stack entry are adapted from shadPS4's
 * fiber_context.cpp, preserving all SysV nonvolatile registers and FP controls. */
__asm__(
".global _sceFiberSetJmp\n"
"_sceFiberSetJmp:\n"
"movq %rax, 0x0(%rdi)\n"
"movq (%rsp), %rdx\n"
"movq %rdx, 0x10(%rdi)\n"
"movq %rcx, 0x08(%rdi)\n"
"movq %rbx, 0x18(%rdi)\n"
"movq %rsp, 0x20(%rdi)\n"
"movq %rbp, 0x28(%rdi)\n"
"movq %r8, 0x30(%rdi)\n"
"movq %r9, 0x38(%rdi)\n"
"movq %r10, 0x40(%rdi)\n"
"movq %r11, 0x48(%rdi)\n"
"movq %r12, 0x50(%rdi)\n"
"movq %r13, 0x58(%rdi)\n"
"movq %r14, 0x60(%rdi)\n"
"movq %r15, 0x68(%rdi)\n"
"fnstcw 0x70(%rdi)\n"
"stmxcsr 0x72(%rdi)\n"
"xorl %eax, %eax\n"
"ret\n"
".global _sceFiberLongJmp\n"
"_sceFiberLongJmp:\n"
"stmxcsr -0x4(%rsp)\n"
"movl 0x72(%rdi), %eax\n"
"andl $0xffffffc0, %eax\n"
"movl -0x4(%rsp), %ecx\n"
"andl $0x3f, %ecx\n"
"xorl %eax, %ecx\n"
"movl %ecx, -0x4(%rsp)\n"
"ldmxcsr -0x4(%rsp)\n"
"movq 0x00(%rdi), %rax\n"
"movq 0x08(%rdi), %rcx\n"
"movq 0x10(%rdi), %rdx\n"
"movq 0x18(%rdi), %rbx\n"
"movq 0x20(%rdi), %rsp\n"
"movq 0x28(%rdi), %rbp\n"
"movq 0x30(%rdi), %r8\n"
"movq 0x38(%rdi), %r9\n"
"movq 0x40(%rdi), %r10\n"
"movq 0x48(%rdi), %r11\n"
"movq 0x50(%rdi), %r12\n"
"movq 0x58(%rdi), %r13\n"
"movq 0x60(%rdi), %r14\n"
"movq 0x68(%rdi), %r15\n"
"fldcw 0x70(%rdi)\n"
"movq %rdx, (%rsp)\n"
"movl $1, %eax\n"
"ret\n"
".global _sceFiberSwitchEntry\n"
"_sceFiberSwitchEntry:\n"
"movq %rdi, %r11\n"
"movq 0x18(%r11), %rsp\n"
"xorl %ebp, %ebp\n"
"movq 0x20(%r11), %r10\n"
"testq %r10, %r10\n"
"jz 1f\n"
"movl $2, (%r10)\n"
"1:\n"
"testl %esi, %esi\n"
"jz 2f\n"
"ldmxcsr 0x2c(%r11)\n"
"fldcw 0x28(%r11)\n"
"2:\n"
"movq 0x08(%r11), %rdi\n"
"movq 0x10(%r11), %rsi\n"
"movq 0x00(%r11), %r11\n"
"xorl %eax, %eax\n"
"xorl %ebx, %ebx\n"
"xorl %ecx, %ecx\n"
"xorl %edx, %edx\n"
"xorq %r8, %r8\n"
"xorq %r9, %r9\n"
"xorq %r10, %r10\n"
"xorq %r12, %r12\n"
"xorq %r13, %r13\n"
"xorq %r14, %r14\n"
"xorq %r15, %r15\n"
"pxor %mm0, %mm0\n"
"pxor %mm1, %mm1\n"
"pxor %mm2, %mm2\n"
"pxor %mm3, %mm3\n"
"pxor %mm4, %mm4\n"
"pxor %mm5, %mm5\n"
"pxor %mm6, %mm6\n"
"pxor %mm7, %mm7\n"
"emms\n"
"pxor %xmm0, %xmm0\n"
"pxor %xmm1, %xmm1\n"
"pxor %xmm2, %xmm2\n"
"pxor %xmm3, %xmm3\n"
"pxor %xmm4, %xmm4\n"
"pxor %xmm5, %xmm5\n"
"pxor %xmm6, %xmm6\n"
"pxor %xmm7, %xmm7\n"
"pxor %xmm8, %xmm8\n"
"pxor %xmm9, %xmm9\n"
"pxor %xmm10, %xmm10\n"
"pxor %xmm11, %xmm11\n"
"pxor %xmm12, %xmm12\n"
"pxor %xmm13, %xmm13\n"
"pxor %xmm14, %xmm14\n"
"pxor %xmm15, %xmm15\n"
"call *%r11\n"
"movl $1, %edi\n"
"call _sceFiberForceQuit\n"
"ud2\n"
);

extern int32_t ABI _sceFiberSetJmp(FiberContext *ctx);
extern void ABI _sceFiberLongJmp(FiberContext *ctx) __attribute__((noreturn));
extern void ABI _sceFiberSwitchEntry(FiberData *data, int set_fpu) __attribute__((noreturn));

void ABI fiber_force_quit(uint64_t value) __asm__("_sceFiberForceQuit");
void ABI fiber_force_quit(uint64_t value) __attribute__((noreturn));
void ABI fiber_force_quit(uint64_t value) {
    FiberContext *ctx = thread_fiber_context;
    if (!ctx) __builtin_trap();
    ctx->return_val = value;
    _sceFiberLongJmp(ctx);
}

static void ABI fiber_switch_to(Fiber *fiber, uint64_t arg, FiberContext *ctx) __attribute__((noreturn));
static void ABI fiber_switch_to(Fiber *fiber, uint64_t arg, FiberContext *ctx) {
    if (fiber->context) {
        ctx->arg_on_run_to = arg;
        _sceFiberLongJmp(fiber->context);
    }
    FiberData data = {0};
    if (ctx->prev_fiber) {
        Fiber *previous = ctx->prev_fiber;
        ctx->prev_fiber = NULL;
        data.state = (uint32_t *)&previous->state;
    }
    data.entry = fiber->entry;
    data.arg_on_initialize = fiber->arg_on_initialize;
    data.arg_on_run_to = arg;
    data.stack_addr = (uint8_t *)fiber->addr_context + fiber->size_context;
    data.fpucw = 0x037f;
    data.mxcsr = 0x9fc0;
    _sceFiberSwitchEntry(&data, (fiber->flags & FIBER_SET_FPU) != 0);
}

static void ABI fiber_switch(Fiber *current, Fiber *next, uint64_t arg, FiberContext *ctx) __attribute__((noreturn));
static void ABI fiber_switch(Fiber *current, Fiber *next, uint64_t arg, FiberContext *ctx) {
    ctx->prev_fiber = current;
    ctx->current_fiber = next;
    if (!next->addr_context) {
        ctx->prev_fiber = NULL;
        FiberData data = {0};
        data.entry = next->entry;
        data.arg_on_initialize = next->arg_on_initialize;
        data.arg_on_run_to = arg;
        data.stack_addr = (void *)(ctx->rsp & ~(uint64_t)15);
        data.state = (uint32_t *)&current->state;
        data.fpucw = 0x037f;
        data.mxcsr = 0x9fc0;
        _sceFiberSwitchEntry(&data, (next->flags & FIBER_SET_FPU) != 0);
    }
    fiber_switch_to(next, arg, ctx);
}

static int32_t ABI fiber_initialize(Fiber *fiber, const char *name, FiberEntry entry,
                                    uint64_t arg_initialize, void *stack, uint64_t stack_size,
                                    const FiberOptParam *opt, uint32_t build_version) {
    if (!fiber || !name || !entry) return FIBER_ERR_NULL;
    if (((uintptr_t)fiber & 7u) || ((uintptr_t)stack & 15u)) return FIBER_ERR_ALIGNMENT;
    if (opt && ((uintptr_t)opt & 7u)) return FIBER_ERR_ALIGNMENT;
    if (stack_size && stack_size < FIBER_MIN_CONTEXT) return FIBER_ERR_RANGE;
    if ((stack_size & 15u) || (!stack && stack_size) || (stack && !stack_size)) return FIBER_ERR_INVALID;
    if (opt && opt->magic != FIBER_OPT_MAGIC) return FIBER_ERR_INVALID;
    memset(fiber, 0, sizeof(*fiber));
    memcpy(fiber->name, name, strnlen(name, FIBER_MAX_NAME));
    fiber->entry = entry;
    fiber->arg_on_initialize = arg_initialize;
    fiber->addr_context = stack;
    fiber->size_context = stack_size;
    fiber->context = NULL;
    fiber->flags = build_version >= 0x03500000u ? FIBER_SET_FPU : 0;
    if (stack) {
        fiber->context_start = stack;
        fiber->context_end = (uint8_t *)stack + stack_size;
        *(uint64_t *)stack = FIBER_STACK_MAGIC;
    }
    fiber->magic_start = FIBER_MAGIC_START;
    fiber->magic_end = FIBER_MAGIC_END;
    atomic_store(&fiber->state, FIBER_IDLE);
    return 0;
}

static int32_t ABI fiber_finalize(Fiber *fiber) {
    if (!fiber) return FIBER_ERR_NULL;
    if ((uintptr_t)fiber & 7u) return FIBER_ERR_ALIGNMENT;
    if (fiber->magic_start != FIBER_MAGIC_START || fiber->magic_end != FIBER_MAGIC_END)
        return FIBER_ERR_INVALID;
    uint32_t expected = FIBER_IDLE;
    if (!atomic_compare_exchange_strong(&fiber->state, &expected, FIBER_TERMINATED))
        return FIBER_ERR_STATE;
    return 0;
}

static int32_t ABI fiber_run(Fiber *fiber, uint64_t arg_run, uint64_t *arg_return) {
    if (!fiber) return FIBER_ERR_NULL;
    if ((uintptr_t)fiber & 7u) return FIBER_ERR_ALIGNMENT;
    if (fiber->magic_start != FIBER_MAGIC_START || fiber->magic_end != FIBER_MAGIC_END)
        return FIBER_ERR_INVALID;
    if (thread_fiber_context) return FIBER_ERR_PERMISSION;
    uint32_t expected = FIBER_IDLE;
    if (!atomic_compare_exchange_strong(&fiber->state, &expected, FIBER_RUN)) return FIBER_ERR_STATE;
    FiberContext ctx = {0};
    ctx.current_fiber = fiber;
    thread_fiber_context = &ctx;
    if (!_sceFiberSetJmp(&ctx)) {
        if (fiber->addr_context) {
            fiber_switch_to(fiber, arg_run, &ctx);
        } else {
            FiberData data = {0};
            data.entry = fiber->entry;
            data.arg_on_initialize = fiber->arg_on_initialize;
            data.arg_on_run_to = arg_run;
            data.stack_addr = (void *)(ctx.rsp & ~(uint64_t)15);
            data.fpucw = 0x037f;
            data.mxcsr = 0x9fc0;
            _sceFiberSwitchEntry(&data, (fiber->flags & FIBER_SET_FPU) != 0);
        }
    }
    Fiber *returned = ctx.current_fiber;
    if (returned) atomic_store(&returned->state, FIBER_IDLE);
    thread_fiber_context = NULL;
    if (arg_return) *arg_return = ctx.arg_on_return;
    return ctx.return_val ? FIBER_ERR_STATE : 0;
}

static int32_t ABI fiber_switch_api(Fiber *fiber, uint64_t arg_to, uint64_t *arg_from) {
    if (!fiber) return FIBER_ERR_NULL;
    if ((uintptr_t)fiber & 7u) return FIBER_ERR_ALIGNMENT;
    if (fiber->magic_start != FIBER_MAGIC_START || fiber->magic_end != FIBER_MAGIC_END)
        return FIBER_ERR_INVALID;
    FiberContext *ctx = thread_fiber_context;
    if (!ctx) return FIBER_ERR_PERMISSION;
    uint32_t expected = FIBER_IDLE;
    if (!atomic_compare_exchange_strong(&fiber->state, &expected, FIBER_RUN)) return FIBER_ERR_STATE;
    Fiber *current = ctx->current_fiber;
    if (!current->addr_context) fiber_switch(current, fiber, arg_to, ctx);
    FiberContext saved = {0};
    if (!_sceFiberSetJmp(&saved)) {
        current->context = &saved;
        fiber_switch(current, fiber, arg_to, ctx);
    }
    ctx = thread_fiber_context;
    if (ctx->prev_fiber) {
        atomic_store(&ctx->prev_fiber->state, FIBER_IDLE);
        ctx->prev_fiber = NULL;
    }
    if (arg_from) *arg_from = ctx->arg_on_run_to;
    return 0;
}

static int32_t ABI fiber_return_to_thread(uint64_t arg_return, uint64_t *arg_run) {
    FiberContext *ctx = thread_fiber_context;
    if (!ctx) return FIBER_ERR_PERMISSION;
    Fiber *current = ctx->current_fiber;
    if (current->addr_context) {
        FiberContext saved = {0};
        if (_sceFiberSetJmp(&saved)) {
            ctx = thread_fiber_context;
            if (ctx->prev_fiber) {
                atomic_store(&ctx->prev_fiber->state, FIBER_IDLE);
                ctx->prev_fiber = NULL;
            }
            if (arg_run) *arg_run = ctx->arg_on_run_to;
            return 0;
        }
        current->context = &saved;
    }
    ctx->arg_on_return = arg_return;
    _sceFiberLongJmp(ctx);
}

uintptr_t runtime_fiber_resolve(const char *name) {
    if (!strcmp(name, "hVYD7Ou2pCQ#libSceFiber")) return (uintptr_t)fiber_initialize;
    if (!strcmp(name, "JeNX5F-NzQU#libSceFiber")) return (uintptr_t)fiber_finalize;
    if (!strcmp(name, "a0LLrZWac0M#libSceFiber")) return (uintptr_t)fiber_run;
    if (!strcmp(name, "PFT2S-tJ7Uk#libSceFiber")) return (uintptr_t)fiber_switch_api;
    if (!strcmp(name, "B0ZX2hx9DMw#libSceFiber")) return (uintptr_t)fiber_return_to_thread;
    return 0;
}
