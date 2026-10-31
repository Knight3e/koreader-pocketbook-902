/* probe9.c - button and event logger for the PocketBook Pro 902 (firmware 2.x).
 * Standard InkView skeleton (works on this device: no OpenScreen, no timers).
 * Shows a list of buttons to press; logs EVERY event (type, par1, par2) with milliseconds, so we get the key code of
 * each button and the event the tilt sensor sends. Closes after 40 key presses, or when the firmware exits it (Home).
 * Writes only /mnt/ext1/probe9.txt. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <sys/time.h>

#define OUTPATH "/mnt/ext1/probe9.txt"
typedef int (*iv_handler)(int, int, int);
static FILE *out;
static int fd_out = -1;
static void *lib, *font;
static long start_ms;
static int events, presses, shows;
static void (*p_InkViewMain)(iv_handler);
static void (*p_ClearScreen)(void);
static void (*p_FullUpdate)(void);
static void (*p_PartialUpdate)(int, int, int, int);
static void (*p_FillArea)(int, int, int, int, int);
static void (*p_DrawString)(int, int, const char *);
static void *(*p_OpenFont)(const char *, int, int);
static void (*p_SetFont)(void *, int);
static int (*p_ScreenWidth)(void);
static void (*p_CloseApp)(void);

static long now_ms(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec * 1000L + tv.tv_usec / 1000; }
static void L(const char *fmt, ...) {
    va_list ap;
    if (!out) return;
    fprintf(out, "[+%6ld ms] ", now_ms() - start_ms);
    va_start(ap, fmt); vfprintf(out, fmt, ap); va_end(ap);
    fputc('\n', out); fflush(out); fsync(fd_out);
}
static void on_signal(int sig) { L("SIGNAL %d", sig); _exit(128 + sig); }
#define SYM(name) do { *(void **)(&p_##name) = dlsym(lib, #name); if (!p_##name) L("sym %s MISSING", #name); } while (0)

static void draw_list(void) {
    static const char *lines[] = {
        "probe9: press each button ONCE, in this order",
        " 1 UP   2 DOWN   3 LEFT   4 RIGHT   5 OK (center)",
        " 6 PAGE-PREV   7 PAGE-NEXT",
        " 8 ZOOM +   9 ZOOM -",
        "10 VOLUME +   11 VOLUME -",
        "12 BOOK (chapters)   13 BACK",
        "14 any other button you have (menu?)",
        "15 now TILT the device sideways, then back upright",
        "16 finally press HOME (this ends the test)",
        NULL };
    int i;
    p_ClearScreen();
    if (font) { p_SetFont(font, 0); for (i = 0; lines[i]; i++) p_DrawString(40, 60 + i * 48, lines[i]); }
    p_FullUpdate();
}
static void show_last(int type, int code) {
    char buf[96];
    int w = p_ScreenWidth();
    if (!font) return;
    p_FillArea(30, 640, w - 60, 130, 0xFFFFFF);
    snprintf(buf, sizeof buf, "last event: type %d  code %d", type, code);
    p_SetFont(font, 0); p_DrawString(40, 660, buf);
    snprintf(buf, sizeof buf, "key presses so far: %d", presses);
    p_DrawString(40, 710, buf);
    p_PartialUpdate(30, 640, w - 60, 130);
}
static int handler(int type, int a, int b) {
    L("event %d: type=%d par1=%d par2=%d", events++, type, a, b);
    if (type == 21) font = p_OpenFont("LiberationSans", 30, 1);
    else if (type == 23 && shows++ == 0) draw_list();
    else if (type == 25) { presses++; show_last(type, a); if (presses >= 40) p_CloseApp(); }
    else if (type != 26 && type != 23 && type != 151 && type != 152) show_last(type, a);   /* e.g. the tilt event */
    return 0;
}
int main(void) {
    int i, sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGTERM, SIGHUP, SIGPIPE, 0 };
    start_ms = now_ms();
    out = fopen(OUTPATH, "w"); if (!out) return 1;
    fd_out = fileno(out);
    for (i = 0; sigs[i]; i++) signal(sigs[i], on_signal);
    L("probe9 started");
    lib = dlopen("libinkview.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) { L("dlopen failed: %s", dlerror()); return 2; }
    SYM(InkViewMain); SYM(ClearScreen); SYM(FullUpdate); SYM(PartialUpdate); SYM(FillArea);
    SYM(DrawString); SYM(OpenFont); SYM(SetFont); SYM(ScreenWidth); SYM(CloseApp);
    L("calling InkViewMain");
    p_InkViewMain(handler);
    L("InkViewMain returned");
    return 0;
}
