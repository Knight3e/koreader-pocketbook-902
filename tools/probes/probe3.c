/* probe3.c - crash-hunting probe for the PocketBook Pro 902 (firmware 2.x).
 * Same idea as probe2, but every step is logged BEFORE and AFTER (with fsync), fatal signals are caught
 * and logged with the faulting address / backtrace / memory map, and the drawing steps are spread over
 * a 1 second InkView timer so we can see exactly which call kills the app.
 * Writes only /mnt/ext1/probe3.txt and draws on the screen. The app closes itself after ~15 seconds. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <execinfo.h>
#include <ucontext.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <time.h>
#include <stdarg.h>

#define OUTPATH "/mnt/ext1/probe3.txt"
typedef int (*iv_handler)(int, int, int);
typedef void (*iv_timerproc)(void);

static FILE *out;
static int fd_out = -1;
static void *lib;
static int events, tick, screen_open;
static void *font;
static void (*p_InkViewMain)(iv_handler);
static void (*p_OpenScreen)(void);
static void (*p_ClearScreen)(void);
static void (*p_FullUpdate)(void);
static void (*p_FillArea)(int, int, int, int, int);
static void (*p_DrawString)(int, int, const char *);
static void *(*p_OpenFont)(const char *, int, int);
static void (*p_SetFont)(void *, int);
static int (*p_ScreenWidth)(void);
static int (*p_ScreenHeight)(void);
static void (*p_CloseApp)(void);
static void (*p_SetHardTimer)(const char *, iv_timerproc, int);
static char *(*p_GetSoftwareVersion)(void);

static void L(const char *fmt, ...) {
    va_list ap;
    if (!out) return;
    fprintf(out, "[%ld] ", (long)time(NULL));
    va_start(ap, fmt); vfprintf(out, fmt, ap); va_end(ap);
    fputc('\n', out); fflush(out); fsync(fd_out);
}

static void on_signal(int sig, siginfo_t *si, void *ctx) {
    void *bt[32]; int n, fd, r; char buf[4096];
    L("SIGNAL %d (%s) code=%d fault_addr=%p", sig, strsignal(sig), si->si_code, si->si_addr);
#ifdef __arm__
    L("pc=0x%lx lr=0x%lx sp=0x%lx", ((ucontext_t *)ctx)->uc_mcontext.arm_pc,
      ((ucontext_t *)ctx)->uc_mcontext.arm_lr, ((ucontext_t *)ctx)->uc_mcontext.arm_sp);
#endif
    n = backtrace(bt, 32);
    L("backtrace (%d frames):", n);
    backtrace_symbols_fd(bt, n, fd_out);
    fd = open("/proc/self/maps", O_RDONLY);
    if (fd >= 0) { L("memory map:"); while ((r = read(fd, buf, sizeof buf)) > 0) (void)write(fd_out, buf, r); close(fd); }
    fsync(fd_out);
    _exit(128 + sig);
}

static void on_exit_(void) { L("atexit handler called (process is exiting normally via exit())"); }

#define SYM(name) do { *(void **)(&p_##name) = dlsym(lib, #name); L("sym %-18s %s", #name, p_##name ? "found" : "MISSING"); } while (0)

static void tickproc(void) {
    tick++;
    L("tick %d", tick);
    switch (tick) {
    case 1: L("step ClearScreen"); p_ClearScreen(); L("ok"); break;
    case 2: L("step FillArea frame");
            p_FillArea(20, 20, 788, 6, 0); p_FillArea(20, 1174, 788, 6, 0);
            p_FillArea(20, 20, 6, 1160, 0); p_FillArea(802, 20, 6, 1160, 0); L("ok"); break;
    case 3: L("step OpenFont"); font = p_OpenFont("LiberationSans", 32, 1); L("ok font=%p", font); break;
    case 4: if (font) { L("step SetFont"); p_SetFont(font, 0); L("ok"); L("step DrawString"); p_DrawString(60, 80, "probe3 alive"); L("ok"); } break;
    case 5: L("step FullUpdate"); p_FullUpdate(); L("ok"); break;
    default: break;
    }
    if (tick >= 15) { L("step CloseApp"); p_CloseApp(); L("CloseApp returned"); return; }
    p_SetHardTimer("probe3tick", tickproc, 1000);
}

static int handler(int type, int par1, int par2) {
    if (events < 300) L("event %d: type=%d par1=%d par2=%d", events, type, par1, par2);
    events++;
    if (type == 21 && !screen_open) {
        L("step OpenScreen"); p_OpenScreen(); screen_open = 1;
        L("ok: screen %d x %d", p_ScreenWidth(), p_ScreenHeight());
        L("step SetHardTimer"); p_SetHardTimer("probe3tick", tickproc, 1000); L("timer armed");
    }
    if (events < 300) L("handler returns");
    return 0;
}

int main(int argc, char **argv) {
    struct sigaction sa;
    int i, sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTERM, SIGHUP, SIGPIPE, SIGINT, SIGQUIT, 0 };
    out = fopen(OUTPATH, "w");
    if (!out) return 1;
    fd_out = fileno(out);
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_signal; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
    for (i = 0; sigs[i]; i++) sigaction(sigs[i], &sa, NULL);
    atexit(on_exit_);
    L("probe3 started, pid=%d ppid=%d argc=%d argv0=%s", (int)getpid(), (int)getppid(), argc, argv[0]);
    L("env LD_LIBRARY_PATH=%s PATH=%s", getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") : "(unset)",
      getenv("PATH") ? getenv("PATH") : "(unset)");
    lib = dlopen("libinkview.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) { L("dlopen failed: %s", dlerror()); return 2; }
    SYM(InkViewMain); SYM(OpenScreen); SYM(ClearScreen); SYM(FullUpdate); SYM(FillArea); SYM(DrawString);
    SYM(OpenFont); SYM(SetFont); SYM(ScreenWidth); SYM(ScreenHeight); SYM(CloseApp); SYM(SetHardTimer);
    SYM(GetSoftwareVersion);
    if (p_GetSoftwareVersion) L("GetSoftwareVersion() = \"%s\"", p_GetSoftwareVersion());
    if (!p_InkViewMain || !p_SetHardTimer) { L("missing functions, giving up"); return 3; }
    L("calling InkViewMain");
    p_InkViewMain(handler);
    L("InkViewMain returned normally");
    return 0;
}
