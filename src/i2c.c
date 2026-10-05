// license:BSD-3-Clause
// copyright-holders:Sandro Ronco
/* SPG290 I2C master - port of MAME's spg290_i2c.cpp, with the HyperScan controllers
 * (MAME's hyperscan_ctrl.cpp HLE) on ports 0 and 1 as wired by the spg29x driver.
 *
 * MAME clocks the block from the 27 MHz crystal. The BIOS sets the divider to 600, so a
 * read of one controller register takes 3.4 ms, and the 10 it makes per frame (two pads,
 * 5 registers each) take 33.86 ms: all of a frame. Its frame loop then never waits for
 * the vblank, which is where it feeds its boot jingle, and the jingle stalls after its
 * first buffer. Clocked from the 108 MHz system clock (g_i2c_sysclk, fast preset) the
 * reads take a quarter of the frame and the jingle plays through. */
#include "devices.h"
#include "sched.h"
#include "score7.h"

static uint32_t s_config, s_irq_control, s_clock_conf, s_id, s_port_addr, s_wdata, s_rdata;
static emu_timer *s_timer;
int g_i2c_sysclk;

/* IN.0/IN.1 buttons (active high), IN.2/IN.3 analog sticks centred at 0x7f */
uint8_t g_pad[2][4] = { { 0, 0, 0x7f, 0x7f }, { 0, 0, 0x7f, 0x7f } };
static uint16_t s_pad_latch[2][4];

static uint16_t ctrl_read(int port, uint32_t offset)
{
	int addr = offset & 0x0f;
	if (addr < 4)
	{
		s_pad_latch[port][addr] = g_pad[port][addr];
		return s_pad_latch[port][addr];
	}
	// this is used to validate the inputs
	return ((s_pad_latch[port][0] + s_pad_latch[port][1] + s_pad_latch[port][2] + s_pad_latch[port][3]) << 2) | ((offset >> 4) & 0x0f);
}

static uint16_t i2c_port_read(uint32_t offset)
{
	int port = (offset >> 4) & 0x0f;
	if (port < 2)
		return ctrl_read(port, offset);
	return 0xffff;
}

static void i2c_update(void *ctx, int param)
{
	(void)ctx; (void)param;
	if (s_config & 0x40)
		s_rdata = i2c_port_read(s_port_addr);
	/* writes go nowhere: the controllers ignore them in MAME */

	// I2C ack
	s_config |= (s_config & 7) << 3;

	// I2C IRQ
	if (s_irq_control & 0x02)
	{
		s_irq_control |= 0x01;
		score7_set_input(IRQ_I2C);
	}
}

void i2c_init(void)
{
	s_config = s_irq_control = s_clock_conf = s_id = s_port_addr = s_wdata = s_rdata = 0;
	s_timer = timer_alloc(i2c_update, NULL, "i2c");
}

uint32_t i2c_read(uint32_t off)
{
	switch (off)
	{
	case 0x20: return s_config;
	case 0x24: return s_irq_control;
	case 0x28: return s_clock_conf;
	case 0x2c: return s_id;
	case 0x30: return s_port_addr;
	case 0x34: return s_wdata;
	case 0x38: return s_rdata;
	}
	hs_log("i2c: unknown read %x\n", off);
	return 0;
}

void i2c_write(uint32_t off, uint32_t data, uint32_t mask)
{
	switch (off)
	{
	case 0x20:
	{
		COMBINE(s_config, data, mask);
		/* attotime::from_hz(clock() / 4 / ((m_clock_conf & 0x3ff) + 1)), integer clock */
		const uint32_t base = g_i2c_sysclk ? CPU_CLOCK : XTAL_27M;
		const atto_t clk = hz_to_atto(base / 4 / ((s_clock_conf & 0x3ff) + 1));
		if (s_config & 0x0001)       // Start 8
			timer_adjust(s_timer, clk * (2 + 9 * 4), 0, 0);
		else if (s_config & 0x0002)  // Start 16
			timer_adjust(s_timer, clk * (2 + 9 * 5), 0, 0);
		else if (s_config & 0x0004)  // Start N
			timer_adjust(s_timer, clk * (2 + 9 * 4), 0, clk * (2 + 9 * 4));
		else if (s_config & 0x0100)  // Stop N
			timer_disable(s_timer);
		break;
	}
	case 0x24:
		COMBINE(s_irq_control, data, mask);
		if (mask & 0xff)
			s_irq_control &= ~(data & 0x01);  // IRQ ack
		break;
	case 0x28: COMBINE(s_clock_conf, data, mask); break;
	case 0x2c: COMBINE(s_id, data, mask); break;
	case 0x30: COMBINE(s_port_addr, data, mask); break;
	case 0x34: COMBINE(s_wdata, data, mask); break;
	case 0x38: COMBINE(s_rdata, data, mask); break;
	default: hs_log("i2c: unknown write %x = %x\n", off, data);
	}
}
