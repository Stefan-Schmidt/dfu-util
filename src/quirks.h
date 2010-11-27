#ifndef DFU_QUIRKS_H
#define DFU_QUIRKS_H

#define VENDOR_OPENMOKO 0x1d50 /* Openmoko Freerunner / GTA02 */
#define VENDOR_FIC      0x1457 /* Openmoko Freerunner / GTA02 */
#define VENDOR_VOTI	0x16c0 /* OpenPCD Reader */

#define QUIRK_POLLTIMEOUT  (1<<0)

/* Fallback value, works for OpenMoko */
#define DEFAULT_POLLTIMEOUT  5

extern int quirks;

void set_quirks(unsigned long vendor, unsigned long product);

#endif /* DFU_QUIRKS_H */
