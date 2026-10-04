// license:BSD-3-Clause
// copyright-holders:Sandro Ronco
/* HyperScan RFID card - port of MAME's hyperscan_card.cpp (with the HS-LBA fork's 16-bit
 * response index). The BIOS bit-bangs the reader through GPIO bit 1; pulse widths are
 * classified in 13.56 MHz carrier clocks: < 500 = "0", 500-999 = "1", 1000-4999 = strobe
 * (advances the response). Card writes (game saves) go to the card's save file, if any. */
#include <limits.h>
#include <string.h>
#include "card.h"
#include "sched.h"

static int s_loaded;
static uint8_t m_memory[CARD_SIZE];
static char s_save_path[1024];          /* where card writes go ("" = memory only) */

static void save_card(void)
{
	if (!s_save_path[0]) return;
	FILE *f = fopen(s_save_path, "wb");
	if (!f) { hs_log("card: cannot write %s\n", s_save_path); return; }
	fwrite(m_memory, 1, CARD_SIZE, f);
	fclose(f);
}

void card_insert(const uint8_t *image, const char *save_path)
{
	memcpy(m_memory, image, CARD_SIZE);
	snprintf(s_save_path, sizeof(s_save_path), "%s", save_path ? save_path : "");
	s_loaded = 1;
}

void card_remove(void)
{
	s_loaded = 0;
	s_save_path[0] = 0;
}
static int m_state, m_bit_pos;
static uint8_t m_data;
static uint8_t m_cmd_buf[16];
static int m_cmd_len;
static uint8_t m_resp_buf[2048];       /* one bit per entry */
static int m_resp_len;
static uint16_t m_resp_idx;
static atto_t m_last_at;

int card_load(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	size_t n = fread(m_memory, 1, CARD_SIZE, f);
	fclose(f);
	if (n != CARD_SIZE) return 0;
	s_loaded = 1;
	return 1;
}

int card_loaded(void) { return s_loaded; }

void card_reset(void)
{
	m_state = m_bit_pos = 0;
	m_data = 0;
	m_cmd_len = m_resp_len = 0;
	m_resp_idx = 0;
	m_last_at = 0;
}

static uint16_t calc_crc(const uint8_t *data, int len)
{
	uint16_t crc = 0xffffU;
	for (int i = 0; i < len; i++)
	{
		uint8_t b = data[i];
		b ^= crc & 0xff;
		b ^= b << 4;
		crc = (crc >> 8) ^ ((uint16_t)b << 8) ^ ((uint16_t)b << 3) ^ ((uint16_t)b >> 4);
	}
	return ~crc;
}

static void resp_add_byte(uint8_t data, int add_parity)
{
	int parity = 1;
	for (int i = 0; i < 8; i++)
	{
		int bit = (data >> i) & 1;
		m_resp_buf[m_resp_len++] = bit;
		parity ^= bit;
	}
	if (add_parity)
		m_resp_buf[m_resp_len++] = parity;
}

static void make_resp(const uint8_t *data, int len, int add_crc)
{
	m_resp_idx = 0;
	m_resp_len = 0;
	resp_add_byte(0x00, 0);
	resp_add_byte(0x80, 0);
	for (int i = 0; i < len; i++)
		resp_add_byte(data[i], 1);
	if (add_crc)
	{
		uint16_t crc = calc_crc(data, len);
		resp_add_byte(crc, 1);
		resp_add_byte(crc >> 8, 1);
	}
}

static void check_command(void)
{
	uint8_t buf[CARD_SIZE + 2];
	int n = 0;
	uint8_t *cmd = m_cmd_buf;

	if (m_cmd_len == 1 && (cmd[0] == 0x26 || cmd[0] == 0x52))     // REQA, WUPA
	{
		buf[n++] = 0x00; buf[n++] = 0x00;                             // ATQA
		m_cmd_len = 0;
		make_resp(buf, n, 0);
	}
	else if (m_cmd_len == 9 && cmd[0] == 0x78)                        // RID
	{
		buf[n++] = 0x11; buf[n++] = 0x00;                             // HR0, HR1
		for (int i = 0; i < 4; i++) buf[n++] = m_memory[i];           // UID
		m_cmd_len = 0;
		make_resp(buf, n, 1);
	}
	else if (m_cmd_len == 9 && cmd[0] == 0x01)                        // READ
	{
		if (cmd[1] >= CARD_SIZE) cmd[1] %= CARD_SIZE;
		buf[n++] = cmd[1];
		buf[n++] = m_memory[cmd[1]];
		m_cmd_len = 0;
		make_resp(buf, n, 1);
	}
	else if (m_cmd_len == 9 && cmd[0] == 0x00)                        // RALL
	{
		buf[n++] = 0x11; buf[n++] = 0x00;
		for (int i = 0; i < CARD_SIZE; i++) buf[n++] = m_memory[i];
		m_cmd_len = 0;
		make_resp(buf, n, 1);
	}
	else if (m_cmd_len == 9 && (cmd[0] == 0x53 || cmd[0] == 0x1a))   // WRITE-E, WRITE-NE
	{
		if (cmd[1] >= CARD_SIZE) cmd[1] %= CARD_SIZE;
		const uint8_t old = m_memory[cmd[1]];
		if (cmd[0] == 0x53) m_memory[cmd[1]] = cmd[2];
		else m_memory[cmd[1]] |= cmd[2];
		if (m_memory[cmd[1]] != old)
			save_card();
		buf[n++] = cmd[1];
		buf[n++] = cmd[2];
		m_cmd_len = 0;
		make_resp(buf, n, 1);
	}
}

void card_write(int state)
{
	if (m_state == state || !s_loaded)
		return;
	m_state = state;

	/* int clk = 13560000 / (machine().time() - m_last_at).as_hz(); */
	const atto_t now = sched_time();
	const atto_t dt = now - m_last_at;
	double hz;
	if (dt < ATTO_PER_SEC)
		hz = 1e18 / (double)(uint64_t)dt;
	else
		hz = 1.0 / ((double)(uint64_t)(dt / ATTO_PER_SEC) + (double)(uint64_t)(dt % ATTO_PER_SEC) * 1e-18);
	const double c = 13560000 / hz;
	const int clk = c >= (double)INT_MAX ? INT_MAX : (int)c;
	m_last_at = now;

	if (state)
		return;

	if (clk < 1000)
	{
		if (clk < 500)
			m_data &= ~(1 << m_bit_pos);
		else
			m_data |= 1 << m_bit_pos;

		m_bit_pos++;

		if ((m_cmd_len == 0 && m_bit_pos == 7) || m_bit_pos == 8)
		{
			if (m_cmd_len == 0)
				m_data &= 0x7f;
			if (m_cmd_len < (int)sizeof(m_cmd_buf))
				m_cmd_buf[m_cmd_len++] = m_data;
			m_data = 0;
			m_bit_pos = 0;
			check_command();
		}
	}
	else if (clk < 5000)
	{
		m_bit_pos = 0;
		m_data = 0;
		if (m_resp_idx <= m_resp_len * 2)
			m_resp_idx++;
	}
}

int card_read(void)
{
	if (s_loaded && (m_resp_idx >> 1) < m_resp_len)
		return m_resp_buf[m_resp_idx >> 1] ^ (m_resp_idx & 1);
	return 0;
}
