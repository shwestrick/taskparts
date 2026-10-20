#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

/*---------------------------------------------------------------------*/
/* Context save/restore
 *
 * A context records the callee-saved registers, the stack pointer,
 * and the resume address of a suspended computation.
 *
 *   ctx_save(ctx)        saves the current context into ctx and
 *                        returns nullptr.
 *   ctx_restore(ctx, v)  resumes ctx; the matching ctx_save then
 *                        returns v (or 1, if v is nullptr).
 */

#if defined(__APPLE__)
#define TASKPARTS_ASM_SYM(name) "_" #name
#else
#define TASKPARTS_ASM_SYM(name) #name
#endif

#if defined(__x86_64__)

/* layout: rbx, rbp, r12, r13, r14, r15, rsp, rip */
static constexpr std::size_t taskparts_ctx_szb = 8 * 8;

asm(".text\n"
    ".p2align 4\n"
    TASKPARTS_ASM_SYM(taskparts_ctx_save) ":\n"
    "    movq %rbx, 0(%rdi)\n"
    "    movq %rbp, 8(%rdi)\n"
    "    movq %r12, 16(%rdi)\n"
    "    movq %r13, 24(%rdi)\n"
    "    movq %r14, 32(%rdi)\n"
    "    movq %r15, 40(%rdi)\n"
    "    leaq 8(%rsp), %rdx\n"
    "    movq %rdx, 48(%rdi)\n"
    "    movq (%rsp), %rax\n"
    "    movq %rax, 56(%rdi)\n"
    "    xorq %rax, %rax\n"
    "    ret\n"
    ".p2align 4\n"
    TASKPARTS_ASM_SYM(taskparts_ctx_restore) ":\n"
    "    movq 0(%rdi), %rbx\n"
    "    movq 8(%rdi), %rbp\n"
    "    movq 16(%rdi), %r12\n"
    "    movq 24(%rdi), %r13\n"
    "    movq 32(%rdi), %r14\n"
    "    movq 40(%rdi), %r15\n"
    "    test %rsi, %rsi\n"
    "    mov $1, %rax\n"
    "    cmove %rax, %rsi\n"
    "    mov %rsi, %rax\n"
    "    movq 56(%rdi), %rdx\n"
    "    movq 48(%rdi), %rsp\n"
    "    jmpq *%rdx\n"
    /* first code to run on a fresh stack: calls rbx(rax) */
    ".p2align 4\n"
    TASKPARTS_ASM_SYM(taskparts_ctx_trampoline) ":\n"
    "    movq %rax, %rdi\n"
    "    callq *%rbx\n"
    "    ud2\n");

#elif defined(__aarch64__)

/* layout: x19-x28, fp, lr, sp, d8-d15 */
static constexpr std::size_t taskparts_ctx_szb = 21 * 8;

asm(".text\n"
    ".p2align 4\n"
    TASKPARTS_ASM_SYM(taskparts_ctx_save) ":\n"
    "    stp x19, x20, [x0, 0]\n"
    "    stp x21, x22, [x0, 16]\n"
    "    stp x23, x24, [x0, 32]\n"
    "    stp x25, x26, [x0, 48]\n"
    "    stp x27, x28, [x0, 64]\n"
    "    stp fp, lr, [x0, 80]\n"
    "    mov x1, sp\n"
    "    str x1, [x0, 96]\n"
    "    stp d8, d9, [x0, 104]\n"
    "    stp d10, d11, [x0, 120]\n"
    "    stp d12, d13, [x0, 136]\n"
    "    stp d14, d15, [x0, 152]\n"
    "    mov x0, #0\n"
    "    ret\n"
    ".p2align 4\n"
    TASKPARTS_ASM_SYM(taskparts_ctx_restore) ":\n"
    "    ldp x19, x20, [x0, 0]\n"
    "    ldp x21, x22, [x0, 16]\n"
    "    ldp x23, x24, [x0, 32]\n"
    "    ldp x25, x26, [x0, 48]\n"
    "    ldp x27, x28, [x0, 64]\n"
    "    ldp fp, lr, [x0, 80]\n"
    "    ldr x2, [x0, 96]\n"
    "    mov sp, x2\n"
    "    ldp d8, d9, [x0, 104]\n"
    "    ldp d10, d11, [x0, 120]\n"
    "    ldp d12, d13, [x0, 136]\n"
    "    ldp d14, d15, [x0, 152]\n"
    "    mov x2, #1\n"
    "    cmp x1, #0\n"
    "    csel x0, x2, x1, eq\n"
    "    ret\n"
    /* first code to run on a fresh stack: calls x19(x0) */
    ".p2align 4\n"
    TASKPARTS_ASM_SYM(taskparts_ctx_trampoline) ":\n"
    "    blr x19\n"
    "    brk #0\n");

#else
#error "taskparts: unsupported architecture (need x86-64 or arm64)"
#endif

extern "C" __attribute__((returns_twice)) void* taskparts_ctx_save(char* ctx);
extern "C" [[noreturn]] void taskparts_ctx_restore(char* ctx, void* v);
extern "C" void taskparts_ctx_trampoline();

namespace taskparts {
namespace context {

using context_type = char[taskparts_ctx_szb];

// Save the current context into `from`, then resume `to`, passing it `v`.
// Returns when some thread later resumes `from`.
[[gnu::noinline]] inline
void swap(context_type& from, context_type& to, void* v = nullptr) {
  if (taskparts_ctx_save(from) != nullptr) {
    return;
  }
  taskparts_ctx_restore(to, v);
}

// Resume `to`, discarding the current context.
[[noreturn]] inline
void throw_to(context_type& to, void* v = nullptr) {
  taskparts_ctx_restore(to, v);
}

// Initialize `ctx` so that resuming it with value v calls entry(v) on the
// stack [stack, stack + szb). `entry` must never return.
inline
void prepare(context_type& ctx, char* stack, std::size_t szb, void (*entry)(void*)) {
  std::memset(ctx, 0, sizeof(ctx));
  auto regs = reinterpret_cast<void**>(ctx);
  auto sp = reinterpret_cast<void*>(
    reinterpret_cast<std::uintptr_t>(stack + szb) & ~std::uintptr_t(15));
  auto trampoline = reinterpret_cast<void*>(&taskparts_ctx_trampoline);
#if defined(__x86_64__)
  regs[0] = reinterpret_cast<void*>(entry); // rbx
  regs[6] = sp;                             // rsp
  regs[7] = trampoline;                     // rip
#else
  regs[0] = reinterpret_cast<void*>(entry); // x19
  regs[11] = trampoline;                    // lr
  regs[12] = sp;                            // sp
#endif
}

} // namespace context
} // namespace taskparts
