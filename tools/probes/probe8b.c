/* probe8b.c - instruction-set probe. Each test instruction lives in its own assembler file so that no
 * assembler directive can leak into the rest of the program. A marker file is created FIRST, so we can tell
 * "app did not start" from "app started". Writes /mnt/ext1/probe8_started and /mnt/ext1/probe8.txt. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <dlfcn.h>
#include <fcntl.h>

extern unsigned t_rev(unsigned), t_uxtb(unsigned), t_movw(void), t_movt(void);
extern void t_ldrex(unsigned *), t_vfp_single(unsigned), t_vfp_double(void), t_neon(void);

static sigjmp_buf jb;
static FILE *out;
static void on_ill(int sig) { (void)sig; siglongjmp(jb, 1); }
#define TRY(name, call) do { \
    if (sigsetjmp(jb, 1) == 0) { call; fprintf(out, "%-36s OK (runs)\n", name); } \
    else fprintf(out, "%-36s ILLEGAL INSTRUCTION (SIGILL)\n", name); \
    fflush(out); fsync(fileno(out)); } while (0)

int main(void) {
    struct sigaction sa;
    void *lib;
    char *(*getmodel)(void), *(*getver)(void);
    int (*gsensor)(void);
    unsigned v = 5;
    int fd = open("/mnt/ext1/probe8_started", O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd >= 0) { (void)write(fd, "started\n", 8); fsync(fd); close(fd); }
    out = fopen("/mnt/ext1/probe8.txt", "w");
    if (!out) return 1;
    memset(&sa, 0, sizeof sa); sa.sa_handler = on_ill; sigemptyset(&sa.sa_mask);
    sigaction(SIGILL, &sa, NULL);
    fprintf(out, "probe8b: which instructions does this CPU/kernel really run?\n"); fflush(out); fsync(fileno(out));
    TRY("ARMv6  rev (byte swap)",            t_rev(v));
    TRY("ARMv6K ldrex/strex",                t_ldrex(&v));
    TRY("ARMv6  uxtb",                       t_uxtb(v));
    TRY("ARMv7  movw",                       t_movw());
    TRY("ARMv7  movt",                       t_movt());
    TRY("VFP    vmov s0,r0 (single)",        t_vfp_single(v));
    TRY("VFP    vadd.f64 (double)",          t_vfp_double());
    TRY("NEON   vadd.i32 q0",                t_neon());
    lib = dlopen("libinkview.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) { fprintf(out, "dlopen libinkview failed: %s\n", dlerror()); fclose(out); return 2; }
    *(void **)(&getmodel) = dlsym(lib, "GetDeviceModel");
    *(void **)(&getver) = dlsym(lib, "GetSoftwareVersion");
    *(void **)(&gsensor) = dlsym(lib, "QueryGSensor");
    fprintf(out, "GetDeviceModel()     = %s\n", getmodel ? getmodel() : "(missing)");
    fprintf(out, "GetSoftwareVersion() = %s\n", getver ? getver() : "(missing)");
    fprintf(out, "QueryGSensor()       = %s\n", gsensor ? (gsensor() ? "yes" : "no") : "(missing)");
    fprintf(out, "done\n");
    fclose(out);
    return 0;
}
