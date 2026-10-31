/* probe6.c - copies KOReader's own start-up flow for old PocketBook firmware (base/input/inkview-compat.c):
 *   1. main thread: OpenScreen() FIRST, then GetTaskFramebuffer(GetCurrentTask()), draw test pattern directly
 *   2. InkViewMain() runs in a SEPARATE THREAD; its handler hands every event to the main thread through
 *      two semaphores; a 20 ms SetWeakTimer ("compat-poll") wakes the main loop
 *   3. main thread loops (like ProcessEventLoop), logs events, closes after 3 key presses
 * Logs every step with fsync, catches crashes (like probe3-5). Writes only /mnt/ext1/probe6.txt. */
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
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>

#define OUTPATH "/mnt/ext1/probe6.txt"
typedef int (*iv_handler)(int, int, int);
typedef void (*iv_timerproc)(void);
typedef struct { int width, height, scanline, depth, clipx1, clipx2, clipy1, clipy2; unsigned char *addr; } canvas_t;

static FILE *out;
static int fd_out = -1;
static void *lib;
static void *font;
static canvas_t *fb;
static long ticks;
static int loglines;

static void (*p_InkViewMain)(iv_handler);
static void (*p_OpenScreen)(void);
static void (*p_FullUpdate)(void);
static void (*p_PartialUpdate)(int, int, int, int);
static void (*p_DynamicUpdate)(int, int, int, int);
static void (*p_DrawString)(int, int, const char *);
static void *(*p_OpenFont)(const char *, int, int);
static void (*p_SetFont)(void *, int);
static int (*p_GetCurrentTask)(void);
static canvas_t *(*p_GetTaskFramebuffer)(int);
static void (*p_SetPanelType)(int);
static void (*p_SetWeakTimer)(const char *, iv_timerproc, int);
static void (*p_ClearTimer)(iv_timerproc);
static void (*p_LeaveInkViewMain)(void);
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

/* ---------- KOReader's compat layer (copied in spirit from base/input/inkview-compat.c) ---------- */
static pthread_t ivm_thread;
static sem_t sem_notify, sem_poll;
static int abc[3];
static int exiting;

static void poll_timer(void) {
    p_SetWeakTimer("compat-poll", poll_timer, 20);   /* re-arm */
    abc[0] = -1;                                     /* signals expiry */
    sem_post(&sem_notify);
    sem_wait(&sem_poll);
}

static int ivm_proc(int a, int b, int c) {
    if (a == 21) {                                   /* EVT_INIT */
        p_SetWeakTimer("compat-poll", poll_timer, 20);
        sem_wait(&sem_poll);
    }
    abc[0] = a; abc[1] = b; abc[2] = c;
    if (a == 22) exiting = 1; else if (exiting) return 2;
    sem_post(&sem_notify);
    sem_wait(&sem_poll);
    return 0;
}

static void *ivm_thread_fn(void *arg) { (void)arg; p_InkViewMain(ivm_proc); return NULL; }

/* ---------- drawing helpers (8 bit gray framebuffer, as measured in probe5) ---------- */
static void fill(int x, int y, int w, int h, int gray) {
    int yy;
    for (yy = y; yy < y + h && yy < fb->height; yy++) memset(fb->addr + (long)yy * fb->scanline + x, gray, w);
}

int main(int argc, char **argv) {
    struct sigaction sa;
    int i, sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTERM, SIGHUP, SIGPIPE, SIGINT, SIGQUIT, 0 };
    int keypresses = 0, events = 0, task, done = 0, shows = 0;
    long t0, last_tick_log = 0;
    struct timespec ts;
    void *ret;

    out = fopen(OUTPATH, "w");
    if (!out) return 1;
    fd_out = fileno(out);
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_signal; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
    for (i = 0; sigs[i]; i++) sigaction(sigs[i], &sa, NULL);
    L("probe6 started, pid=%d argv0=%s argc=%d", (int)getpid(), argv[0], argc);
    lib = dlopen("libinkview.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) { L("dlopen failed: %s", dlerror()); return 2; }
    SYM(InkViewMain); SYM(OpenScreen); SYM(FullUpdate); SYM(PartialUpdate); SYM(DynamicUpdate); SYM(DrawString);
    SYM(OpenFont); SYM(SetFont); SYM(GetCurrentTask); SYM(GetTaskFramebuffer); SYM(SetPanelType);
    SYM(SetWeakTimer); SYM(ClearTimer); SYM(LeaveInkViewMain); SYM(GetSoftwareVersion);
    if (p_GetSoftwareVersion) L("GetSoftwareVersion() = \"%s\"", p_GetSoftwareVersion());
    if (!p_InkViewMain || !p_OpenScreen || !p_GetTaskFramebuffer || !p_SetWeakTimer) { L("missing functions, giving up"); return 3; }

    /* step 1: like KOReader's framebuffer init */
    L("step OpenScreen (main thread, BEFORE InkViewMain)");
    p_OpenScreen();
    L("ok");
    task = p_GetCurrentTask();
    fb = p_GetTaskFramebuffer(task);
    L("task=%d framebuffer=%p", task, (void *)fb);
    if (!fb || !fb->addr || fb->depth != 8) { L("unexpected framebuffer, giving up"); return 4; }
    L("canvas: %d x %d scanline=%d depth=%d addr=%p", fb->width, fb->height, fb->scanline, fb->depth, (void *)fb->addr);
    memset(fb->addr, 0xFF, (long)fb->scanline * fb->height);
    fill(40, 100, fb->width - 80, 80, 0);                 /* black bar */
    for (i = 0; i < 8; i++) fill(40, 260 + i * 60, fb->width - 80, 56, i * 36);   /* gray bands */
    t0 = now_ms(); p_FullUpdate(); L("FullUpdate (direct pixels) took %ld ms", now_ms() - t0);

    /* step 2: like KOReader's input init: InkViewMain in its own thread */
    sem_init(&sem_notify, 0, 0); sem_init(&sem_poll, 0, 0);
    L("step pthread_create(InkViewMain)");
    if (pthread_create(&ivm_thread, NULL, ivm_thread_fn, NULL)) { L("pthread_create failed"); return 5; }
    L("thread started; entering event loop");

    /* step 3: like ProcessEventLoop() called from KOReader's main loop */
    while (!done) {
        int a, b, c;
        sem_post(&sem_poll);
        sem_wait(&sem_notify);
        a = abc[0]; b = abc[1]; c = abc[2];
        if (a == -1) {                                    /* poll tick */
            ticks++;
            if (ticks == 1) L("first poll tick received");
            if (now_ms() - last_tick_log > 5000 && loglines < 30) { last_tick_log = now_ms(); loglines++; L("poll ticks so far: %ld", ticks); }
            continue;
        }
        events++;
        if (events < 200) L("event %d: type=%d par1=%d par2=%d", events, a, b, c);
        if (a == 21) {                                   /* EVT_INIT */
            font = p_OpenFont("LiberationSans", 32, 1);
            if (p_SetPanelType) p_SetPanelType(0);
        } else if (a == 23 && shows++ == 0) {            /* EVT_SHOW */
            if (font) { p_SetFont(font, 0); p_DrawString(60, 40, "probe6: thread loop alive, press 3 keys"); }
            t0 = now_ms(); p_PartialUpdate(0, 0, fb->width, 100); L("PartialUpdate of title took %ld ms", now_ms() - t0);
        } else if (a == 25) {                            /* EVT_KEYPRESS */
            if (++keypresses >= 3) { L("3 key presses -> leaving InkViewMain"); done = 1; }
        } else if (a == 22) {                            /* EVT_EXIT */
            L("EVT_EXIT received"); done = 1;
        }
    }

    /* step 4: like ClearOnExit() */
    L("clearing: ClearTimer, LeaveInkViewMain, joining thread (ticks total %ld)", ticks);
    if (p_ClearTimer) p_ClearTimer(poll_timer);
    if (!exiting && p_LeaveInkViewMain) { p_LeaveInkViewMain(); exiting = 1; }
    sem_post(&sem_poll); sem_post(&sem_poll);
    { struct timeval tv; gettimeofday(&tv, NULL); ts.tv_sec = tv.tv_sec + 5; ts.tv_nsec = tv.tv_usec * 1000L; }
    i = pthread_timedjoin_np(ivm_thread, &ret, &ts);
    L("pthread_timedjoin_np -> %d (%s)", i, i == 0 ? "joined" : (i == ETIMEDOUT ? "timed out" : "error"));
    L("done");
    fclose(out);
    _exit(0);
}
