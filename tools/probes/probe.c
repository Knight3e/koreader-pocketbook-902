/* probe.c - read-only device probe for the PocketBook Pro 902 (firmware 2.x).
 * Writes a text report to OUTPATH (on the e-reader's storage, so it shows up when you plug in USB).
 * It does NOT write anywhere else and does not change any system file.
 * Two builds: static (-DSTATIC_BUILD, no shared libs needed) and dynamic (tests glibc + dlopen). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#ifndef STATIC_BUILD
#include <dlfcn.h>
#include <gnu/libc-version.h>
#endif

#ifndef OUTPATH
#define OUTPATH "/mnt/ext1/probe.txt"
#endif
#ifndef BUILD_NAME
#define BUILD_NAME "probe"
#endif

static FILE *out;

static void dump_file(const char *path, int maxlines) {
    FILE *f = fopen(path, "r");
    char line[512];
    int n = 0;
    if (!f) { fprintf(out, "--- %s: cannot open (%s)\n", path, strerror(errno)); return; }
    fprintf(out, "--- %s\n", path);
    while (fgets(line, sizeof line, f) && (maxlines <= 0 || n < maxlines)) { fputs(line, out); n++; }
    fclose(f);
}

static void list_dir(const char *path, int maxn) {
    DIR *d = opendir(path);
    struct dirent *e;
    int n = 0;
    if (!d) { fprintf(out, "--- dir %s: cannot open (%s)\n", path, strerror(errno)); return; }
    fprintf(out, "--- dir %s\n", path);
    while ((e = readdir(d)) && n < maxn) { fprintf(out, "%s\n", e->d_name); n++; }
    closedir(d);
}

static void elf_info(const char *path) {
    Elf32_Ehdr h;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(out, "elf %s: cannot open (%s)\n", path, strerror(errno)); return; }
    if (fread(&h, 1, sizeof h, f) != sizeof h || memcmp(h.e_ident, ELFMAG, 4) != 0) {
        fprintf(out, "elf %s: not an ELF file\n", path);
    } else {
        fprintf(out, "elf %s: class=%d machine=%d flags=0x%08x -> EABI version %u\n", path,
                h.e_ident[EI_CLASS], h.e_machine, (unsigned)h.e_flags, (unsigned)((h.e_flags >> 24) & 0xff));
    }
    fclose(f);
}

/* list the functions a shared library exports (ELF32 .dynsym), without loading it */
static void dump_dynsyms(const char *path) {
    struct stat st;
    unsigned char *m;
    Elf32_Ehdr *h;
    Elf32_Shdr *sh;
    int fd = open(path, O_RDONLY), i;
    unsigned j;
    if (fd < 0) { fprintf(out, "syms %s: cannot open (%s)\n", path, strerror(errno)); return; }
    if (fstat(fd, &st) || st.st_size < (off_t)sizeof(Elf32_Ehdr)) { close(fd); fprintf(out, "syms %s: too small\n", path); return; }
    m = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (m == MAP_FAILED) { close(fd); fprintf(out, "syms %s: mmap failed\n", path); return; }
    h = (Elf32_Ehdr *)m;
    if (memcmp(h->e_ident, ELFMAG, 4) || h->e_ident[EI_CLASS] != ELFCLASS32 ||
        h->e_shoff == 0 || h->e_shoff + (unsigned long)h->e_shnum * sizeof(Elf32_Shdr) > (unsigned long)st.st_size) {
        fprintf(out, "syms %s: not a usable ELF32 file\n", path);
        munmap(m, st.st_size); close(fd); return;
    }
    sh = (Elf32_Shdr *)(m + h->e_shoff);
    fprintf(out, "--- exported symbols of %s\n", path);
    for (i = 0; i < h->e_shnum; i++) {
        Elf32_Sym *s; const char *str; unsigned n;
        if (sh[i].sh_type != SHT_DYNSYM) continue;
        if (sh[i].sh_link >= h->e_shnum || sh[i].sh_offset + sh[i].sh_size > (unsigned long)st.st_size) continue;
        s = (Elf32_Sym *)(m + sh[i].sh_offset);
        str = (const char *)(m + sh[sh[i].sh_link].sh_offset);
        n = sh[i].sh_size / sizeof(Elf32_Sym);
        for (j = 0; j < n; j++)
            if (s[j].st_shndx != SHN_UNDEF && s[j].st_name && ELF32_ST_TYPE(s[j].st_info) == STT_FUNC)
                fprintf(out, "%s\n", str + s[j].st_name);
    }
    munmap(m, st.st_size); close(fd);
}

int main(int argc, char **argv) {
    struct utsname u;
    time_t now = time(NULL);
    const char *libs[] = { "/ebrmain/lib/libinkview.so", "/usr/lib/libinkview.so", "/lib/libinkview.so", NULL };
    int i;

    out = fopen(OUTPATH, "w");
    if (!out) return 1;
    setvbuf(out, NULL, _IOLBF, 4096);
    fprintf(out, "build: %s\ntime: %s", BUILD_NAME, ctime(&now));
    if (uname(&u) == 0) fprintf(out, "kernel: %s %s %s\n", u.sysname, u.release, u.machine);
#ifndef STATIC_BUILD
    fprintf(out, "glibc at runtime: %s\n", gnu_get_libc_version());
#endif
    dump_file("/proc/version", 0);
    dump_file("/proc/cpuinfo", 40);
    dump_file("/proc/meminfo", 5);
    elf_info("/proc/self/exe");
    elf_info("/bin/busybox");
    elf_info("/lib/libc.so.6");
    list_dir("/", 80);
    list_dir("/ebrmain", 80);
    list_dir("/ebrmain/lib", 120);
    list_dir("/lib", 120);
    list_dir("/usr/lib", 120);
    if (argc > 1) dump_dynsyms(argv[1]);          /* used only for testing on a PC */
    for (i = 0; libs[i]; i++)
        if (access(libs[i], R_OK) == 0) { dump_dynsyms(libs[i]); break; }
#ifndef STATIC_BUILD
    {
        void *hnd = dlopen("libinkview.so", RTLD_LAZY);
        fprintf(out, "--- dlopen(libinkview.so): %s\n", hnd ? "OK" : dlerror());
        if (hnd) {
            const char *names[] = { "InkViewMain", "OpenScreen", "ClearScreen", "SoftUpdate", "FullUpdate",
                                    "PartialUpdate", "DynamicUpdate", "SetOrientation", "GetOrientation",
                                    "ScreenWidth", "ScreenHeight", NULL };
            for (i = 0; names[i]; i++) fprintf(out, "dlsym %-16s %s\n", names[i], dlsym(hnd, names[i]) ? "found" : "MISSING");
        }
    }
#endif
    fprintf(out, "--- done\n");
    fclose(out);
    return 0;
}
