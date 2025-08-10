// kernel/kernel.c
// Terminal-style shell with a medium “fake FS”:
// CREATE <name> <size>, RENAME <old> <new>, DEL <name>, LIST, plus PAGE/END demo.

#include "../cpu/isr.h"
#include "../drivers/screen.h"
#include "kernel.h"
#include "../libc/string.h"
#include "../libc/mem.h"
#include <stdint.h>

/* ===== In-memory fake filesystem ===== */

#define MAX_FILES   16
#define MAX_NAME    16

typedef struct {
    char     name[MAX_NAME];
    uint32_t size;         // requested size (bytes)
    uint32_t alloc_bytes;  // rounded to page size (e.g., 4096)
    char*    data;         // kmalloc() returned virtual address
    uint32_t phys;         // physical address from kmalloc()
    uint8_t  used;         // 1 = in use, 0 = free slot
} FsEntry;

static FsEntry* g_dir = 0;       // directory table (allocated via kmalloc)
static uint32_t g_dir_phys = 0;  // physical addr of directory (for display)
static int g_dir_count = MAX_FILES;

/* --- small helpers (no <string.h>) --- */

static void to_upper_inplace(char* s) {
    for (; *s; ++s) if (*s >= 'a' && *s <= 'z') *s = *s - 'a' + 'A';
}

static char* next_token(char* p) { while (p && *p == ' ') p++; return (p && *p) ? p : 0; }
static char* cut_token (char* p) { while (*p && *p != ' ') p++; if (!*p) return 0; *p++ = 0; return p; }

/* bounded copy with guaranteed NUL */
static void copy_bounded(char* dst, const char* src, int maxlen) {
    int i = 0; if (maxlen <= 0) return;
    for (; i < maxlen - 1 && src && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

/* decimal itoa (positive) */
static void dec_to_ascii(uint32_t v, char out[16]) {
    char buf[16]; int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v && i < 15) { buf[i++] = '0' + (v % 10); v /= 10; }
    int j = 0; while (i) out[j++] = buf[--i]; out[j] = 0;
}

/* decimal parser (returns 0 if empty/invalid) */
static uint32_t parse_uint(const char* s) {
    uint32_t v = 0; int any = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') { any = 1; v = v*10 + (*s - '0'); s++; }
    return any ? v : 0;
}

/* page rounding */
static uint32_t round_up_page(uint32_t n) {
    const uint32_t P = 4096;
    return (n + P - 1) & ~(P - 1);
}

/* find file index by name (tutorial strcmp expects char*, not const) */
static int fs_find(char* name) {
    for (int i = 0; i < g_dir_count; i++)
        if (g_dir[i].used && strcmp(g_dir[i].name, name) == 0) return i;
    return -1;
}

/* allocate directory table and clear it */
static void fs_init(void) {
    g_dir = (FsEntry*) kmalloc(sizeof(FsEntry) * MAX_FILES, 1, &g_dir_phys);
    for (int i = 0; i < MAX_FILES; i++) g_dir[i].used = 0;

    kprint("FS init. dir@");
    char hx[16]; hex_to_ascii((uint32_t)g_dir, hx); kprint(hx);
    kprint(" phys@");       hex_to_ascii(g_dir_phys, hx);     kprint(hx);
    kprint("\n");
}

/* ===== Commands ===== */

static void cmd_list(void) {
    kprint("FILES:\n");
    for (int i = 0; i < g_dir_count; i++) {
        if (!g_dir[i].used) continue;
        kprint("  "); kprint(g_dir[i].name);
        kprint("  size=");
        char b[16]; dec_to_ascii(g_dir[i].size, b); kprint(b);
        kprint("B alloc=");
        dec_to_ascii(g_dir[i].alloc_bytes, b); kprint(b);
        kprint("B v@");
        char hx[16]; hex_to_ascii((uint32_t)g_dir[i].data, hx); kprint(hx);
        kprint(" p@"); hex_to_ascii(g_dir[i].phys, hx); kprint(hx);
        kprint("\n");
    }
}

/* CREATE <name> <size> */
static void cmd_create(char* name, char* size_str) {
    if (!name || !*name || !size_str || !*size_str) { kprint("usage: CREATE <name> <size>\n"); return; }
    if (strlen(name) >= MAX_NAME) { kprint("ERR: name too long\n"); return; }
    if (fs_find(name) >= 0) { kprint("ERR: exists\n"); return; }

    uint32_t req = parse_uint(size_str);
    if (req == 0) { kprint("ERR: size must be > 0\n"); return; }

    int slot = -1;
    for (int i = 0; i < g_dir_count; i++) if (!g_dir[i].used) { slot = i; break; }
    if (slot < 0) { kprint("ERR: directory full\n"); return; }

    uint32_t alloc = round_up_page(req);
    uint32_t phys = 0;
    char* data = (char*) kmalloc(alloc, 1, &phys);
    if (!data) { kprint("ERR: kmalloc failed\n"); return; }
    memory_set((uint8_t*)data, 0, alloc);   // instead of memset

    copy_bounded(g_dir[slot].name, name, MAX_NAME);
    g_dir[slot].size        = req;
    g_dir[slot].alloc_bytes = alloc;
    g_dir[slot].data        = data;
    g_dir[slot].phys        = phys;
    g_dir[slot].used        = 1;

    kprint("OK\n");
}

/* RENAME <old> <new> */
static void cmd_rename(char* oldn, char* newn) {
    if (!oldn || !*oldn || !newn || !*newn) { kprint("usage: RENAME <old> <new>\n"); return; }
    if (strlen(newn) >= MAX_NAME) { kprint("ERR: name too long\n"); return; }
    int idx = fs_find(oldn); if (idx < 0) { kprint("ERR: not found\n"); return; }
    if (fs_find(newn) >= 0)  { kprint("ERR: exists\n");    return; }
    copy_bounded(g_dir[idx].name, newn, MAX_NAME);
    kprint("OK\n");
}

/* DEL <name> */
static void cmd_del(char* name) {
    if (!name || !*name) { kprint("usage: DEL <name>\n"); return; }
    int idx = fs_find(name);
    if (idx < 0) { kprint("ERR: not found\n"); return; }
    g_dir[idx].used = 0;
    memory_set((uint8_t*)g_dir[idx].name, 0, MAX_NAME);
    g_dir[idx].size = g_dir[idx].alloc_bytes = g_dir[idx].phys = 0;
    g_dir[idx].data = 0;
    kprint("OK\n");
}

/* ===== Boot entry + shell hooks ===== */

void kernel_main() {
    isr_install();
    irq_install();

    asm("int $2");
    asm("int $3");

    fs_init();

    kprint("Mini-OS ready.\n");
    kprint("Commands: LIST | CREATE <name> <size> | RENAME <old> <new> | DEL <name> | PAGE | END\n\n");
    cmd_list();
    kprint("\n> ");
}

void user_input(char *input) {
    to_upper_inplace(input);

    if (strcmp(input, "END") == 0) {
        kprint("Stopping the CPU. Bye!\n");
        asm volatile("hlt");
        return;
    }

    if (strcmp(input, "PAGE") == 0) {
        uint32_t phys_addr;
        uint32_t page = kmalloc(1000, 1, &phys_addr);
        char page_str[16] = "", phys_str[16] = "";
        hex_to_ascii(page, page_str);
        hex_to_ascii(phys_addr, phys_str);
        kprint("Page: "); kprint(page_str);
        kprint(", physical address: "); kprint(phys_str);
        kprint("\n> ");
        return;
    }

    /* parse: CMD [arg1] [arg2] */
    char *p = input;
    char *cmd  = next_token(p); if (!cmd) { kprint("> "); return; }
    p = cut_token(cmd);
    char *arg1 = next_token(p); if (arg1) p = cut_token(arg1);
    char *arg2 = next_token(p); /* may be 0 */

    if (strcmp(cmd, "LIST") == 0) {
        cmd_list();
    } else if (strcmp(cmd, "CREATE") == 0) {
        cmd_create(arg1, arg2);
    } else if (strcmp(cmd, "RENAME") == 0) {
        cmd_rename(arg1, arg2);
    } else if (strcmp(cmd, "DEL") == 0) {
        cmd_del(arg1);
    } else {
        kprint("Unknown command\n");
    }

    kprint("> ");
}
