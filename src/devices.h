/* HyperScanEmu - SPG290 peripherals
 * Timer, I2C, PPU and screen are ports of the MAME devices by Sandro Ronco
 * (BSD-3-Clause): spg290_timer.cpp, spg290_i2c.cpp, spg290_ppu.cpp, hyperscan_ctrl.cpp,
 * plus the timing parts of MAME's screen_device. */
#ifndef HS_DEVICES_H
#define HS_DEVICES_H

#include <stdio.h>
#include "emu.h"

#define COMBINE(var, data, mask)  ((var) = ((var) & ~(mask)) | ((data) & (mask)))

/* interrupt vectors */
#define IRQ_I2C      39
#define IRQ_PPU_VBL  53
#define IRQ_TIMER    56

/* timer.c - six SPG290 timers, P_TIMER_CLK_SEL */
void timers_init(void);
uint32_t timer_read(int n, uint32_t off);
void timer_write(int n, uint32_t off, uint32_t data, uint32_t mask);
void timer_control_w(int n, uint32_t data);
uint32_t timers_clk_sel_r(void);
void timers_clk_sel_w(uint32_t data, uint32_t mask);

/* i2c.c - I2C master with the two HyperScan controllers on ports 0 and 1 */
void i2c_init(void);
uint32_t i2c_read(uint32_t off);
void i2c_write(uint32_t off, uint32_t data, uint32_t mask);
extern uint8_t g_pad[2][4];      /* IN.0, IN.1 (buttons), IN.2 (Y), IN.3 (X) */

/* screen.c - raster timing (MAME screen_device), TV encoder registers */
void screen_init(void);
void screen_configure(int width, int height, int vis_w, int vis_h, atto_t frame_period);
int screen_vpos(void);
int screen_width(void), screen_height(void);
uint32_t tv_read(uint32_t off);
void tv_write(uint32_t off, uint32_t data, uint32_t mask);
void tv_buffer_ctrl_w(uint32_t data);
uint32_t tv_start_addr_r(int n);
void tv_start_addr_w(int n, uint32_t data, uint32_t mask);
extern uint32_t g_frame[480][640];   /* last completed frame, 0x00RRGGBB */
extern uint64_t g_frame_count;
extern int g_i2c_sysclk;             /* I2C from the 108 MHz system clock (MAME: 27 MHz) */
extern int g_field_vblank;           /* timing experiment: PPU vblank per field (interlaced) */
extern void (*g_on_frame)(void);     /* frontend hook, called at each vblank start */
void screen_compose(void);           /* render the PPU and the TV encoder into g_frame */
void screen_dump(FILE *f);           /* debug dumps: the state screen_compose() needs */
int screen_undump(FILE *f);

/* ppu.c */
void ppu_init(void);
uint32_t ppu_read(uint32_t off);
void ppu_write(uint32_t off, uint32_t data, uint32_t mask);
void ppu_screen_vblank(int state);
void ppu_render(uint32_t (*bitmap)[640], int vis_w, int vis_h);
void ppu_dump(FILE *f);              /* debug dumps: registers and memories */
int ppu_undump(FILE *f);

#endif
