/* probe7.c - bisect probe. probe6 (OpenScreen in main + InkViewMain in a thread) got EVT_BACKGROUND (152)
 * about one second after start and never received key events. This file builds two variants:
 *   MODE 1 (probe7a): OpenScreen() before InkViewMain, but InkViewMain runs in the MAIN thread (classic layout)
 *   MODE 2 (probe7b): NO OpenScreen; InkViewMain in a thread with KOReader's semaphore/weak-timer hand-over;
 *                     framebuffer taken after the first EVT_SHOW
 *   MODE 3 (probe7c): OpenScreen() in main, THEN InkViewMain in a thread (= KOReader's fallback order),
 *                     but NO drawing before the thread starts (draw only after the first EVT_SHOW)
 *   MODE 4 (probe7d): like probe6: OpenScreen() + direct drawing + FullUpdate in main, THEN the thread
 * Every log line has milliseconds since start. Do NOT press any key for the first 8 seconds.
 * Then press 3 keys: the app closes. Writes only /mnt/ext1/probe7<a|b>.txt. */
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
#include <mqueue.h>

#ifndef MODE
#error define MODE
#endif
#if MODE == 1
#define OUTPATH "/mnt/ext1/probe7a.txt"
#elif MODE == 2
#define OUTPATH "/mnt/ext1/probe7b.txt"
#elif MODE == 3
#define OUTPATH "/mnt/ext1/probe7c.txt"
#else
#define OUTPATH "/mnt/ext1/probe7d.txt"
#endif
typedef int (*iv_handler)(int, int, int);
typedef void (*iv_timerproc)(void);
typedef struct { int width, height, scanline, depth, clipx1, clipx2, clipy1, clipy2; unsigned char *addr; } canvas_t;

static FILE *out;
static int fd_out = -1;
static void *lib, *font;
static canvas_t *fb;
static long start_ms, ticks;
static int keypresses, shows;

static void (*p_InkViewMain)(iv_handler);
static void (*p_OpenScreen)(void);
static void (*p_FullUpdate)(void);
static void (*p_PartialUpdate)(int, int, int, int);
static void (*p_DrawString)(int, int, const char *);
static void *(*p_OpenFont)(const char *, int, int);
static void (*p_SetFont)(void *, int);
static int (*p_GetCurrentTask)(void);
static canvas_t *(*p_GetTaskFramebuffer)(int);
static void (*p_SetWeakTimer)(const char *, iv_timerproc, int);
static void (*p_ClearTimer)(iv_timerproc);
static void (*p_LeaveInkViewMain)(void);
static void (*p_CloseApp)(void);

static long now_ms(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec * 1000L + tv.tv_usec / 1000; }
static void L(const char *fmt, ...) {
    va_list ap;
    if (!out) return;
    fprintf(out, "[+%5ld ms] ", now_ms() - start_ms);
    va_start(ap, fmt); vfprintf(out, fmt, ap); va_end(ap);
    fputc('\n', out); fflush(out); fsync(fd_out);
}
static void on_signal(int sig, siginfo_t *si, void *ctx) {
    void *bt[32]; int n, fd, r; char buf[4096];
    L("SIGNAL %d (%s) code=%d fault_addr=%p", sig, strsignal(sig), si->si_code, si->si_addr);
#ifdef __arm__
    L("pc=0x%lx lr=0x%lx", ((ucontext_t *)ctx)->uc_mcontext.arm_pc, ((ucontext_t *)ctx)->uc_mcontext.arm_lr);
#endif
    n = backtrace(bt, 32); L("backtrace (%d frames):", n); backtrace_symbols_fd(bt, n, fd_out);
    fd = open("/proc/self/maps", O_RDONLY);
    if (fd >= 0) { L("memory map:"); while ((r = read(fd, buf, sizeof buf)) > 0) (void)write(fd_out, buf, r); close(fd); }
    fsync(fd_out); _exit(128 + sig);
}
#define SYM(name) do { *(void **)(&p_##name) = dlsym(lib, #name); if (!p_##name) L("sym %s MISSING", #name); } while (0)

static void fill(int x, int y, int w, int h, int gray) {
    int yy;
    for (yy = y; yy < y + h && yy < fb->height; yy++) memset(fb->addr + (long)yy * fb->scanline + x, gray, w);
}
static void draw_all(const char *title) {
    int i;
    memset(fb->addr, 0xFF, (long)fb->scanline * fb->height);
    fill(40, 100, fb->width - 80, 80, 0);
    for (i = 0; i < 8; i++) fill(40, 260 + i * 60, fb->width - 80, 56, i * 36);
    if (font) { p_SetFont(font, 0); p_DrawString(60, 40, title); }
    p_FullUpdate();
}

#if MODE == 1
/* ---------- variant a: classic handler in the main thread ---------- */
static int handler1(int type, int a, int b) {
    L("event: type=%d par1=%d par2=%d", type, a, b);
    if (type == 21) font = p_OpenFont("LiberationSans", 32, 1);
    else if (type == 23 && shows++ == 0) { L("drawing"); draw_all("probe7a: wait 8 s, then press 3 keys"); L("drawn"); }
    else if (type == 25 && ++keypresses >= 3) { L("3 keys -> CloseApp"); p_CloseApp(); }
    return 0;
}
#else
/* ---------- variant b: KOReader's thread + semaphore + weak timer hand-over ---------- */
static pthread_t ivm_thread;
static sem_t sem_notify, sem_poll;
static int abc[3], exiting;
static void poll_timer(void) { p_SetWeakTimer("compat-poll", poll_timer, 20); abc[0] = -1; sem_post(&sem_notify); sem_wait(&sem_poll); }
static int ivm_proc(int a, int b, int c) {
    if (a == 21) { p_SetWeakTimer("compat-poll", poll_timer, 20); sem_wait(&sem_poll); }
    abc[0] = a; abc[1] = b; abc[2] = c;
    if (a == 22) exiting = 1; else if (exiting) return 2;
    sem_post(&sem_notify); sem_wait(&sem_poll);
    return 0;
}
static void *ivm_thread_fn(void *x) { (void)x; p_InkViewMain(ivm_proc); return NULL; }
#endif

int main(int argc, char **argv) {
    struct sigaction sa;
    int i, sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTERM, SIGHUP, SIGPIPE, SIGINT, SIGQUIT, 0 };
    start_ms = now_ms();
    out = fopen(OUTPATH, "w");
    if (!out) return 1;
    fd_out = fileno(out);
    memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_signal; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
    for (i = 0; sigs[i]; i++) sigaction(sigs[i], &sa, NULL);
    L("probe7 MODE %d started pid=%d argc=%d", MODE, (int)getpid(), argc); (void)argv;
    lib = dlopen("libinkview.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) { L("dlopen failed: %s", dlerror()); return 2; }
    SYM(InkViewMain); SYM(OpenScreen); SYM(FullUpdate); SYM(PartialUpdate); SYM(DrawString); SYM(OpenFont);
    SYM(SetFont); SYM(GetCurrentTask); SYM(GetTaskFramebuffer); SYM(SetWeakTimer); SYM(ClearTimer);
    SYM(LeaveInkViewMain); SYM(CloseApp);
    {
        mqd_t q = mq_open("/hwevent", O_RDONLY | O_NONBLOCK);
        L("mq_open(\"/hwevent\") -> %d (%s)", (int)q, q == (mqd_t)-1 ? strerror(errno) : "ok");
        if (q != (mqd_t)-1) mq_close(q);
    }
#if MODE == 1
    L("OpenScreen (main thread, before InkViewMain)"); p_OpenScreen(); L("OpenScreen returned");
    fb = p_GetTaskFramebuffer(p_GetCurrentTask());
    L("framebuffer %p", (void *)fb);
    L("calling InkViewMain in the MAIN thread");
    p_InkViewMain(handler1);
    L("InkViewMain returned");
    return 0;
#else
    {
        int done = 0, events = 0;
        sem_init(&sem_notify, 0, 0); sem_init(&sem_poll, 0, 0);
#if MODE >= 3
        L("OpenScreen (main thread, before starting the InkViewMain thread)"); p_OpenScreen(); L("OpenScreen returned");
        fb = p_GetTaskFramebuffer(p_GetCurrentTask());
        L("framebuffer %p", (void *)fb);
#if MODE == 4
        if (fb && fb->addr && fb->depth == 8) { L("early drawing + FullUpdate in main thread"); draw_all("early"); L("early drawing done"); }
#endif
#else
        L("no OpenScreen");
#endif
        L("starting InkViewMain thread");
        if (pthread_create(&ivm_thread, NULL, ivm_thread_fn, NULL)) { L("pthread_create failed"); return 5; }
        while (!done) {
            int a, b, c;
            sem_post(&sem_poll); sem_wait(&sem_notify);
            a = abc[0]; b = abc[1]; c = abc[2];
            if (a == -1) { ticks++; continue; }
            events++;
            L("event: type=%d par1=%d par2=%d (poll ticks so far %ld)", a, b, c, ticks);
            if (a == 21) font = p_OpenFont("LiberationSans", 32, 1);
            else if (a == 23 && shows++ == 0) {
                if (!fb) fb = p_GetTaskFramebuffer(p_GetCurrentTask());
                L("framebuffer %p, drawing", (void *)fb);
                if (fb && fb->addr && fb->depth == 8) { draw_all(MODE == 2 ? "probe7b: wait 8 s, then press 3 keys" : MODE == 3 ? "probe7c: wait 8 s, then press 3 keys" : "probe7d: wait 8 s, then press 3 keys"); L("drawn"); }
            } else if (a == 25 && ++keypresses >= 3) { L("3 keys -> leaving"); done = 1; }
            else if (a == 22) { L("EVT_EXIT"); done = 1; }
        }
        L("ClearTimer + LeaveInkViewMain, then _exit");
        if (p_ClearTimer) p_ClearTimer(poll_timer);
        if (!exiting && p_LeaveInkViewMain) { p_LeaveInkViewMain(); exiting = 1; }
        sem_post(&sem_poll); sem_post(&sem_poll);
        sleep(2);
        L("done");
        fclose(out); _exit(0);
    }
#endif
}
