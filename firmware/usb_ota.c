/* firmware/usb_ota.c - Monohull's USB: Dinghy's platform (zvenson's, after Felucca, SLOOP and OberVave,
 * GPL-3.0-only): the USB-MIDI device (plat/usb.c), the M-UPGRADE update path (plat/ota.c) and the RAM flash
 * driver it stages the next firmware's loader with (hal/fm1_flash.h), wired as Dinghy's platform.c wires them.
 * So the web installer, the hub and tools/fm1_install.py find the FM-1 under Monohull (it answers the identity
 * handshake with FELUCCA_ID, FM-1_928, from tools/build_fw.py) and install over it, and a computer can play it.
 *
 * Built on its own at -Os: Dinghy's API.md says the USB-MIDI IN babbled on the device when usb.c was built
 * at -O2. All SIE traffic is in plat_usb_poll (TIMER5, 2 kHz); MIDI in goes through a ring the audio ISR
 * drains (plat_midi_get); the update runs from the main loop (plat_service). The flash driver may erase and
 * write only the update's staging area (FL_RANGE_OK below): Monohull keeps nothing else in the flash.
 * SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_time.h"
#include "fm1_sys.h"
#include "usb_ota.h"

void *memcpy(void *d, const void *s, unsigned long n);

#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0                       /* MIDI only: Monohull's output is 24-bit and its own */
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_928"
#endif
#include "plat/usb.c"

/* ---- flash: read anywhere; erase / program only the update's staging area */
#define FL_RANGE_OK(off, n) FL_IN(off, n, FL_OTA_LO, FL_OTA_HI)
#include "hal/fm1_flash.h"
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}

#include "plat/ota.c"
static uint32_t ota_now_ms(void) { return plat_ms(); }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code) { plat_update_screen(step, code); }
static void ota_commit(const uint8_t *parm)
{
    plat_reset_prep();
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);                 /* the record into RAM, a core reset: the loader installs */
}

void plat_start(void)
{
    flash_ok = FL_JEDEC_OK(FL_FAR(fl_jedec_ram)());
    if (flash_ok)
        ota_boot_cleanup();                 /* a staging area left by an update that finished or never started */
    usb_start();
}

void plat_usb_poll(void) { usb_poll(); }

int plat_midi_get(uint32_t *pkt)
{
    if (mi_r == mi_w)
        return 0;
    *pkt = midi_in_q[mi_r % MQ];
    RING_PUBLISH();
    mi_r++;
    return 1;
}

int plat_service(void)
{
    usb_retry(plat_ms());
    ota_service();                          /* the identity handshake */
    if (usb.ota_req) {                      /* the installer's upgrade command */
        usb.ota_req = 0;
        if (flash_ok) {
            plat_update_begin();
            ota_session();                  /* returns only if nothing was committed */
            plat_update_end();
        }
    }
    if (usb.uboot_req) {                    /* SysEx F0 22 24 35 7D F7: the stock update mode */
        usb.uboot_req = 0;
        return 1;
    }
    return 0;
}

void plat_detach(void)
{
    usb_detach();
    fm1_delay_ms(30);
}
