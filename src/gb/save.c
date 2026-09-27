#include <gb/gb.h>
#include <string.h>
#include "save.h"

#define SAVE_VERSION 1
/* Two copies of the save: the primary (where v1 has always lived, so existing
 * saves still load) and a backup written right after it. A write torn by a
 * power-off or a loose cartridge can only damage one copy, so the other one
 * survives instead of every record being wiped. */
#define SRAM_PRIMARY ((uint8_t *)0xA000)
#define SRAM_BACKUP  ((uint8_t *)0xA020)

/* each copy must fit in its 32-byte slot */
typedef char save_fits_slot[(sizeof(SaveData) <= 0x20) ? 1 : -1];

SaveData save;

uint8_t save_checksum(const SaveData *s)
{
    const uint8_t *p = (const uint8_t *)s;
    uint8_t i, c = 0x5A;
    for (i = 0; i < (uint8_t)(sizeof(SaveData) - 1); i++) c = (uint8_t)((c << 1 | c >> 7) ^ p[i]);
    return c;
}

static uint8_t save_valid(void)
{
    return (uint8_t)(save.magic[0] == 'I' && save.magic[1] == 'B' && save.version == SAVE_VERSION &&
                     save.checksum == save_checksum(&save));
}

static void sram_read(const uint8_t *src)
{
    ENABLE_RAM;
    SWITCH_RAM(0);
    memcpy(&save, src, sizeof(SaveData));
    DISABLE_RAM;
}

void save_load(void)
{
    sram_read(SRAM_PRIMARY);
    if (save_valid()) return;
    sram_read(SRAM_BACKUP);
    if (save_valid()) {
        save_write();       /* repair the primary copy */
        return;
    }
    memset(&save, 0, sizeof(SaveData));
    save.magic[0] = 'I';
    save.magic[1] = 'B';
    save.version = SAVE_VERSION;
    save.zen_sector = 1;
    save.zen_seed = 0;   /* chosen on first zen start */
    save_write();
}

void save_write(void)
{
    save.checksum = save_checksum(&save);
    ENABLE_RAM;
    SWITCH_RAM(0);
    memcpy(SRAM_PRIMARY, &save, sizeof(SaveData));
    memcpy(SRAM_BACKUP, &save, sizeof(SaveData));
    DISABLE_RAM;
}
