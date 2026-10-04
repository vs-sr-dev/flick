// license:BSD-3-Clause
// copyright-holders:Sandro Ronco
/******************************************************************************

    Sunplus Technology S+core 7 interpreter

    C port of MAME's src/devices/cpu/score/score.cpp (Sandro Ronco, BSD-3-Clause),
    including the rotate instructions added in the HS-LBA fork. Behaviour is kept
    identical to MAME, quirks included, because MAME's trace is the M1 oracle:
    flat cycles per instruction, flag quirks of addc/subc, BIT(x, 32) and BIT(x, -1)
    as x86 evaluates them, m_ppc left on the interrupted PC while the handler's first
    instruction runs. Deliberate fixes come later, each one against a test.

******************************************************************************/

#include <string.h>
#include "score7.h"
#include "bus.h"
#include "trace.h"
#include "sched.h"

score7_t g_cpu;
uint32_t g_mark_pc = 1;      /* odd: never matches */
uint64_t g_mark_count;

#define m_pc    g_cpu.pc
#define m_ppc   g_cpu.ppc
#define m_op    g_cpu.op
#define m_gpr   g_cpu.gpr
#define m_cr    g_cpu.cr
#define m_sr    g_cpu.sr
#define m_ce    g_cpu.ce

/* field extraction (scorem.h) */
#define GET_S_FUNC6(op)     ((op >>  1) & 0x3f)
#define GET_S_RB(op)        ((op >> 10) & 0x1f)
#define GET_S_RA(op)        ((op >> 15) & 0x1f)
#define GET_S_RD(op)        ((op >> 20) & 0x1f)
#define GET_S_CU(op)        (op & 0x01)
#define GET_S_LK(op)        (op & 0x01)
#define GET_I_FUNC3(op)     ((op >> 17) & 0x07)
#define GET_I_IMM16(op)     ((op >>  1) & 0xffff)
#define GET_I_RD(op)        ((op >> 20) & 0x1f)
#define GET_I_CU(op)        (op & 0x01)
#define GET_RI_IMM14(op)    ((op >>  1) & 0x3fff)
#define GET_RI_RA(op)       ((op >> 15) & 0x1f)
#define GET_RI_RD(op)       ((op >> 20) & 0x1f)
#define GET_RI_CU(op)       (op & 0x01)
#define GET_J_DISP24(op)    ((op >> 1) & 0x00ffffff)
#define GET_J_DISP11(op)    ((op >> 1) & 0x000007ff)
#define GET_J_LK(op)        (op & 0x01)
#define GET_BC_DISP19(op)   (((op >> 6) & 0x07fe00) | ((op >>  1) & 0x0001ff))
#define GET_BC_BC(op)       ((op >> 10) & 0x1f)
#define GET_BC_LK(op)       (op & 0x01)
#define GET_RIX_FUNC3(op)   (op & 0x07)
#define GET_RIX_IMM12(op)   ((op >> 3) & 0x0fff)
#define GET_RIX_RA(op)      ((op >> 15) & 0x1f)
#define GET_RIX_RD(op)      ((op >> 20) & 0x1f)
#define GET_R_FUNC4(op)     (op & 0x0f)
#define GET_R_RA(op)        ((op >> 4) & 0x0f)
#define GET_R_RD(op)        ((op >> 8) & 0x0f)
#define GET_BX_DISP8(op)    (op & 0xff)
#define GET_BX_EC(op)       ((op >> 8) & 0x0f)
#define GET_LS_RD(op)       ((op >> 20) & 0x1f)
#define GET_LS_RA(op)       ((op >> 15) & 0x1f)
#define GET_LS_IMM15(op)    (op & 0x7fff)
#define GET_I2_RD(op)       ((op >> 8) & 0x0f)
#define GET_I2_IMM8(op)     (op & 0xff)
#define GET_I16_FUNC3(op)   (op & 0x07)
#define GET_I16_RD(op)      ((op >> 8) & 0x0f)
#define GET_I16_IMM5(op)    ((op >> 3) & 0x1f)
#define GET_CR_OP(op)       (op & 0xff)
#define GET_CR_CR(op)       ((op >> 15) & 0x1f)
#define GET_CR_RD(op)       ((op >> 20) & 0x1f)
#define GET_P_RAG(op)       ((op >> 4) & 0x07)
#define GET_P_RDG(op)       (((op >> 8) & 0x0f) | ((op >> 3) & 0x10))

/* flags */
#define FLAG_V  0x01
#define FLAG_C  0x02
#define FLAG_Z  0x04
#define FLAG_N  0x08
#define FLAG_T  0x10

#define REG_BP      m_gpr[2]
#define REG_LNK     m_gpr[3]
#define REG_PSR     m_cr[0]
#define REG_CR      m_cr[1]
#define REG_ECR     m_cr[2]
#define REG_EXCPVEC m_cr[3]
#define REG_EPC     m_cr[5]
#define REG_EMA     m_cr[6]
#define REG_CNT     m_sr[0]
#define REG_CEH     m_ce[0]
#define REG_CEL     m_ce[1]

#define GET_V   ((REG_CR & FLAG_V)>>0)
#define GET_C   ((REG_CR & FLAG_C)>>1)
#define GET_Z   ((REG_CR & FLAG_Z)>>2)
#define GET_N   ((REG_CR & FLAG_N)>>3)
#define GET_T   ((REG_CR & FLAG_T)>>4)

#define SET_F(m, f)  do { if (f) REG_CR |= (m); else REG_CR &= ~(uint32_t)(m); } while (0)
#define SET_V(f)     SET_F(FLAG_V, f)
#define SET_C(f)     SET_F(FLAG_C, f)
#define SET_Z(f)     SET_F(FLAG_Z, f)
#define SET_N(f)     SET_F(FLAG_N, f)
#define SET_T(f)     SET_F(FLAG_T, f)

#define CHECK_Z(r)          SET_Z((r)==0)
#define CHECK_N(r)          SET_N((int32_t)(r) < 0)
#define CHECK_C_ADD(a,b)    SET_C((a) > (0xffffffffu - (b)))
#define CHECK_C_SUB(a,b)    SET_C((a) >= (b))
#define CHECK_V_ADD(a,b,d)  SET_V((int32_t)(((a) ^ (d)) & ((b) ^ (d))) < 0)
#define CHECK_V_SUB(a,b,d)  SET_V((int32_t)(((a) ^ (b)) & ((a) ^ (d))) < 0)

/* MAME's BIT(x, n) is (x >> n) & 1; for n = 32 or -1 (shift counts of 0) x86 masks the
 * count, which is what MAME's compiled code does */
#define BIT(x, n)   (((uint32_t)(x) >> ((n) & 31)) & 1)
#define BIT64(x, n) (((uint64_t)(x) >> (n)) & 1)

static inline int32_t sext(uint32_t v, int width)
{
	return (int32_t)(v << (32 - width)) >> (32 - width);
}

/* exception causes */
#define EXCEPTION_NMI                1
#define EXCEPTION_ADEL_INSTRUCTION   2
#define EXCEPTION_BUSEL_INSTRUCTION  5
#define EXCEPTION_P_EL               6
#define EXCEPTION_SYSCALL            7
#define EXCEPTION_CCU                8
#define EXCEPTION_RI                 9
#define EXCEPTION_TRAP              10
#define EXCEPTION_ADEL_DATA         11
#define EXCEPTION_ADES_DATA         12
#define EXCEPTION_CEE               16
#define EXCEPTION_CPE               17
#define EXCEPTION_BUSEL_DATA        18
#define EXCEPTION_SWI               19
#define EXCEPTION_INTERRUPT         20


/* ------------------------------------------------------------------------ */
/*  memory                                                                   */
/* ------------------------------------------------------------------------ */

static inline uint8_t read_byte(uint32_t a)        { return bus_read8(a); }
static inline uint16_t read_word(uint32_t a)       { return bus_read16(a & ~1u); }
static inline uint32_t read_dword(uint32_t a)      { return bus_read32(a & ~3u); }
static inline void write_byte(uint32_t a, uint8_t d)   { g_cpu.stores++; bus_write8(a, d); }
static inline void write_word(uint32_t a, uint16_t d)  { g_cpu.stores++; bus_write16(a & ~1u, d); }
static inline void write_dword(uint32_t a, uint32_t d) { g_cpu.stores++; bus_write32(a & ~3u, d); }


/* ------------------------------------------------------------------------ */
/*  helpers                                                                  */
/* ------------------------------------------------------------------------ */

static int check_condition(uint8_t bc)
{
	switch (bc & 0x0f)
	{
		case  0:    return GET_C;                      // carry set
		case  1:    return !GET_C;                     // carry clear
		case  2:    return GET_C && !GET_Z;            // C & ~Z
		case  3:    return !GET_C || GET_Z;            // ~C | Z
		case  4:    return GET_Z;                      // Z
		case  5:    return !GET_Z;                     // ~Z
		case  6:    return !GET_Z && (GET_N == GET_V); // (Z = 0) & (N = V)
		case  7:    return GET_Z || (GET_N != GET_V);  // (Z = 1) | (N != V)
		case  8:    return (GET_N == GET_V);           // N = V
		case  9:    return (GET_N != GET_V);           // N != V
		case 10:    return GET_N;                      // N
		case 11:    return !GET_N;                     // ~N
		case 12:    return GET_V;                      // overflow V
		case 13:    return !GET_V;                     // no overflow ~V
		case 14:    return REG_CNT > 0;                // CNT>0
		case 15:    return 1;                          // always
	}
	return 0;
}

static int check_condition_branch(uint8_t bc)
{
	if ((bc & 0x0f) == 14)          // CNT>0, CNT--
	{
		if (REG_CNT > 0)
		{
			REG_CNT--;
			return 1;
		}
		return 0;
	}
	return check_condition(bc);
}

static void gen_exception(int cause, uint32_t param)
{
	if (g_trace)
	{
		if (cause == EXCEPTION_INTERRUPT) trace_printf("I %u c=%llu\n", param, (unsigned long long)g_cpu.cycles);
		else trace_printf("X %d %x\n", cause, param);
	}

	REG_ECR = (REG_ECR & ~0x0000001fu) | (cause & 0x1f);              // set exception cause
	REG_PSR = (REG_PSR & ~0x0000000fu) | ((REG_PSR << 2) & 0x0c);     // push status bits
	REG_CR  = (REG_CR  & ~0x000003ffu) | ((REG_CR << 5) & 0x3e0);     // push flag bits
	REG_EPC = m_ppc & 0xfffffffe;                                     // set return address

	switch (cause)
	{
		case EXCEPTION_P_EL:
			REG_EMA = REG_EPC;
			/* fall through */
		case EXCEPTION_NMI:
		case EXCEPTION_CEE:
		case EXCEPTION_SYSCALL:
		case EXCEPTION_TRAP:
		case EXCEPTION_RI:
			m_pc = (REG_EXCPVEC & 0xffff0000) + 0x200;
			break;

		case EXCEPTION_SWI:
		case EXCEPTION_INTERRUPT:
			REG_ECR = (REG_ECR & ~0x00fc0000u) | ((param & 0x3f) << 18);     // set irq source
			m_pc = (REG_EXCPVEC & 0xffff0000) + 0x200 + (param << (REG_EXCPVEC & 1 ? 4 : 2));
			break;

		case EXCEPTION_ADEL_INSTRUCTION:
		case EXCEPTION_BUSEL_INSTRUCTION:
		case EXCEPTION_CCU:
		case EXCEPTION_ADEL_DATA:
		case EXCEPTION_ADES_DATA:
		case EXCEPTION_CPE:
		case EXCEPTION_BUSEL_DATA:
			hs_fatal("unhandled exception: %d 0x%08x (PC=0x%08x)", cause, param, m_ppc);
	}
}

static void check_irq(void)
{
	if (REG_PSR & 0x01)
	{
		for (int i = 63; i > 0; i--)
		{
			if (g_cpu.pending & (1ULL << i))
			{
				g_cpu.pending &= ~(1ULL << i);
				gen_exception(EXCEPTION_INTERRUPT, i);
				return;
			}
		}
	}
}

static void unemulated_op(const char *op)
{
	hs_fatal("unemulated %s (PC=0x%08x)", op, m_ppc);
}

static void op_undef(void)
{
	hs_log("undefined instruction 0x%x (PC=0x%08x)\n", m_op, m_ppc);
	gen_exception(EXCEPTION_RI, 0);
}


/* ------------------------------------------------------------------------ */
/*  32-bit opcodes                                                           */
/* ------------------------------------------------------------------------ */

static void op_specialform(void)
{
	uint8_t ra = GET_S_RA(m_op);
	uint8_t rb = GET_S_RB(m_op);
	uint8_t rd = GET_S_RD(m_op);
	uint8_t cu = GET_S_CU(m_op);
	uint32_t r;

	switch (GET_S_FUNC6(m_op))
	{
		case 0x00:  // nop
			break;
		case 0x01:  // syscall
			gen_exception(EXCEPTION_SYSCALL, 0);
			break;
		case 0x02:  // trap
			if (check_condition(rb))
				gen_exception(EXCEPTION_TRAP, 0);
			break;
		case 0x03:  // sdbbp
			unemulated_op("sdbbp");
			break;
		case 0x04:  // br
			if (check_condition_branch(rb))
			{
				if (GET_S_LK(m_op))
					REG_LNK = m_pc;

				m_pc = m_gpr[ra];
			}
			break;
		case 0x05: // pflush
			unemulated_op("pflush");
			break;
		case 0x08:  // add
			r = m_gpr[ra] + m_gpr[rb];
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				CHECK_V_ADD(m_gpr[ra], m_gpr[rb], r);
				CHECK_C_ADD(m_gpr[ra], m_gpr[rb]);
			}
			m_gpr[rd] = r;
			break;
		case 0x09:  // addc
			r = m_gpr[ra] + m_gpr[rb] + GET_C;
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				CHECK_V_ADD(m_gpr[ra] + GET_C, m_gpr[rb], r);
				CHECK_C_ADD(m_gpr[ra] + GET_C, m_gpr[rb]);
			}
			m_gpr[rd] = r;
			break;
		case 0x0a:  // sub
			r = m_gpr[ra] - m_gpr[rb];
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				CHECK_V_SUB(m_gpr[ra], m_gpr[rb], r);
				CHECK_C_SUB(m_gpr[ra], m_gpr[rb]);
			}
			m_gpr[rd] = r;
			break;
		case 0x0b:  // subc
			r = m_gpr[ra] - m_gpr[rb] - (GET_C ^ 1);
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				CHECK_V_SUB(m_gpr[ra] - (GET_C ^ 1), m_gpr[rb], r);
				CHECK_C_SUB(m_gpr[ra] - (GET_C ^ 1), m_gpr[rb]);
			}
			m_gpr[rd] = r;
			break;
		case 0x0c:  // cmp
			if (cu)
			{
				r = m_gpr[ra] - m_gpr[rb];
				CHECK_Z(r);
				CHECK_N(r);
				CHECK_V_SUB(m_gpr[ra], m_gpr[rb], r);
				CHECK_C_SUB(m_gpr[ra], m_gpr[rb]);
				switch (rd & 0x03)
				{
					case 0: SET_T(GET_Z); break;
					case 1: SET_T(GET_N); break;
				}
			}
			break;
		case 0x0d:  // cmpz
			if (cu)
			{
				r = m_gpr[ra] - 0;
				CHECK_Z(r);
				CHECK_N(r);
				CHECK_V_SUB(m_gpr[ra], 0u, r);
				CHECK_C_SUB(m_gpr[ra], 0u);
				switch (rd & 0x03)
				{
					case 0: SET_T(GET_Z); break;
					case 1: SET_T(GET_N); break;
				}
			}
			break;
		case 0x0f:  // neg
			r = 0 - m_gpr[rb];
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				CHECK_V_SUB(0u, m_gpr[rb], r);
				CHECK_C_SUB(0u, m_gpr[rb]);
			}
			m_gpr[rd] = r;
			break;
		case 0x10:  // and
			m_gpr[rd] = m_gpr[ra] & m_gpr[rb];
			if (cu)
			{
				CHECK_Z(m_gpr[rd]);
				CHECK_N(m_gpr[rd]);
			}
			break;
		case 0x11:  // or
			m_gpr[rd] = m_gpr[ra] | m_gpr[rb];
			if (cu)
			{
				CHECK_Z(m_gpr[rd]);
				CHECK_N(m_gpr[rd]);
			}
			break;
		case 0x12:  // not
			r = ~m_gpr[ra];
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
			}
			m_gpr[rd] = r;
			break;
		case 0x13:  // xor
			m_gpr[rd] = m_gpr[ra] ^ m_gpr[rb];
			if (cu)
			{
				CHECK_Z(m_gpr[rd]);
				CHECK_N(m_gpr[rd]);
			}
			break;
		case 0x14:  // bitclr
			m_gpr[rd] = m_gpr[ra] & ~(1u << rb);
			if (cu)
			{
				CHECK_Z(m_gpr[rd]);
				CHECK_N(m_gpr[rd]);
			}
			break;
		case 0x15:  // bitset
			m_gpr[rd] = m_gpr[ra] | (1u << rb);
			if (cu)
			{
				CHECK_Z(m_gpr[rd]);
				CHECK_N(m_gpr[rd]);
			}
			break;
		case 0x16:  // bittst
			if (cu)
			{
				r = m_gpr[ra] & (1u << rb);
				CHECK_N(m_gpr[ra]);
				CHECK_Z(r);
			}
			break;
		case 0x17:  // bittgl
			m_gpr[rd] = m_gpr[ra] ^ (1u << rb);
			if (cu)
			{
				CHECK_Z(m_gpr[rd]);
				CHECK_N(m_gpr[rd]);
			}
			break;
		case 0x18:  // sll
			r = m_gpr[ra] << (m_gpr[rb] & 0x1f);
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				SET_C(BIT(m_gpr[ra], 32 - (m_gpr[rb] & 0x1f)));
			}
			m_gpr[rd] = r;
			break;
		case 0x1a:  // srl
			r = m_gpr[ra] >> (m_gpr[rb] & 0x1f);
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				SET_C(BIT(m_gpr[ra], (m_gpr[rb] & 0x1f) - 1));
			}
			m_gpr[rd] = r;
			break;
		case 0x1b:  // sra
			r = sext(m_gpr[ra] >> (m_gpr[rb] & 0x1f), 32 - (m_gpr[rb] & 0x1f));
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				SET_C(BIT(m_gpr[ra], (m_gpr[rb] & 0x1f) - 1));
			}
			m_gpr[rd] = r;
			break;
		case 0x1c:  // ror
		{
			const uint32_t n = m_gpr[rb] & 0x1f;
			r = n ? ((m_gpr[ra] >> n) | (m_gpr[ra] << (32 - n))) : m_gpr[ra];
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				if (n) SET_C(BIT(m_gpr[ra], n - 1));
			}
			m_gpr[rd] = r;
			break;
		}
		case 0x1d:  // rorc
		{
			const uint32_t n = (m_gpr[rb] & 0x1f) % 33;
			if (n)
			{
				const uint64_t v = ((uint64_t)GET_C << 32) | m_gpr[ra];
				const uint64_t x = ((v >> n) | (v << (33 - n))) & 0x1ffffffffULL;
				r = (uint32_t)x;
				if (cu) SET_C(BIT64(x, 32));
			}
			else
				r = m_gpr[ra];
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
			}
			m_gpr[rd] = r;
			break;
		}
		case 0x1e:  // rol
		{
			const uint32_t n = m_gpr[rb] & 0x1f;
			r = n ? ((m_gpr[ra] << n) | (m_gpr[ra] >> (32 - n))) : m_gpr[ra];
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				if (n) SET_C(BIT(m_gpr[ra], 32 - n));
			}
			m_gpr[rd] = r;
			break;
		}
		case 0x1f:  // rolc
		{
			const uint32_t n = (m_gpr[rb] & 0x1f) % 33;
			if (n)
			{
				const uint64_t v = ((uint64_t)GET_C << 32) | m_gpr[ra];
				const uint64_t x = ((v << n) | (v >> (33 - n))) & 0x1ffffffffULL;
				r = (uint32_t)x;
				if (cu) SET_C(BIT64(x, 32));
			}
			else
				r = m_gpr[ra];
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
			}
			m_gpr[rd] = r;
			break;
		}
		case 0x20:  // mul
		{
			uint64_t d = (uint64_t)((int64_t)(int32_t)m_gpr[ra] * (int64_t)(int32_t)m_gpr[rb]);
			REG_CEL = d & 0xffffffff;
			REG_CEH = (d >> 32) & 0xffffffff;
			break;
		}
		case 0x21:  // mulu
		{
			uint64_t d = (uint64_t)m_gpr[ra] * (uint64_t)m_gpr[rb];
			REG_CEL = d & 0xffffffff;
			REG_CEH = (d >> 32) & 0xffffffff;
			break;
		}
		case 0x22:  // div
			if (m_gpr[rb])
			{
				int32_t a = (int32_t)m_gpr[ra];
				int32_t b = (int32_t)m_gpr[rb];
				if (a == INT32_MIN && b == -1)  // traps on x86; MAME would crash here
				{
					REG_CEL = (uint32_t)a;
					REG_CEH = 0;
				}
				else
				{
					REG_CEL = a / b;
					REG_CEH = a % b;
				}
			}
			else
			{
				gen_exception(EXCEPTION_CEE, 0);   // divide by zero exception
			}
			break;
		case 0x23:  // divu
			if (m_gpr[rb])
			{
				uint32_t a = m_gpr[ra];
				uint32_t b = m_gpr[rb];
				REG_CEL = a / b;
				REG_CEH = a % b;
			}
			else
			{
				gen_exception(EXCEPTION_CEE, 0);   // divide by zero exception
			}
			break;
		case 0x24:  // mfce
			switch (rb & 3)
			{
				case 1: m_gpr[rd] = REG_CEL;                        break;
				case 2: m_gpr[rd] = REG_CEH;                        break;
				case 3: m_gpr[rd] = REG_CEH; m_gpr[ra] = REG_CEL;   break;
			}
			break;
		case 0x25:  // mtce
			switch (rb & 3)
			{
				case 1: REG_CEL = m_gpr[rd];                        break;
				case 2: REG_CEH = m_gpr[rd];                        break;
				case 3: REG_CEH = m_gpr[rd]; REG_CEL = m_gpr[ra];   break;
			}
			break;
		case 0x28:  // mfsr
			if (rb < 3)
				m_gpr[rd] = m_sr[rb];
			else
				hs_log("mfsr accessing sr%d (PC=0x%08x)\n", rb, m_ppc);
			break;
		case 0x29:  // mtsr
			if (rb < 3)
				m_sr[rb] = m_gpr[ra];
			else
				hs_log("mtsr accessing sr%d (PC=0x%08x)\n", rb, m_ppc);
			break;
		case 0x2a:  // t
			SET_T(check_condition(rb));
			break;
		case 0x2b:  // mv
			if ((rb & 0x0f) != 14 && check_condition(rb))
				m_gpr[rd] = m_gpr[ra];
			break;
		case 0x2c:  // extsb
		case 0x2d:  // extsh
			m_gpr[rd] = sext(m_gpr[ra], (GET_S_FUNC6(m_op) & 1) ? 16 : 8);
			if (cu)
			{
				CHECK_N(m_gpr[rd]);
				CHECK_Z(m_gpr[rd]);     // undefined
			}
			break;
		case 0x2e:  // extzb
		case 0x2f:  // extzh
			m_gpr[rd] = m_gpr[ra] & ((GET_S_FUNC6(m_op) & 1) ? 0xffff : 0x00ff);
			if (cu)
			{
				CHECK_N(m_gpr[rd]);
				CHECK_Z(m_gpr[rd]);     // undefined
			}
			break;
		case 0x30:  // lcb
			unemulated_op("lcb");
			break;
		case 0x31:  // lcw
			unemulated_op("lcw");
			break;
		case 0x33:  // lce
			unemulated_op("lce");
			break;
		case 0x34:  // scb
			unemulated_op("scb");
			break;
		case 0x35:  // scw
			unemulated_op("scw");
			break;
		case 0x37:  // sce
			unemulated_op("sce");
			break;
		case 0x38:  // slli
			r = m_gpr[ra] << rb;
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				SET_C(BIT(m_gpr[ra], 32 - rb));
			}
			m_gpr[rd] = r;
			break;
		case 0x3a:  // srli
			r = m_gpr[ra] >> rb;
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				SET_C(BIT(m_gpr[ra], rb - 1));
			}
			m_gpr[rd] = r;
			break;
		case 0x3b:  // srai
			r = sext(m_gpr[ra] >> rb, 32 - rb);
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				SET_C(BIT(m_gpr[ra], rb - 1));
			}
			m_gpr[rd] = r;
			break;
		case 0x3c:  // rori
		{
			const uint32_t n = rb & 0x1f;
			r = n ? ((m_gpr[ra] >> n) | (m_gpr[ra] << (32 - n))) : m_gpr[ra];
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				if (n) SET_C(BIT(m_gpr[ra], n - 1));
			}
			m_gpr[rd] = r;
			break;
		}
		case 0x3d:  // roric
		{
			const uint32_t n = (rb & 0x1f) % 33;
			if (n)
			{
				const uint64_t v = ((uint64_t)GET_C << 32) | m_gpr[ra];
				const uint64_t x = ((v >> n) | (v << (33 - n))) & 0x1ffffffffULL;
				r = (uint32_t)x;
				if (cu) SET_C(BIT64(x, 32));
			}
			else
				r = m_gpr[ra];
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
			}
			m_gpr[rd] = r;
			break;
		}
		case 0x3e:  // roli
		{
			const uint32_t n = rb & 0x1f;
			r = n ? ((m_gpr[ra] << n) | (m_gpr[ra] >> (32 - n))) : m_gpr[ra];
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				if (n) SET_C(BIT(m_gpr[ra], 32 - n));
			}
			m_gpr[rd] = r;
			break;
		}
		case 0x3f:  // rolic
		{
			const uint32_t n = (rb & 0x1f) % 33;
			if (n)
			{
				const uint64_t v = ((uint64_t)GET_C << 32) | m_gpr[ra];
				const uint64_t x = ((v << n) | (v >> (33 - n))) & 0x1ffffffffULL;
				r = (uint32_t)x;
				if (cu) SET_C(BIT64(x, 32));
			}
			else
				r = m_gpr[ra];
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
			}
			m_gpr[rd] = r;
			break;
		}
		default:
			op_undef();
	}
}

static void op_iform1(void)
{
	uint8_t rd = GET_I_RD(m_op);
	uint32_t imm16 = GET_I_IMM16(m_op);
	int32_t simm16 = sext(imm16, 16);
	uint8_t cu = GET_I_CU(m_op);
	uint32_t r;

	switch (GET_I_FUNC3(m_op))
	{
		case 0: // addi
			r = m_gpr[rd] + simm16;
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				CHECK_V_ADD(m_gpr[rd], (uint32_t)simm16, r);
				CHECK_C_ADD(m_gpr[rd], (uint32_t)simm16);
			}
			m_gpr[rd] = r;
			break;
		case 2: // cmpi
			if (cu)
			{
				r = m_gpr[rd] - simm16;
				CHECK_Z(r);
				CHECK_N(r);
				CHECK_V_SUB(m_gpr[rd], (uint32_t)simm16, r);
				CHECK_C_SUB(m_gpr[rd], (uint32_t)simm16);
			}
			break;
		case 4: // andi
			m_gpr[rd] = m_gpr[rd] & imm16;
			if (cu)
			{
				CHECK_Z(m_gpr[rd]);
				CHECK_N(m_gpr[rd]);
			}
			break;
		case 5: // ori
			m_gpr[rd] = m_gpr[rd] | imm16;
			if (cu)
			{
				CHECK_Z(m_gpr[rd]);
				CHECK_N(m_gpr[rd]);
			}
			break;
		case 6: // ldi
			m_gpr[rd] = simm16;
			break;
		default:
			op_undef();
	}
}

static void op_jump(void)
{
	if (GET_J_LK(m_op))
		REG_LNK = m_pc;

	m_pc = (m_ppc & 0xfe000000) | (GET_J_DISP24(m_op) << 1);
}

static void op_loadstore_rix(uint8_t rd, uint32_t addr)
{
	switch (GET_RIX_FUNC3(m_op))
	{
		case 0: m_gpr[rd] = read_dword(addr);                 break; // lw
		case 1: m_gpr[rd] = sext(read_word(addr), 16);        break; // lh
		case 2: m_gpr[rd] = read_word(addr);                  break; // lhu
		case 3: m_gpr[rd] = sext(read_byte(addr), 8);         break; // lb
		case 4: write_dword(addr, m_gpr[rd]);                 break; // sw
		case 5: write_word(addr, m_gpr[rd] & 0xffff);         break; // sh
		case 6: m_gpr[rd] = read_byte(addr);                  break; // lbu
		case 7: write_byte(addr, m_gpr[rd] & 0xff);           break; // sb
	}
}

static void op_rixform1(void)
{
	uint8_t ra = GET_RIX_RA(m_op);
	uint8_t rd = GET_RIX_RD(m_op);

	// pre-increment
	m_gpr[ra] += sext(GET_RIX_IMM12(m_op), 12);
	op_loadstore_rix(rd, m_gpr[ra]);
}

static void op_branch(void)
{
	if (check_condition_branch(GET_BC_BC(m_op)))
	{
		int32_t disp = sext(GET_BC_DISP19(m_op), 19) * 2;
		if (GET_BC_LK(m_op))
			REG_LNK = m_pc;

		m_pc = m_ppc + disp;
	}
}

static void op_iform2(void)
{
	uint8_t rd = GET_I_RD(m_op);
	uint32_t imm16 = GET_I_IMM16(m_op) << 16;
	int32_t simm16 = (int32_t)imm16;
	uint8_t cu = GET_I_CU(m_op);
	uint32_t r;

	switch (GET_I_FUNC3(m_op))
	{
		case 0: // addis
			r = m_gpr[rd] + simm16;
			if (cu)
			{
				CHECK_Z(r);
				CHECK_N(r);
				CHECK_V_ADD(m_gpr[rd], imm16, r);
				CHECK_C_ADD(m_gpr[rd], imm16);
			}
			m_gpr[rd] = r;
			break;
		case 2: // cmpis
			if (cu)
			{
				r = m_gpr[rd] - simm16;
				CHECK_Z(r);
				CHECK_N(r);
				CHECK_V_SUB(m_gpr[rd], imm16, r);
				CHECK_C_SUB(m_gpr[rd], imm16);
			}
			break;
		case 4: // andis
			m_gpr[rd] = m_gpr[rd] & imm16;
			if (cu)
			{
				CHECK_Z(m_gpr[rd]);
				CHECK_N(m_gpr[rd]);
			}
			break;
		case 5: // oris
			m_gpr[rd] = m_gpr[rd] | imm16;
			if (cu)
			{
				CHECK_Z(m_gpr[rd]);
				CHECK_N(m_gpr[rd]);
			}
			break;
		case 6: // ldis
			m_gpr[rd] = imm16;
			break;
		default:
			op_undef();
	}
}

static void op_crform(void)
{
	if ((REG_PSR & 0x08) && !(REG_PSR & 0x10000000))
		return;

	uint8_t cr = GET_CR_CR(m_op);
	uint8_t rd = GET_CR_RD(m_op);

	switch (GET_CR_OP(m_op))
	{
		case 0x00:  // mtcr
			m_cr[cr] = m_gpr[rd];
			break;
		case 0x01:  // mfcr
			m_gpr[rd] = m_cr[cr];
			break;
		case 0x84:  // rte
			REG_PSR = (REG_PSR & ~0x03u) | ((REG_PSR >> 2) & 0x03);
			REG_CR = (REG_CR & ~0x1fu) | ((REG_CR >> 5) & 0x1f);
			m_pc = REG_EPC;
			break;
		default:
			if ((GET_CR_OP(m_op) & 0xc0) == 0)
				hs_fatal("unemulated Coprocessor 0x%x (PC=0x%08x)", GET_CR_OP(m_op) & 0x07, m_ppc);
			else
				op_undef();
	}
}

static void op_rixform2(void)
{
	uint8_t ra = GET_RIX_RA(m_op);
	uint8_t rd = GET_RIX_RD(m_op);

	op_loadstore_rix(rd, m_gpr[ra]);

	// post-increment
	m_gpr[ra] += sext(GET_RIX_IMM12(m_op), 12);
}

static void op_addri(void)
{
	uint8_t ra = GET_RI_RA(m_op);
	uint8_t rd = GET_RI_RD(m_op);
	int32_t simm14 = sext(GET_RI_IMM14(m_op), 14);
	uint8_t cu = GET_RI_CU(m_op);

	uint32_t r = m_gpr[ra] + simm14;
	if (cu)
	{
		CHECK_Z(r);
		CHECK_N(r);
		CHECK_V_ADD(m_gpr[ra], (uint32_t)simm14, r);
		CHECK_C_ADD(m_gpr[ra], (uint32_t)simm14);
	}
	m_gpr[rd] = r;
}

static void op_andri(void)
{
	uint8_t ra = GET_RI_RA(m_op);
	uint8_t rd = GET_RI_RD(m_op);
	uint32_t imm14 = GET_RI_IMM14(m_op);

	m_gpr[rd] = m_gpr[ra] & imm14;

	if (GET_RI_CU(m_op))
	{
		CHECK_Z(m_gpr[rd]);
		CHECK_N(m_gpr[rd]);
	}
}

static void op_orri(void)
{
	uint8_t ra = GET_RI_RA(m_op);
	uint8_t rd = GET_RI_RD(m_op);
	uint32_t imm14 = GET_RI_IMM14(m_op);

	m_gpr[rd] = m_gpr[ra] | imm14;

	if (GET_RI_CU(m_op))
	{
		CHECK_Z(m_gpr[rd]);
		CHECK_N(m_gpr[rd]);
	}
}

#define LS_ADDR (m_gpr[GET_LS_RA(m_op)] + sext(GET_LS_IMM15(m_op), 15))
static void op_lw(void)  { m_gpr[GET_LS_RD(m_op)] = read_dword(LS_ADDR); }
static void op_lh(void)  { m_gpr[GET_LS_RD(m_op)] = sext(read_word(LS_ADDR), 16); }
static void op_lhu(void) { m_gpr[GET_LS_RD(m_op)] = read_word(LS_ADDR); }
static void op_lb(void)  { m_gpr[GET_LS_RD(m_op)] = sext(read_byte(LS_ADDR), 8); }
static void op_sw(void)  { write_dword(LS_ADDR, m_gpr[GET_LS_RD(m_op)]); }
static void op_sh(void)  { write_word(LS_ADDR, m_gpr[GET_LS_RD(m_op)] & 0xffff); }
static void op_lbu(void) { m_gpr[GET_LS_RD(m_op)] = read_byte(LS_ADDR); }
static void op_sb(void)  { write_byte(LS_ADDR, m_gpr[GET_LS_RD(m_op)] & 0xff); }

static void op_cache(void)
{
	//unemulated_op("CACHE");
}

static void op_cenew(void)
{
	unemulated_op("CENew");
}


/* ------------------------------------------------------------------------ */
/*  16-bit opcodes                                                           */
/* ------------------------------------------------------------------------ */

static void op_rform1(void)
{
	uint8_t rd = GET_R_RD(m_op);
	uint8_t ra = GET_R_RA(m_op);

	switch (GET_R_FUNC4(m_op))
	{
		case 0x00:  // nop!
			break;
		case 0x01:  // mlfh!
			m_gpr[rd] = m_gpr[0x10 + ra];
			break;
		case 0x02:  // mhfl!
			m_gpr[0x10 + rd] = m_gpr[ra];
			break;
		case 0x03:  // mv!
			m_gpr[rd] = m_gpr[ra];
			break;
		case 0x04:  // br!
			if (check_condition_branch(rd))
				m_pc = m_gpr[ra];
			break;
		case 0x05:  // t!
			SET_T(check_condition(rd));
			break;
		case 0x08:  // sll!
			m_gpr[rd] = m_gpr[rd] << (m_gpr[ra] & 0x1f);
			break;
		case 0x09:  // addc!
			m_gpr[rd] = m_gpr[rd] + m_gpr[ra] + GET_C;
			break;
		case 0x0a:  // srl!
			m_gpr[rd] = m_gpr[rd] >> (m_gpr[ra] & 0x1f);
			break;
		case 0x0b:  // sra!
			m_gpr[rd] = sext(m_gpr[rd] >> (m_gpr[ra] & 0x1f), 32 - (m_gpr[ra] & 0x1f));
			break;
		case 0x0c:  // brl!
			if (check_condition_branch(rd))
			{
				REG_LNK = m_pc;
				m_pc = m_gpr[ra];
			}
			break;
		default:
			op_undef();
	}
}

static void op_rform2(void)
{
	uint8_t rd = GET_R_RD(m_op);
	uint8_t ra = GET_R_RA(m_op);
	uint32_t r;

	switch (GET_R_FUNC4(m_op))
	{
		case 0x00:  // add!
			r = m_gpr[rd] + m_gpr[ra];
			CHECK_Z(r);
			CHECK_N(r);
			CHECK_V_ADD(m_gpr[rd], m_gpr[ra], r);
			CHECK_C_ADD(m_gpr[rd], m_gpr[ra]);
			m_gpr[rd] = r;
			break;
		case 0x01:  // sub!
			r = m_gpr[rd] - m_gpr[ra];
			CHECK_Z(r);
			CHECK_N(r);
			CHECK_V_SUB(m_gpr[rd], m_gpr[ra], r);
			CHECK_C_SUB(m_gpr[rd], m_gpr[ra]);
			m_gpr[rd] = r;
			break;
		case 0x02:  // neg!
			r = 0 - m_gpr[ra];
			CHECK_Z(r);
			CHECK_N(r);
			CHECK_V_SUB(0u, m_gpr[ra], r);
			CHECK_C_SUB(0u, m_gpr[ra]);
			m_gpr[rd] = r;
			break;
		case 0x03:  // cmp!
			r = m_gpr[rd] - m_gpr[ra];
			CHECK_Z(r);
			CHECK_N(r);
			CHECK_V_SUB(m_gpr[rd], m_gpr[ra], r);
			CHECK_C_SUB(m_gpr[rd], m_gpr[ra]);
			break;
		case 0x04:  // and!
			m_gpr[rd] &= m_gpr[ra];
			CHECK_Z(m_gpr[rd]);
			CHECK_N(m_gpr[rd]);
			break;
		case 0x05:  // or!
			m_gpr[rd] |= m_gpr[ra];
			CHECK_Z(m_gpr[rd]);
			CHECK_N(m_gpr[rd]);
			break;
		case 0x06:  // not!
			m_gpr[rd] = ~m_gpr[ra];
			CHECK_Z(m_gpr[rd]);
			CHECK_N(m_gpr[rd]);
			break;
		case 0x07:  // xor!
			m_gpr[rd] ^= m_gpr[ra];
			CHECK_Z(m_gpr[rd]);
			CHECK_N(m_gpr[rd]);
			break;
		case 0x08:  // lw!
			m_gpr[rd] = read_dword(m_gpr[ra]);
			break;
		case 0x09:  // lh!
			m_gpr[rd] = sext(read_word(m_gpr[ra]), 16);
			break;
		case 0x0a:  // pop!
			m_gpr[GET_P_RDG(m_op)] = read_dword(m_gpr[GET_P_RAG(m_op)]);
			m_gpr[GET_P_RAG(m_op)] += 0x04;
			break;
		case 0x0b:  // lbu!
			m_gpr[rd] = read_byte(m_gpr[ra]);
			break;
		case 0x0c:  // sw!
			write_dword(m_gpr[ra], m_gpr[rd]);
			break;
		case 0x0d:  // sh!
			write_word(m_gpr[ra], m_gpr[rd] & 0xffff);
			break;
		case 0x0e:  // push
			m_gpr[GET_P_RAG(m_op)] -= 0x04;
			write_dword(m_gpr[GET_P_RAG(m_op)], m_gpr[GET_P_RDG(m_op)]);
			break;
		case 0x0f:  // sb!
			write_byte(m_gpr[ra], m_gpr[rd] & 0xff);
			break;
	}
}

static void op_jform(void)
{
	if (GET_J_LK(m_op))
		REG_LNK = m_pc;

	m_pc = (m_ppc & 0xfffff000) | (GET_J_DISP11(m_op) << 1);
}

static void op_branch16(void)
{
	if (check_condition_branch(GET_BX_EC(m_op)))
		m_pc = m_ppc + (sext(GET_BX_DISP8(m_op), 8) * 2);
}

static void op_ldiu(void)
{
	m_gpr[GET_I2_RD(m_op)] = GET_I2_IMM8(m_op);
}

static void op_iform1a(void)
{
	uint8_t rd = GET_I16_RD(m_op);
	uint8_t imm5 = GET_I16_IMM5(m_op);

	switch (GET_I16_FUNC3(m_op))
	{
		case 0x00:  // addei!
			if (imm5 & 0x10)
				m_gpr[rd] -= 1u << (imm5 & 0xf);
			else
				m_gpr[rd] += 1u << (imm5 & 0xf);

			// according to the datasheet, the flags are undefined after this instruction, but xmen expects the Z flag to be valid.
			CHECK_Z(m_gpr[rd]);
			CHECK_N(m_gpr[rd]);
			break;
		case 0x01:  // slli!
			SET_C(BIT(m_gpr[rd], 32 - imm5));
			m_gpr[rd] <<= imm5;
			CHECK_Z(m_gpr[rd]);
			CHECK_N(m_gpr[rd]);
			break;
		case 0x02:  // sdbbp!
			unemulated_op("sdbbp!");
			break;
		case 0x03:  // srli!
			SET_C(BIT(m_gpr[rd], imm5 - 1));
			m_gpr[rd] >>= imm5;
			CHECK_Z(m_gpr[rd]);
			CHECK_N(m_gpr[rd]);
			break;
		case 0x04:  // bitclr!
			m_gpr[rd] &= ~(1u << imm5);
			CHECK_Z(m_gpr[rd]);
			CHECK_N(m_gpr[rd]);
			break;
		case 0x05:  // bitset!
			m_gpr[rd] |= (1u << imm5);
			CHECK_Z(m_gpr[rd]);
			CHECK_N(m_gpr[rd]);
			break;
		case 0x06:  // bittst!
			CHECK_N(m_gpr[rd]);
			CHECK_Z(m_gpr[rd] & (1u << imm5));
			break;
		case 0x07:  // bittgl!
			m_gpr[rd] ^= (1u << imm5);
			CHECK_Z(m_gpr[rd]);
			CHECK_N(m_gpr[rd]);
			break;
	}
}

static void op_iform1b(void)
{
	uint8_t rd = GET_I16_RD(m_op);
	uint16_t imm5 = GET_I16_IMM5(m_op);

	switch (GET_I16_FUNC3(m_op))
	{
		case 0x00:  // lwp!
			m_gpr[rd] = read_dword(REG_BP + (imm5 << 2));
			break;
		case 0x01:  // lhp!
			m_gpr[rd] = sext(read_word(REG_BP + (imm5 << 1)), 16);
			break;
		case 0x03:  // lbup!
			m_gpr[rd] = read_byte(REG_BP + imm5);
			break;
		case 0x04:  // swp!
			write_dword(REG_BP + (imm5 << 2), m_gpr[rd]);
			break;
		case 0x05:  // shp!
			write_word(REG_BP + (imm5 << 1), m_gpr[rd] & 0xffff);
			break;
		case 0x07:  // sbp!
			write_byte(REG_BP + imm5, m_gpr[rd] & 0xff);
			break;
		default:
			op_undef();
	}
}


/* ------------------------------------------------------------------------ */
/*  tables and execution                                                     */
/* ------------------------------------------------------------------------ */

typedef void (*op_handler)(void);

static const op_handler s_opcode32_table[4*8] =
{
	op_specialform, op_iform1, op_jump,  op_rixform1, op_branch, op_iform2, op_crform, op_rixform2,
	op_addri,       op_undef,  op_undef, op_undef,    op_andri,  op_orri,   op_undef,  op_undef,
	op_lw,          op_lh,     op_lhu,   op_lb,       op_sw,     op_sh,     op_lbu,    op_sb,
	op_cache,       op_undef,  op_undef, op_undef,    op_cenew,  op_undef,  op_undef,  op_undef
};

static const op_handler s_opcode16_table[8] =
{
	op_rform1, op_undef, op_rform2, op_jform, op_branch16, op_ldiu, op_iform1a, op_iform1b
};

/* Idle-loop skip. A backward branch that comes back to the same loop head with every
 * register unchanged, and no store or I/O access in between, repeats forever until an
 * event: memory and registers are the same at each iteration. Whole iterations are skipped
 * up to the next event (never past it), so the resulting state and timing are exactly
 * those of executing the loop. Off while tracing, as it changes instruction counts. */
#define LOOP_REGS (sizeof(g_cpu.gpr) + sizeof(g_cpu.cr) + sizeof(g_cpu.sr) + sizeof(g_cpu.ce))
static uint32_t s_loop_pc = 1;
static uint64_t s_loop_cycles, s_loop_stores, s_loop_io;
static uint32_t s_loop_regs[LOOP_REGS / 4];

static void idle_check(void)
{
	if (s_loop_pc == m_pc && s_loop_stores == g_cpu.stores && s_loop_io == g_cpu.io_total
		&& !(g_cpu.pending && (REG_PSR & 1))
		&& !memcmp(s_loop_regs, g_cpu.gpr, LOOP_REGS))
	{
		const uint64_t period = g_cpu.cycles - s_loop_cycles;
		const uint64_t target = g_next_event_cycle;
		if (period && target != UINT64_MAX && target > g_cpu.cycles)
		{
			const uint64_t k = (target - g_cpu.cycles) / period;
			g_cpu.cycles += k * period;
			g_cpu.idle_skipped += k;
		}
	}
	s_loop_pc = m_pc;
	s_loop_cycles = g_cpu.cycles;
	s_loop_stores = g_cpu.stores;
	s_loop_io = g_cpu.io_total;
	memcpy(s_loop_regs, g_cpu.gpr, LOOP_REGS);
}

void score7_reset(void)
{
	memset(&g_cpu, 0, sizeof(g_cpu));
	g_cpu.cpi = 6;
	REG_EXCPVEC = m_pc = 0x9f000000;
}

/* queued interrupt requests, delivered at the next instruction boundary */
static int s_inq[64];
static int s_inq_n;

void score7_set_input(int line)
{
	if (s_inq_n < 64) s_inq[s_inq_n++] = line;
}

void score7_flush_inputs(void)
{
	for (int i = 0; i < s_inq_n; i++)
	{
		int line = s_inq[i];
		if (g_trace) trace_printf("L %d c=%llu\n", line, (unsigned long long)g_cpu.cycles);
		if (line > 0 && line < 64)
			g_cpu.pending |= 1ULL << line;
	}
	s_inq_n = 0;
}

void score7_step(void)
{
	m_ppc = m_pc;
	if (g_trace) trace_begin();

	if (g_cpu.pending)
		check_irq();

	const uint32_t ipc = m_pc;
	uint32_t op = bus_fetch32(m_pc & ~3u);
	const int form = ((op >> 30) & 2) | ((op >> 15) & 1);

	switch (form)
	{
		case 0: // 16-bit + 16-bit instruction
			m_op = ((m_pc & 0x02) ? (op >> 16) : op) & 0x7fff;
			m_pc += 2;
			s_opcode16_table[(m_op >> 12) & 0x07]();
			break;
		case 1: // undefined parity bit
			m_pc += 4;
			gen_exception(EXCEPTION_P_EL, 0);
			break;
		case 2: // parallel conditional execution
			m_op = (GET_T ? op : (op >> 16)) & 0x7fff;
			m_pc += 4;
			s_opcode16_table[(m_op >> 12) & 0x07]();
			break;
		case 3: // 32-bit instruction
			m_op = (op & 0x7fff) | ((op >> 1) & 0x3fff8000);
			m_pc += 4;
			s_opcode32_table[(m_op >> 25) & 0x01f]();
			break;
	}

	// FIXME (MAME): if available use correct cycles per instructions
	g_cpu.cycles += g_cpu.cpi;
	if (g_cpu.io_count)
	{
		g_cpu.cycles += (uint64_t)g_cpu.io_cycles * g_cpu.io_count;
		g_cpu.io_total += g_cpu.io_count;
		g_cpu.io_count = 0;
	}
	if (ipc == g_mark_pc) g_mark_count++;
	if (g_cpu.idle_skip && m_pc < ipc && ipc - m_pc <= 64 && !g_trace)
		idle_check();

	if (g_trace)
		trace_end(ipc, form == 0 ? ((ipc & 2) ? op >> 16 : op) : op, form == 0);
	if (s_inq_n)
		score7_flush_inputs();
}
