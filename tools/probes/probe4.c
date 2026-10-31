/* probe4.c - the "textbook" InkView app skeleton for the PocketBook Pro 902 (firmware 2.x).
 * Difference to probe2/probe3: it does NOT call OpenScreen() and uses no timers.
 *   EVT_INIT (21)  -> open a font
 *   EVT_SHOW (23)  -> draw a frame + text and update the screen
 *   EVT_KEYPRESS (25) -> after 6 key presses the app closes itself (Home button also leaves it)
 * Every step is logged before/after with fsync; fatal signals are caught and logged (like probe3).
 * Writes only /mnt/ext1/probe4.txt. */
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

#define OUTPATH "/mnt/ext1/probe4.txt"
typedef int (*iv_handler)(int, int, int);

static FILE *out;
static int fd_out = -1;
static void *lib;
static int events, keypresses, shows;
static void *font;
static void (*p_InkViewMain)(iv_handler);
static void (*p_ClearScreen)(void);
static void (*p_FullUpdate)(void);
static void (*p_FillArea)(int, int, int, int, int);
static void (*p_DrawString)(int, int, const char *);
static void *(*p_OpenFont)(const char *, int, int);
static void (*p_SetFont)(void *, int);
static int (*p_ScreenWidth)(void);
static int (*p_ScreenHeight)(void);
static void (*p_CloseApp)(void);
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

static void on_exit_(void) { L("atexit handler called"); }

#define SYM(name) do { *(void **)(&p_##name) = dlsym(lib, #name); L("sym %-18s %s", #name, p_##name ? "found" : "MISSING"); } while (0)

static void draw(void) {
    int w = p_ScreenWidth(), h = p_ScreenHeight();
    L("draw: screen %d x %d", w, h);
    L("step ClearScreen"); p_ClearScreen(); L("ok");
    L("step FillArea frame");
    p_FillArea(20, 20, w - 40, 6, 0); p_FillArea(20, h - 26, w - 40, 6, 0);
    p_FillArea(20, 20, 6, h - 40, 0); p_FillArea(w - 26, 20, 6, h - 40, 0);
    L("ok");
    if (font) {
        L("step SetFont"); p_SetFont(font, 0); L("ok");
        L("step DrawString"); p_DrawString(60, 80, "probe4: press 6 keys"); L("ok");
    }
    L("step FullUpdate"); p_FullUpdate(); L("ok");
}

static int handler(int type, int par1, int par2) {
    if (events < 300) L("event %d: type=%d par1=%d par2=%d", events, type, par1, par2);
    events++;
    if (type == 21) {                       /* EVT_INIT */
        L("step OpenFont"); font = p_OpenFont("LiberationSans", 32, 1); L("ok font=%p", font);
    } else if (type == 23 && shows < 3) {   /* EVT_SHOW */
        shows++;
        draw();
    } else if (type == 25) {                /* EVT_KEYPRESS */
        if (++keypresses >= 6) { L("6 key presses, step CloseApp"); p_CloseApp(); L("CloseApp returned"); }
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
    L("probe4 started, pid=%d argv0=%s", (int)getpid(), argv[0]);
    lib = dlopen("libinkview.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) { L("dlopen failed: %s", dlerror()); return 2; }
    SYM(InkViewMain); SYM(ClearScreen); SYM(FullUpdate); SYM(FillArea); SYM(DrawString);
    SYM(OpenFont); SYM(SetFont); SYM(ScreenWidth); SYM(ScreenHeight); SYM(CloseApp); SYM(GetSoftwareVersion);
    if (p_GetSoftwareVersion) L("GetSoftwareVersion() = \"%s\"", p_GetSoftwareVersion());
    if (!p_InkViewMain) { L("no InkViewMain, giving up"); return 3; }
    L("calling InkViewMain (no OpenScreen, no timers)");
    p_InkViewMain(handler);
    L("InkViewMain returned normally");
    return 0;
}
