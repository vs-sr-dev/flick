/* Flick - USB stick for homebrew, served at the firmware's USB host driver.
 *
 * The console's USB port goes unused by the retail games, but ppcasm's HyperScan SDK reads
 * FAT32 sticks through it (DOOM keeps its WAD there). The SDK does not drive the USB host
 * controller itself: its FatFS glue calls four functions of the firmware in RAM, at fixed
 * addresses. Flick does not emulate the controller (0x081c0000 stays unmapped, as in MAME):
 * with an image attached, those four entry points are answered here from the image file.
 * Without one they run as they are and find no device, like the console with an empty port.
 *
 * The addresses belong to the known BIOS dump (BIOS_CRC32): do not attach with another. */
#ifndef HS_USB_H
#define HS_USB_H

#include "emu.h"

/* the firmware's entry points (SDK: hslibs/FatFS/diskio.c), lowest and highest */
#define USB_HLE_FIRST 0xa001e5d4u
#define USB_HLE_LAST  0xa001efccu

extern int g_usb_hle;                 /* an image is attached: score7_step calls usb_hle */

/* a raw disk image (a stick's dump: MBR + FAT32, or a bare FAT volume), written in place
 * when the program writes; 0: cannot open. Replaces the one attached. */
int usb_attach(const char *path);
void usb_detach(void);
/* the CPU is at one of the entry points: do the call and return to the caller.
 * 0: pc is not one of them, nothing done */
int usb_hle(void);

#endif
