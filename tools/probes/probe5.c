/* probe5.c - "KOReader-style" framebuffer probe for the PocketBook Pro 902 (firmware 2.x).
 * Same safe skeleton as probe4 (works on your device). In the first EVT_SHOW it does what KOReader does:
 *   GetCurrentTask() -> GetTaskFramebuffer(task) -> read width/height/scanline/depth/addr,
 *   write pixels DIRECTLY into that memory, then refresh with FullUpdate / PartialUpdate / DynamicUpdate,
 * logging every step and how long each refresh call takes. It also checks whether InkView's own DrawString
 * draws into that same memory. Closes after 3 key presses. Writes only /mnt/ext1/probe5.txt. */
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
#include <sys/time.h>

#define OUTPATH "/mnt/ext1/probe5.txt"
typedef int (*iv_handler)(int, int, int);
typedef struct { int width, height, scanline, depth, clipx1, clipx2, clipy1, clipy2; unsigned char *addr; } canvas_t;

static FILE *out;
static int fd_out = -1;
static void *lib;
static int events, keypresses, shows;
static void *font;
static void (*p_InkViewMain)(iv_handler);
static void (*p_FullUpdate)(void);
static void (*p_PartialUpdate)(int, int, int, int);
static void (*p_DynamicUpdate)(int, int, int, int);
static void (*p_DrawString)(int, int, const char *);
static void *(*p_OpenFont)(const char *, int, int);
static void (*p_SetFont)(void *, int);
static int (*p_ScreenWidth)(void);
static int (*p_ScreenHeight)(void);
static int (*p_GetOrientation)(void);
static int (*p_GetCurrentTask)(void);
static canvas_t *(*p_GetTaskFramebuffer)(int);
static void (*p_SetPanelType)(int);
static void (*p_CloseApp)(void);
static char *(*p_GetSoftwareVersion)(void);

static void L(const char *fmt, ...) {
    va_list ap;
    if (!out) return;
    fprintf(out, "[%ld] ", (long)time(NULL));
    va_start(ap, fmt); vfprintf(out, fmt, ap); va_end(ap);
    fputc('\n', out); fflush(out); fsync(fd_out);
}

static long now_ms(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec * 1000L + tv.tv_usec / 1000; }

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

#define SYM(name) do { *(void **)(&p_##name) = dlsym(lib, #name); L("sym %-20s %s", #name, p_##name ? "found" : "MISSING"); } while (0)

/* fill a rectangle directly in the framebuffer: gray 0 = black .. 255 = white */
static void fill(canvas_t *c, int x, int y, int w, int h, int gray) {
    int yy, xx;
    if (c->depth == 8) {
        for (yy = y; yy < y + h && yy < c->height; yy++) memset(c->addr + (long)yy * c->scanline + x, gray, w);
    } else if (c->depth == 4) {
        unsigned char v = (unsigned char)(((gray >> 4) << 4) | (gray >> 4));
        for (yy = y; yy < y + h && yy < c->height; yy++)
            for (xx = x & ~1; xx < x + w; xx += 2) c->addr[(long)yy * c->scanline + xx / 2] = v;
    }
}

/* how many bytes in a rectangle are not "white" (0xFF) - used to see whether DrawString wrote into addr */
static long nonwhite(canvas_t *c, int y0, int y1, int bytes_per_row) {
    long n = 0; int yy, i;
    for (yy = y0; yy < y1 && yy < c->height; yy++)
        for (i = 0; i < bytes_per_row && i < c->scanline; i++) if (c->addr[(long)yy * c->scanline + i] != 0xFF) n++;
    return n;
}

static void framebuffer_test(void) {
    int task, w, h, i;
    canvas_t *c;
    long t0, before, after;
    unsigned *raw;
    task = p_GetCurrentTask ? p_GetCurrentTask() : -1;
    L("GetCurrentTask() = %d, GetOrientation() = %d", task, p_GetOrientation ? p_GetOrientation() : -99);
    c = p_GetTaskFramebuffer ? p_GetTaskFramebuffer(task) : NULL;
    L("GetTaskFramebuffer(%d) = %p", task, (void *)c);
    if (!c) return;
    raw = (unsigned *)c;
    for (i = 0; i < 12; i += 4) L("raw words %d..%d: %08x %08x %08x %08x", i, i + 3, raw[i], raw[i + 1], raw[i + 2], raw[i + 3]);
    L("canvas: width=%d height=%d scanline=%d depth=%d addr=%p clip=%d,%d,%d,%d",
      c->width, c->height, c->scanline, c->depth, (void *)c->addr, c->clipx1, c->clipx2, c->clipy1, c->clipy2);
    if (!c->addr || c->width <= 0 || c->height <= 0 || c->scanline <= 0 || (c->depth != 8 && c->depth != 4)) {
        L("canvas looks unusual (layout differs or depth not 4/8), not writing pixels"); return;
    }
    w = c->width; h = c->height;

    L("A: whole buffer white (direct memset), then FullUpdate");
    memset(c->addr, 0xFF, (long)c->scanline * h);
    t0 = now_ms(); p_FullUpdate(); L("   FullUpdate took %ld ms", now_ms() - t0); sleep(2);

    L("B: black bar at top via direct writes, then PartialUpdate");
    fill(c, 40, 100, w - 80, 80, 0);
    t0 = now_ms(); p_PartialUpdate(40, 100, w - 80, 80); L("   PartialUpdate took %ld ms", now_ms() - t0); sleep(2);

    L("C: 8 gray bands via direct writes, then DynamicUpdate");
    for (i = 0; i < 8; i++) fill(c, 40, 260 + i * 60, w - 80, 56, i * 36);
    t0 = now_ms(); p_DynamicUpdate(40, 260, w - 80, 8 * 60); L("   DynamicUpdate took %ld ms", now_ms() - t0); sleep(2);

    L("D: does InkView DrawString draw into the same memory?");
    before = nonwhite(c, 800, 860, c->scanline);
    if (font) { p_SetFont(font, 0); p_DrawString(60, 810, "InkView DrawString"); }
    after = nonwhite(c, 800, 860, c->scanline);
    L("   non-white bytes in rows 800-860: before=%ld after=%ld -> %s", before, after,
      after > before ? "YES, same memory" : "NO (or text not drawn)");
    t0 = now_ms(); p_PartialUpdate(40, 800, w - 80, 60); L("   PartialUpdate took %ld ms", now_ms() - t0);
    L("framebuffer test finished; press 3 keys to close");
}

static int handler(int type, int par1, int par2) {
    if (events < 300) L("event %d: type=%d par1=%d par2=%d", events, type, par1, par2);
    events++;
    if (type == 21) {                       /* EVT_INIT */
        font = p_OpenFont("LiberationSans", 32, 1); L("OpenFont -> %p", font);
        if (p_SetPanelType) { p_SetPanelType(0); L("SetPanelType(0) done"); }
    } else if (type == 23 && shows < 1) {   /* EVT_SHOW */
        shows++;
        framebuffer_test();
    } else if (type == 25) {                /* EVT_KEYPRESS */
        if (++keypresses >= 3) { L("3 key presses, step CloseApp"); p_CloseApp(); L("CloseApp returned"); }
    }
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
    L("probe5 started, pid=%d argv0=%s", (int)getpid(), argv[0]);
    lib = dlopen("libinkview.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) { L("dlopen failed: %s", dlerror()); return 2; }
    SYM(InkViewMain); SYM(FullUpdate); SYM(PartialUpdate); SYM(DynamicUpdate); SYM(DrawString);
    SYM(OpenFont); SYM(SetFont); SYM(ScreenWidth); SYM(ScreenHeight); SYM(GetOrientation);
    SYM(GetCurrentTask); SYM(GetTaskFramebuffer); SYM(SetPanelType); SYM(CloseApp); SYM(GetSoftwareVersion);
    if (p_GetSoftwareVersion) L("GetSoftwareVersion() = \"%s\"", p_GetSoftwareVersion());
    if (!p_InkViewMain || !p_FullUpdate || !p_PartialUpdate || !p_DynamicUpdate) { L("missing functions, giving up"); return 3; }
    L("calling InkViewMain");
    p_InkViewMain(handler);
    L("InkViewMain returned normally");
    return 0;
}
