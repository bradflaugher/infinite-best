/* mgba_server - a tiny headless mGBA front end driven over stdin/stdout, so Python tests
 * can run the ROM on a cycle-accurate emulator (tools/mgba/mgba_pyboy.py wraps it in a
 * PyBoy-like API).  Build: make build/mgba_server   (needs libmgba-dev).
 *
 * Commands (one per line), replies one line each:
 *   t N          run N frames                         -> "ok"
 *   k MASK       set held keys (A=1 B=2 SEL=4 ST=8 R=16 L=32 U=64 D=128) -> "ok"
 *   r ADDR       read a byte on the bus               -> "VAL"
 *   R ADDR LEN   read LEN bytes                       -> hex string
 *   w ADDR VAL   write a byte (raw)                   -> "ok"
 *   V OFF LEN    read LEN bytes of VRAM as stored (OFF 0x0000-0x1FFF is bank 0, from
 *                0x2000 the CGB's bank 1), whatever the LCD is doing -> hex string
 *   S OFF LEN    read LEN bytes of cartridge RAM, enabled or not -> hex string
 *   i            longest VBlank and STAT interrupt handler seen by T, in dots (456 per
 *                scanline, 4560 per VBlank), and the fewest STAT interrupts served in
 *                one frame; then reset                -> "VBL STAT MIN"
 *   s PATH       save the screen as a binary PPM      -> "ok"
 *   T N          run N frames one instruction at a time, watching every store to VRAM
 *                (0x8000-0x9FFF) and to the CGB palette data ports; a store the PPU
 *                refuses because the LCD is in mode 3 is a write real hardware drops too
 *                -> "DROPPED TOTAL [PC:ADDR ...]" (dropped count, stores seen, first few)
 *   q            quit
 */
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba-util/vfs.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/sm83/sm83.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void quiet_log(struct mLogger *l, int cat, enum mLogLevel lvl, const char *fmt, va_list a)
{ (void)l; (void)cat; (void)lvl; (void)fmt; (void)a; }
static struct mLogger logger = { .log = quiet_log };

/* The address and value of the store the next instruction makes, if it is a plain store. */
static int next_store(struct mCore *core, struct SM83Core *cpu, uint16_t *addr, uint8_t *val)
{
    uint16_t pc = cpu->pc;
    uint8_t op = (uint8_t)core->busRead8(core, pc);
    uint16_t hl = (uint16_t)(cpu->h << 8 | cpu->l);
    *val = cpu->a;
    switch (op) {
    case 0x02: *addr = (uint16_t)(cpu->b << 8 | cpu->c); return 1;
    case 0x12: *addr = (uint16_t)(cpu->d << 8 | cpu->e); return 1;
    case 0x22: case 0x32: case 0x77: *addr = hl; return 1;
    case 0x70: *addr = hl; *val = cpu->b; return 1;
    case 0x71: *addr = hl; *val = cpu->c; return 1;
    case 0x72: *addr = hl; *val = cpu->d; return 1;
    case 0x73: *addr = hl; *val = cpu->e; return 1;
    case 0x74: *addr = hl; *val = cpu->h; return 1;
    case 0x75: *addr = hl; *val = cpu->l; return 1;
    case 0x36: *addr = hl; *val = (uint8_t)core->busRead8(core, (uint16_t)(pc + 1)); return 1;
    case 0xE0: *addr = (uint16_t)(0xFF00 | core->busRead8(core, (uint16_t)(pc + 1))); return 1;
    case 0xE2: *addr = (uint16_t)(0xFF00 | cpu->c); return 1;
    case 0xEA: *addr = (uint16_t)(core->busRead8(core, (uint16_t)(pc + 1)) |
                                  core->busRead8(core, (uint16_t)(pc + 2)) << 8); return 1;
    default: return 0;
    }
}

static FILE *isr_log;
static unsigned stat_min = ~0u;  /* fewest STAT interrupts served in one frame */
static uint32_t isr_max[2];   /* longest VBlank / STAT handler seen by `T`, in dots; `i` reads + clears */

/* IB_TRACE=1: print each dropped store with the 48 instructions before it (pc@line.mode) */
static int trace;
static uint16_t tr_pc[64]; static uint8_t tr_ly[64], tr_mode[64]; static unsigned tr_n;
static void watch_frames(struct mCore *core, unsigned frames)
{
    struct GB *gb = core->board;
    struct SM83Core *cpu = core->cpu;
    unsigned dropped = 0, total = 0, shown = 0;
    static int in_isr; static uint16_t isr_sp; static uint32_t isr_t0; static int isr_vec;
    char out[256]; size_t o = 0;
    out[0] = 0;
    while (frames--) {
        uint32_t f0 = gb->video.frameCounter;
        unsigned stat_n = 0;
        while (gb->video.frameCounter == f0) {
            uint16_t addr = 0, pc = cpu->pc; uint8_t val = 0;
            int st = cpu->executionState == SM83_CORE_FETCH && !cpu->halted && next_store(core, cpu, &addr, &val);
            int vram = st && addr >= 0x8000 && addr < 0xA000;
            int pal = st && (addr == 0xFF69 || addr == 0xFF6B);
            int lcd_on = gb->memory.io[0x40] & 0x80;
            uint16_t sp0 = cpu->sp;
            /* how long each interrupt handler runs, in dots (a scanline is 456, VBlank 4560) */
            if (pc == 0x48 && cpu->executionState == SM83_CORE_FETCH) stat_n++;
            if (!in_isr && (pc == 0x40 || pc == 0x48) && cpu->executionState == SM83_CORE_FETCH) {
                in_isr = 1; isr_sp = cpu->sp; isr_vec = pc == 0x48; isr_t0 = mTimingCurrentTime(&gb->timing);
            }
            if (trace) { tr_pc[tr_n & 63] = pc; tr_ly[tr_n & 63] = gb->video.ly; tr_mode[tr_n & 63] = gb->video.mode; tr_n++; }
            core->step(core);
            if (in_isr && cpu->sp > isr_sp) {
                uint32_t dt = (uint32_t)mTimingCurrentTime(&gb->timing) - isr_t0;
                if (dt > isr_max[isr_vec]) isr_max[isr_vec] = dt;
                if (isr_log && !isr_vec) fprintf(isr_log, "%u\n", dt / 2);   /* timing units are half dots */
                in_isr = 0;
            }
            /* SP moved: the step entered an interrupt instead; the store runs after it returns */
            if (!(vram || pal) || !lcd_on || cpu->sp != sp0) continue;
            total++;
            if (vram ? gb->video.vramBank[addr & 0x1FFF] != val : gb->video.mode == 3) {
                dropped++;
                if (trace) {
                    fprintf(stderr, "DROP pc %04X addr %04X val %02X ly %d mode %d in_isr %d\n", pc, addr, val, gb->video.ly, gb->video.mode, in_isr);
                    for (unsigned i = tr_n - 48; i < tr_n; i++) fprintf(stderr, " %04X@%d.%d", tr_pc[i & 63], tr_ly[i & 63], tr_mode[i & 63]);
                    fprintf(stderr, "\n");
                }
                if (shown < 8) { shown++; o += (size_t)snprintf(out + o, sizeof out - o, " %04X:%04X", pc, addr); }
            }
        }
        if (stat_n < stat_min) stat_min = stat_n;
    }
    printf("%u %u%s\n", dropped, total, out);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: mgba_server ROM dmg|cgb [SAVE]\n"); return 2; }
    mLogSetDefaultLogger(&logger);
    struct mCore *core = mCoreFind(argv[1]);
    if (!core || !core->init(core)) { fprintf(stderr, "no core\n"); return 1; }
    mCoreInitConfig(core, NULL);
    /* a CGB-compatible ROM takes its model from cgb.model (gb.model is for DMG-only ROMs) */
    mCoreConfigSetValue(&core->config, "gb.model", strcmp(argv[2], "dmg") == 0 ? "DMG" : "CGB");
    mCoreConfigSetValue(&core->config, "cgb.model", strcmp(argv[2], "dmg") == 0 ? "DMG" : "CGB");
    mCoreConfigSetValue(&core->config, "cgb.hybridModel", strcmp(argv[2], "dmg") == 0 ? "DMG" : "CGB");
    mCoreConfigSetValue(&core->config, "useBios", "0");
    core->loadConfig(core, &core->config);
    unsigned w, h;
    core->desiredVideoDimensions(core, &w, &h);
    color_t *buf = calloc((size_t)w * h, sizeof(color_t));
    core->setVideoBuffer(core, buf, w);
    if (!mCoreLoadFile(core, argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
    if (argc > 3) {
        struct VFile *vf = VFileOpen(argv[3], O_RDWR | O_CREAT);
        if (vf) core->loadSave(core, vf);
    }
    core->reset(core);
    char line[512];
    trace = getenv("IB_TRACE") != NULL;
    /* IB_ISRLOG=file: the length of every VBlank handler seen by T, in dots, one per line */
    if (getenv("IB_ISRLOG")) isr_log = fopen(getenv("IB_ISRLOG"), "w");
    setvbuf(stdout, NULL, _IOLBF, 0);
    while (fgets(line, sizeof line, stdin)) {
        unsigned a = 0, b = 0;
        switch (line[0]) {
        case 't': sscanf(line + 1, "%u", &a); while (a--) core->runFrame(core); puts("ok"); break;
        case 'T': sscanf(line + 1, "%u", &a); watch_frames(core, a); break;
        case 'i': printf("%u %u %u\n", isr_max[0] / 2, isr_max[1] / 2, stat_min);
            isr_max[0] = isr_max[1] = 0; stat_min = ~0u; break;
        case 'k': sscanf(line + 1, "%u", &a); core->setKeys(core, a); puts("ok"); break;
        case 'r': sscanf(line + 1, "%u", &a); printf("%u\n", core->busRead8(core, a)); break;
        case 'R': sscanf(line + 1, "%u %u", &a, &b);
            for (unsigned i = 0; i < b; i++) printf("%02x", core->busRead8(core, a + i));
            putchar('\n'); break;
        case 'V': { struct GB *gb = core->board;
            sscanf(line + 1, "%u %u", &a, &b);
            for (unsigned i = 0; i < b; i++) printf("%02x", a + i < 0x4000 ? gb->video.vram[a + i] : 0xFF);
            putchar('\n'); break; }
        case 'S': { struct GB *gb = core->board;
            sscanf(line + 1, "%u %u", &a, &b);
            for (unsigned i = 0; i < b; i++) printf("%02x", a + i < gb->sramSize ? gb->memory.sram[a + i] : 0xFF);
            putchar('\n'); break; }
        case 'w': sscanf(line + 1, "%u %u", &a, &b); core->rawWrite8(core, a, -1, (uint8_t)b); puts("ok"); break;
        case 's': {
            char path[400]; sscanf(line + 1, "%399s", path);
            FILE *f = fopen(path, "wb");
            if (f) {
                fprintf(f, "P6\n%u %u\n255\n", 160u, 144u);
                for (unsigned y = 0; y < 144; y++) for (unsigned x = 0; x < 160; x++) {
                    color_t c = buf[y * w + x];
                    unsigned char px[3] = { (unsigned char)(c & 0xFF), (unsigned char)((c >> 8) & 0xFF), (unsigned char)((c >> 16) & 0xFF) };
                    fwrite(px, 1, 3, f);
                }
                fclose(f);
            }
            puts("ok"); break; }
        case 'q': core->deinit(core); return 0;
        default: puts("?"); break;
        }
    }
    core->deinit(core);
    return 0;
}
