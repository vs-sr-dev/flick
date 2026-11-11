/* Flick - USB stick for homebrew (see usb.h) */
#include "usb.h"
#include "bus.h"
#include "score7.h"
#include "os.h"

/* int DrvUSBH_Initial(void), int DrvUSBH_LUNInitial(int lun),
 * int DrvUSBH_ReadSector(uint32_t lba, uint32_t count, uint8_t *buf, int unknown) and
 * DrvUSBH_WriteSector, same arguments. All return 0 when they succeed. S+core calling
 * convention: arguments in r4-r7, result in r4, return address in r3. */
#define DRV_INITIAL      0xa001e5d4u
#define DRV_LUN_INITIAL  0xa001e9e4u
#define DRV_READ_SECTOR  0xa001eee8u
#define DRV_WRITE_SECTOR 0xa001efccu

#define SECTOR 512

int g_usb_hle;

static FILE *s_img;
static uint64_t s_sectors;
static int s_readonly;

int usb_attach(const char *path)
{
	int readonly = 0;
	FILE *f = os_fopen(path, "r+b");
	if (!f)                                      /* a read-only file: a write-protected stick */
	{
		f = os_fopen(path, "rb");
		readonly = 1;
	}
	if (!f) return 0;
	os_fseek(f, 0, SEEK_END);
	const long long size = os_ftell(f);
	if (size < SECTOR)
	{
		fclose(f);
		return 0;
	}
	usb_detach();
	s_img = f;
	s_sectors = (uint64_t)size / SECTOR;
	s_readonly = readonly;
	g_usb_hle = 1;
	hs_log("usb: %s, %llu sectors%s\n", path, (unsigned long long)s_sectors, readonly ? ", read-only" : "");
	return 1;
}

void usb_detach(void)
{
	if (s_img) fclose(s_img);
	s_img = NULL;
	g_usb_hle = 0;
}

/* count sectors between the image and guest memory; 0: done, as the firmware returns.
 * The transfer takes no emulated time (the real one: about 1 ms per sector). */
static uint32_t transfer(uint32_t lba, uint32_t count, uint32_t addr, int write)
{
	uint8_t buf[SECTOR];
	if ((uint64_t)lba + count > s_sectors || (write && s_readonly))
	{
		hs_log("usb: %s of %u sectors at %u refused\n", write ? "write" : "read", count, lba);
		return 1;
	}
	if (os_fseek(s_img, (long long)lba * SECTOR, SEEK_SET) != 0) return 1;
	for (uint32_t s = 0; s < count; s++, addr += SECTOR)
	{
		if (write)
		{
			for (int i = 0; i < SECTOR; i++) buf[i] = bus_read8(addr + i);
			if (fwrite(buf, 1, SECTOR, s_img) != SECTOR) return 1;
		}
		else
		{
			if (fread(buf, 1, SECTOR, s_img) != SECTOR) return 1;
			for (int i = 0; i < SECTOR; i++)
				if (bus_writable(addr + i)) *bus_ptr(addr + i) = buf[i];
		}
	}
	if (write) fflush(s_img);                    /* the image is whole if Flick is closed now */
	else g_cpu.stores++;                         /* memory changed: not an idle loop */
	return 0;
}

int usb_hle(void)
{
	uint32_t *r = g_cpu.gpr;
	uint32_t result;
	switch (g_cpu.pc | 0x20000000u)              /* also through the cached mirror (0x80...) */
	{
	case DRV_INITIAL: result = 0; break;                 /* a device, powered and enumerated */
	case DRV_LUN_INITIAL: result = r[4] ? 1 : 0; break;  /* one logical unit */
	case DRV_READ_SECTOR: result = transfer(r[4], r[5], r[6], 0); break;
	case DRV_WRITE_SECTOR: result = transfer(r[4], r[5], r[6], 1); break;
	default: return 0;
	}
	r[4] = result;
	g_cpu.pc = r[3];
	return 1;
}
