/* firmware/usb_ota.h - Monohull's USB: class-compliant USB-MIDI and the update path the installers use
 * (the identity handshake, M-UPGRADE, the UBOOT soft key). usb_ota.c, built on its own at -Os.
 * SPDX-License-Identifier: GPL-3.0-only */
#ifndef MH_USB_OTA_H
#define MH_USB_OTA_H

#include <stdint.h>

void plat_start(void);                  /* boot, before the audio: the flash, a leftover staging area, USB */
void plat_usb_poll(void);               /* TIMER5 ISR, every 0.5 ms */
int plat_midi_get(uint32_t *pkt);       /* audio ISR: the next USB-MIDI event packet, 0 = none */
int plat_service(void);                 /* main loop: 1 = the host asked for UBOOT */
void plat_detach(void);                 /* before a reset or UBOOT, so the host sees the device go */

/* main.c supplies */
uint32_t plat_ms(void);
void plat_update_screen(uint32_t step, int32_t code);   /* step 1..5, or 9 with a code: failed */
void plat_update_begin(void);           /* an update starts: silence, the screen is the update's */
void plat_update_end(void);             /* it failed or was refused: back to playing */
void plat_reset_prep(void);             /* an intended reset: not a crash for the boot guard */

#endif
