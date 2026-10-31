/* sigprobe.c - dlopen() the given libraries and, if the CPU hits an illegal instruction (or a crash),
 * print WHERE: program counter, return address, which library/file they belong to, nearest symbol.
 * Usage: sigprobe libinkview.so [other.so ...]   (output goes to stdout) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <ucontext.h>

static void say(const char *s) { (void)!write(1, s, strlen(s)); }

static void where(const char *what, unsigned long addr) {
    char line[512], buf[8192]; int fd = open("/proc/self/maps", O_RDONLY), n, i, ls = 0, found = 0;
    Dl_info di;
    snprintf(line, sizeof line, "%s = 0x%lx\n", what, addr); say(line);
    if (dladdr((void *)addr, &di)) {
        snprintf(line, sizeof line, "   dladdr: file=%s  nearest symbol=%s\n", di.dli_fname ? di.dli_fname : "?", di.dli_sname ? di.dli_sname : "(none)");
        say(line);
    }
    if (fd < 0) return;
    n = read(fd, buf, sizeof buf - 1); close(fd);
    if (n <= 0) return;
    buf[n] = 0;
    for (i = 0; i <= n; i++) if (buf[i] == '\n' || buf[i] == 0) {
        unsigned long a, b; char saved = buf[i]; buf[i] = 0;
        if (sscanf(buf + ls, "%lx-%lx", &a, &b) == 2 && addr >= a && addr < b) {
            snprintf(line, sizeof line, "   map line: %s   (offset in mapping: 0x%lx)\n", buf + ls, addr - a); say(line); found = 1;
        }
        buf[i] = saved; ls = i + 1;
    }
    if (!found) say("   (address not in any mapping)\n");
}

static void on_signal(int sig, siginfo_t *si, void *ctx) {
    char line[256];
    unsigned long pc = 0, lr = 0;
    snprintf(line, sizeof line, "\n*** SIGNAL %d (%s) fault_addr=%p\n", sig, strsignal(sig), si->si_addr); say(line);
#ifdef __arm__
    pc = ((ucontext_t *)ctx)->uc_mcontext.arm_pc; lr = ((ucontext_t *)ctx)->uc_mcontext.arm_lr;
    if (pc) { unsigned int insn = 0; if (!(((ucontext_t *)ctx)->uc_mcontext.arm_cpsr & 0x20)) insn = *(unsigned int *)pc;
        snprintf(line, sizeof line, "instruction word at pc (ARM mode): 0x%08x\n", insn); say(line); }
#elif defined(__x86_64__)
    pc = ((ucontext_t *)ctx)->uc_mcontext.gregs[REG_RIP]; (void)lr;
#else
    (void)ctx;
#endif
    where("pc", pc);
    if (lr) where("lr", lr);
    _exit(77);
}

int main(int argc, char **argv) {
    struct sigaction sa; int i, sigs[] = { SIGILL, SIGSEGV, SIGBUS, SIGFPE, SIGABRT, 0 };
    memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_signal; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
    for (i = 0; sigs[i]; i++) sigaction(sigs[i], &sa, NULL);
    if (argc > 1 && !strcmp(argv[1], "--selftest")) raise(SIGILL);
    for (i = 1; i < argc; i++) {
        void *h; char line[600];
        snprintf(line, sizeof line, "dlopen(%s) ...\n", argv[i]); say(line);
        h = dlopen(argv[i], RTLD_NOW | RTLD_GLOBAL);
        snprintf(line, sizeof line, "   -> %s\n", h ? "OK" : dlerror()); say(line);
    }
    say("sigprobe finished without a crash\n");
    return 0;
}
