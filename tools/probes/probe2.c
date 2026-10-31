/* probe2.c - InkView probe for the PocketBook Pro 902 (firmware 2.x).
 * 1) logs GetSoftwareVersion() (KOReader parses this string and refuses to start if it looks too old)
 * 2) starts the normal InkView event loop, opens the screen, draws a frame (and text if a font opens)
 * 3) logs every event the firmware sends (type, par1, par2) to /mnt/ext1/probe2.txt
 * Press a few keys (arrows, OK, page buttons). The app closes itself after 6 key presses.
 * It only writes /mnt/ext1/probe2.txt and draws on the screen. No SDK needed: functions are looked up with dlsym. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <time.h>

#define OUTPATH "/mnt/ext1/probe2.txt"
typedef int (*iv_handler)(int type, int par1, int par2);

static FILE *out;
static void *lib;
static int events, keypresses, screen_open;
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
static char *(*p_GetSoftwareVersion)(void);

#define SYM(name) do { *(void **)(&p_##name) = dlsym(lib, #name); fprintf(out, "sym %-20s %s\n", #name, p_##name ? "found" : "MISSING"); } while (0)

static void draw(void) {
    int w, h, i;
    const char *fonts[] = { "LiberationSans", "DroidSans", "arial", NULL };
    if (!screen_open || !p_ScreenWidth || !p_ScreenHeight) return;
    w = p_ScreenWidth(); h = p_ScreenHeight();
    fprintf(out, "draw: screen %d x %d\n", w, h);
    if (p_ClearScreen) p_ClearScreen();
    if (p_FillArea) {
        p_FillArea(20, 20, w - 40, 6, 0);
        p_FillArea(20, h - 26, w - 40, 6, 0);
        p_FillArea(20, 20, 6, h - 40, 0);
        p_FillArea(w - 26, 20, 6, h - 40, 0);
    }
    if (p_OpenFont && p_SetFont && p_DrawString)
        for (i = 0; fonts[i]; i++) {
            void *f = p_OpenFont(fonts[i], 32, 1);
            fprintf(out, "OpenFont(%s): %s\n", fonts[i], f ? "ok" : "failed");
            if (f) { p_SetFont(f, 0); p_DrawString(60, 80, "probe2: press keys"); break; }
        }
    if (p_FullUpdate) p_FullUpdate();
}

static int handler(int type, int par1, int par2) {
    if (events < 400) fprintf(out, "event %d: type=%d par1=%d par2=%d\n", events, type, par1, par2);
    events++;
    if (type == 21 && !screen_open) {            /* first event: EVT_INIT */
        if (p_OpenScreen) { p_OpenScreen(); screen_open = 1; fprintf(out, "OpenScreen called\n"); }
        draw();
    } else if (type == 23 || type == 43) {      /* EVT_SHOW / EVT_REPAINT, depending on firmware */
        draw();
    } else if (type == 25) {                    /* EVT_KEYPRESS */
        if (++keypresses >= 6) {
            fprintf(out, "6 key presses seen, closing\n");
            fclose(out); out = fopen(OUTPATH, "a");
            if (p_CloseApp) p_CloseApp();
        }
    }
    if (out) fflush(out);
    return 0;
}

int main(void) {
    time_t now = time(NULL);
    out = fopen(OUTPATH, "w");
    if (!out) return 1;
    setvbuf(out, NULL, _IOLBF, 4096);
    fprintf(out, "probe2 started %s", ctime(&now));
    lib = dlopen("libinkview.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) { fprintf(out, "dlopen failed: %s\n", dlerror()); fclose(out); return 2; }
    SYM(InkViewMain); SYM(OpenScreen); SYM(ClearScreen); SYM(FullUpdate); SYM(FillArea); SYM(DrawString);
    SYM(OpenFont); SYM(SetFont); SYM(ScreenWidth); SYM(ScreenHeight); SYM(CloseApp); SYM(GetSoftwareVersion);
    if (p_GetSoftwareVersion) {
        char *v = p_GetSoftwareVersion();
        fprintf(out, "GetSoftwareVersion() = \"%s\"\n", v ? v : "(null)");
    }
    if (!p_InkViewMain) { fprintf(out, "no InkViewMain, giving up\n"); fclose(out); return 3; }
    fprintf(out, "calling InkViewMain...\n");
    p_InkViewMain(handler);
    fprintf(out, "InkViewMain returned\n");
    fclose(out);
    return 0;
}
