/* probe8.c - CPU/instruction-set and device-model probe for the PocketBook Pro 902 (firmware 2.x).
 * No screen, no InkView loop: it only runs a few single CPU instructions (each guarded against SIGILL),
 * asks libinkview for the device model, and writes /mnt/ext1/probe8.txt. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <dlfcn.h>
#include <fcntl.h>

static sigjmp_buf jb;
static void on_ill(int sig) { (void)sig; siglongjmp(jb, 1); }
static FILE *out;

#define TRY(name, ...) do { \
    if (sigsetjmp(jb, 1) == 0) { __VA_ARGS__; fprintf(out, "%-34s OK\n", name); } \
    else fprintf(out, "%-34s ILLEGAL INSTRUCTION (SIGILL)\n", name); \
    fflush(out); fsync(fileno(out)); } while (0)

int main(void) {
    struct sigaction sa;
    void *lib;
    char *(*getmodel)(void);
    char *(*getver)(void);
    int (*gsensor)(void);
    unsigned int tmp = 0;
    out = fopen("/mnt/ext1/probe8.txt", "w");
    if (!out) return 1;
    memset(&sa, 0, sizeof sa); sa.sa_handler = on_ill; sigemptyset(&sa.sa_mask);
    sigaction(SIGILL, &sa, NULL);
    fprintf(out, "probe8: which instructions does this CPU/kernel really run?\n");
    TRY("ARMv6: rev r0,r0 (byte swap)",      __asm__ volatile("rev %0, %0" : "+r"(tmp)));
    TRY("ARMv6K: ldrex/strex",               { unsigned int v = 0, r; __asm__ volatile("ldrex %0, [%2]\n strex %1, %0, [%2]" : "=&r"(r), "=&r"(tmp) : "r"(&v) : "memory"); });
    TRY("ARMv6: uxtb (zero extend byte)",    __asm__ volatile("uxtb %0, %0" : "+r"(tmp)));
    TRY("ARMv7: movw r0,#1 (Thumb-2 era)",   __asm__ volatile(".arch armv7-a\n movw %0, #1" : "=r"(tmp)));
    TRY("ARMv7: movt",                       __asm__ volatile(".arch armv7-a\n movt %0, #1" : "+r"(tmp)));
    TRY("VFP single: vmov s0,r0",            __asm__ volatile(".fpu vfp\n vmov s0, %0" : : "r"(tmp) : "s0"));
    TRY("VFP double: vadd.f64 d0,d0,d0",     __asm__ volatile(".fpu vfp\n vadd.f64 d0, d0, d0" : : : "d0"));
    TRY("NEON: vadd.i32 q0,q0,q0",           __asm__ volatile(".fpu neon\n vadd.i32 q0, q0, q0" : : : "q0"));
    lib = dlopen("libinkview.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) { fprintf(out, "dlopen libinkview failed: %s\n", dlerror()); fclose(out); return 2; }
    *(void **)(&getmodel) = dlsym(lib, "GetDeviceModel");
    *(void **)(&getver) = dlsym(lib, "GetSoftwareVersion");
    *(void **)(&gsensor) = dlsym(lib, "QueryGSensor");
    fprintf(out, "GetDeviceModel()      = %s\n", getmodel ? getmodel() : "(function missing)");
    fprintf(out, "GetSoftwareVersion()  = %s\n", getver ? getver() : "(function missing)");
    fprintf(out, "QueryGSensor()        = %s\n", gsensor ? (gsensor() ? "yes" : "no") : "(function missing)");
    fprintf(out, "done\n");
    fclose(out);
    return 0;
}
