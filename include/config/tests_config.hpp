#pragma once

/*
 * tests_config.hpp  --  THE place where you describe the tests (packet to send + expected result).
 * -------------------------------------------------------------------------------------------------
 * MRC version (OCP MRC 1.0). Each TestCase = one MRC packet sent to the module + what must come out.
 * Cases are grouped in a Scenario (a list of cases run back to back on the same QP).
 *
 * PSN references (resolved by the testbench when the case runs):
 *     Epsn(k)       = epsn + k        epsn     = cack_psn + 1 (next PSN needed to close the left edge)
 *     LastPsn(k)    = last_psn + k    last_psn = PSN of the last ACCEPTED packet
 *     Abs(v)        = the literal value v
 *     SamePsn()     = the PSN of the packet that was sent
 *     RelToFirst(k) = initialEpsn + k (masked to 24 bits). Use this for anything out of order:
 *                     in MRC epsn only moves when holes are filled, so absolute PSNs are easier to read.
 *     Any()         = placeholder for a PSN column that is ignored
 *
 *   MRC state update after every case (replaces the RC rule):
 *     if payload WRITTEN: mark psn as received; while (epsn is marked) epsn++;  last_psn = psn
 *
 * TestCase columns (in order):
 *   name, opcode, psn, vaddr, dmaLen, payloadLen, payloadFill, flags, mmsn,   <- what is SENT
 *   mem, memAddr, memLen, memLast,                                           <- expected memory side
 *   reply, ackPsn, ackMsn,                                                   <- expected transport ACK (0xD1)
 *   rel, cackPsn,                                                            <- expected SACK/NACK (0xDC/0xDD)
 *   drop                                                                     <- expected drop counter
 *
 *   vaddr   : RETH va of THIS packet (MRC puts a RETH on every packet; va advances by PMTU).
 *   dmaLen  : RETH dma length = whole message length, same in every packet of the message.
 *   flags   : F_AR (BTH AckReq) | F_RTX (BTH rtx) | F_TS (BTH ts + 4-byte TSETH after the METH)
 *   mmsn    : MSN carried in the METH (0-based message index; RQMSN is sent as 0).
 *   mem     : MEM_WRITE (payload reaches memory)  |  MEM_NONE (nothing is written)
 *   reply   : REPLY_ACK (MRC transport ACK 0xD1 + AETH) | REPLY_NONE
 *   rel     : REL_SACK | REL_NACK | REL_NONE.  cackPsn: SETH cack_psn, checked only for REL_SACK.
 *   drop    : DROP_INC (regInvalidPsnDropCount +1) | DROP_SAME (unchanged) | DROP_ANY (not checked)
 *   memAddr/memLen/memLast : ignored unless mem == MEM_WRITE.   ackPsn/ackMsn: ignored if REPLY_NONE.
 *
 *   Typical MRC outcomes (spec section 6.3.1):
 *     in window, new (in order OR out of order)  : MEM_WRITE, ACK once the left edge closes, SACK if AR/rtx
 *     duplicate, PSN in (epsn - 2^23, epsn)       : MEM_NONE,  REPLY_ACK, REL_SACK, DROP_ANY
 *     duplicate, already received above epsn      : MEM_NONE,  REPLY_NONE, REL_SACK, DROP_ANY
 *     outside window (>= epsn + MRC_WINDOW, or <= epsn - 2^23) : MEM_NONE, REPLY_NONE, OOR_REL, DROP_INC
 *
 *   Assumptions baked into the expectations below:
 *     - FIRST / MIDDLE rows expect no SACK, so set the module's sack_gen_threshold high enough that
 *       the byte-count trigger never fires during these scenarios.
 *     - While a hole exists the transport ACK is held back; when the hole closes one ACK covers
 *       everything up to the new cack_psn. This is a responder design choice, not dictated by the spec.
 * -------------------------------------------------------------------------------------------------
 */

#include <cstdint>
#include <vector>

#include "ib_transport_protocol/ib_transport_protocol.hpp"
#include "tb_config.hpp"
#include "tests.hpp"   // MemExpect / ReplyExpect / DropExpect

// ---- Parameters that must match the module ----
static constexpr int64_t  MRC_WINDOW = 128;   // responder max_psn_range, in packets
static constexpr uint32_t PKT        = 1024;  // payload of FIRST/MIDDLE packets: must equal the module's PMTU

// ---- Request BTH flags (F_*) and RelExpect live in tests.hpp ----

// What an out-of-window packet produces. Spec 6.3.1 rule 6 says "silently dropped",
// but Table 7-3 lists PSN_OOR_WINDOW (0x0B) as a mandatory NACK. Pick the one the module implements.
static constexpr RelExpect OOR_REL = REL_NONE;   // or REL_NACK

struct PsnRef
{
	enum Base { EPSN, LAST, ABS, SAME, REL } base;
	int64_t offset;
};
static PsnRef Epsn(int64_t k = 0)      { return { PsnRef::EPSN, k }; }          // epsn + k
static PsnRef LastPsn(int64_t k = 0)   { return { PsnRef::LAST, k }; }          // last accepted psn + k
static PsnRef Abs(uint32_t v)          { return { PsnRef::ABS,  (int64_t)v }; } // v
static PsnRef SamePsn()                { return { PsnRef::SAME, 0 }; }          // psn of the pkt just sent
static PsnRef RelToFirst(int64_t v)    { return { PsnRef::REL,  v }; }          // initialEpsn + v (v may be < 0)
static PsnRef Any()                    { return { PsnRef::ABS,  0 }; }          // ignored column

struct TestCase
{
	const char* name;
	// sent
	mrcOpCode	opcode;
	PsnRef      psn;
	uint64_t    vaddr;
	uint32_t    dmaLen;
	uint32_t    payloadLen;
	uint8_t     payloadFill;
	uint8_t     flags;
	uint16_t    mmsn;
	// expected: memory side
	MemExpect   mem;
	uint64_t    memAddr;
	uint32_t    memLen;
	bool        memLast;
	// expected: transport reply
	ReplyExpect reply;
	PsnRef      ackPsn;
	uint32_t    ackMsn;
	// expected: reliability reply
	RelExpect   rel;
	PsnRef      cackPsn;
	// expected: drop counter
	DropExpect  drop;
};

struct Scenario
{
	const char*           name;
	uint32_t              initialEpsn;
	int64_t               initialLastPsn;   // -1 -> initialEpsn - 1
	std::vector<TestCase> cases;
};

// =================================================================================================
// MRC header parsing with RC transport behaviour.
//
// Replace the scenario section of tests_config.hpp with this block.
//
// Packets use the MRC format (MRC opcodes, METH, optional TSETH, RETH on every packet), but every
// expectation is RC behaviour: strict in-order PSNs, NAK on a sequence error (only once, until a
// valid packet arrives), ACK on every accepted packet, no SACK/NACK reliability messages.
//
// Conventions:
//   - F_AR is set on EVERY packet, including FIRST/MIDDLE. RC ACKs every packet; the MRC case in
//     rx_exh_fsm ACKs only when BTH.a = 1. Setting it everywhere makes both behave the same.
//   - ackMsn follows IB semantics: it increments when a message completes (LAST / ONLY),
//     so FIRST/MIDDLE ACKs repeat the previous value.
//   - rel = REL_NONE everywhere: no SACK must be generated yet.
//   - PSNs use Epsn(k): with RC transport epsn simply increments on every accepted packet.
//
// The size tests in group 08 are computed for a 512-bit bus (64 bytes per word).
// After the BTH shifter, the stripper sees  METH(4) [+ TSETH(4)] + RETH(16) + payload  bytes.
// =================================================================================================
 
static const Scenario SCENARIO_MRC_PARSE = {
	"MRC parse, RC transport",
	FIRST_RX_PSN, -1,
	{
	// name                                    opcode                 psn          vaddr            dmaLen      payload fill  flags          mmsn | mem        memAddr          memLen last  | reply       ackPsn       msn | rel       cackPsn | drop
 
	// ---- 01: PSN ahead of epsn -> RC sequence-error NAK (it sends an NAK only the first time) ----
	{ "TEST 01: ONLY ahead not in win",        MRC_RDMA_WRITE_ONLY,   Epsn(140),   0x3000,          64,         64,     0xEE, F_AR,          0,     MEM_NONE,  0,               0,     false, REPLY_NAK,  LastPsn(1),  0,  REL_NONE, Any(),  DROP_INC  },
 
	// ---- 02: in-order single-packet writes; distinct fills prove each one overwrote 0x1000 ----
	{ "TEST 02a: ONLY in order",               MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x1000,          1000,       1000,   0x01, F_AR,          0,     MEM_WRITE, 0x1000,          1000,  true,  REPLY_ACK,  SamePsn(),   1,  REL_NONE, Any(),  DROP_SAME },
	{ "TEST 02b: ONLY in order",               MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x1000,          1000,       1000,   0x02, F_AR,          1,     MEM_WRITE, 0x1000,          1000,  true,  REPLY_ACK,  SamePsn(),   2,  REL_NONE, Any(),  DROP_SAME },
	{ "TEST 02c: ONLY in order",               MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x1000,          1000,       1000,   0x03, F_AR,          2,     MEM_WRITE, 0x1000,          1000,  true,  REPLY_ACK,  SamePsn(),   3,  REL_NONE, Any(),  DROP_SAME },
	{ "TEST 02d: ONLY in order",               MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x1000,          1000,       1000,   0x04, F_AR,          3,     MEM_WRITE, 0x1000,          1000,  true,  REPLY_ACK,  SamePsn(),   4,  REL_NONE, Any(),  DROP_SAME },
	{ "TEST 02e: ONLY in order",               MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x1000,          1000,       1000,   0x05, F_AR,          4,     MEM_WRITE, 0x1000,          1000,  true,  REPLY_ACK,  SamePsn(),   5,  REL_NONE, Any(),  DROP_SAME },
	{ "TEST 02f: ONLY in order",               MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x1000,          1000,       1000,   0x06, F_AR,          5,     MEM_WRITE, 0x1000,          1000,  true,  REPLY_ACK,  SamePsn(),   6,  REL_NONE, Any(),  DROP_SAME },
	{ "TEST 02g: ONLY in order",               MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x1000,          1000,       1000,   0x07, F_AR,          6,     MEM_WRITE, 0x1000,          1000,  true,  REPLY_ACK,  SamePsn(),   7,  REL_NONE, Any(),  DROP_SAME },
	{ "TEST 02h: ONLY in order",               MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x1000,          1000,       1000,   0x08, F_AR,          7,     MEM_WRITE, 0x1000,          1000,  true,  REPLY_ACK,  SamePsn(),   8,  REL_NONE, Any(),  DROP_SAME },
 
	// ---- 03: duplicate of the last accepted packet -> re-ACKed, not rewritten (Coyote increases) ----
	{ "TEST 03a: ONLY in order",               MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x4000,          1000,       1000,   0x09, F_AR,          8,     MEM_WRITE, 0x4000,          1000,  true,  REPLY_ACK,  SamePsn(),   9,  REL_NONE, Any(),  DROP_SAME },
	{ "TEST 03b: ONLY duplicate of 03a",       MRC_RDMA_WRITE_ONLY,   Epsn(-1),    0x4000,          1000,       1000,   0x09, F_AR,          8,     MEM_NONE,  0,               0,     false, REPLY_ACK,  SamePsn(),   9,  REL_NONE, Any(),  DROP_INC  },
 
	// ---- 04: older PSNs (RC duplicate region) -> re-ACKed, not rewritten ----
	{ "TEST 04a: ONLY behind epsn",            MRC_RDMA_WRITE_ONLY,   Epsn(-5),    0x5000,          1024,       1024,   0xEE, F_AR,          0,     MEM_NONE,  0,               0,     false, REPLY_ACK,  SamePsn(),   9,  REL_NONE, Any(),  DROP_ANY  },
	{ "TEST 04b: ONLY behind epsn",            MRC_RDMA_WRITE_ONLY,   Epsn(-30),   0x5000,          1024,       1024,   0xEE, F_AR,          0,     MEM_NONE,  0,               0,     false, REPLY_ACK,  SamePsn(),   9,  REL_NONE, Any(),  DROP_ANY  },
	{ "TEST 04c: ONLY behind epsn",            MRC_RDMA_WRITE_ONLY,   Epsn(-1),    0x5000,          1024,       1024,   0xEE, F_AR,          0,     MEM_NONE,  0,               0,     false, REPLY_ACK,  SamePsn(),   9,  REL_NONE, Any(),  DROP_ANY  },
 
	// ---- 05: multi-packet write. MRC parsing: RETH on MIDDLE/LAST too, each with its own vaddr ----
	{ "TEST 05a: FIRST",                       MRC_RDMA_WRITE_FIRST,  Epsn(0),     0x10000,         2*PKT+100,  PKT,    0xA1, F_AR,          9,     MEM_WRITE, 0x10000,         PKT,   false, REPLY_ACK,  SamePsn(),   9,  REL_NONE, Any(),  DROP_SAME },
	{ "TEST 05b: MIDDLE",                      MRC_RDMA_WRITE_MIDDLE, Epsn(0),     0x10000+PKT,     2*PKT+100,  PKT,    0xA2, F_AR,          9,     MEM_WRITE, 0x10000+PKT,     PKT,   false, REPLY_ACK,  SamePsn(),   9,  REL_NONE, Any(),  DROP_SAME },
	{ "TEST 05c: LAST",                        MRC_RDMA_WRITE_LAST,   Epsn(0),     0x10000+2*PKT,   2*PKT+100,  100,    0xA3, F_AR,          9,     MEM_WRITE, 0x10000+2*PKT,   100,   true,  REPLY_ACK,  SamePsn(),   10, REL_NONE, Any(),  DROP_SAME },
 
	// ---- 06: TSETH -> the stripper removes 8 bytes instead of 4; payload must land unshifted ----
	{ "TEST 06a: ONLY with TSETH",             MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x20000,         700,        700,    0xB0, F_AR | F_TS,   10,    MEM_WRITE, 0x20000,         700,   true,  REPLY_ACK,  SamePsn(),   11, REL_NONE, Any(),  DROP_SAME },
	{ "TEST 06b: FIRST with TSETH",            MRC_RDMA_WRITE_FIRST,  Epsn(0),     0x21000,         2*PKT+50,   PKT,    0xB1, F_AR | F_TS,   11,    MEM_WRITE, 0x21000,         PKT,   false, REPLY_ACK,  SamePsn(),   11, REL_NONE, Any(),  DROP_SAME },
	{ "TEST 06c: MIDDLE with TSETH",           MRC_RDMA_WRITE_MIDDLE, Epsn(0),     0x21000+PKT,     2*PKT+50,   PKT,    0xB2, F_AR | F_TS,   11,    MEM_WRITE, 0x21000+PKT,     PKT,   false, REPLY_ACK,  SamePsn(),   11, REL_NONE, Any(),  DROP_SAME },
	{ "TEST 06d: LAST with TSETH",             MRC_RDMA_WRITE_LAST,   Epsn(0),     0x21000+2*PKT,   2*PKT+50,   50,     0xB3, F_AR | F_TS,   11,    MEM_WRITE, 0x21000+2*PKT,   50,    true,  REPLY_ACK,  SamePsn(),   12, REL_NONE, Any(),  DROP_SAME },
	// offset changes from packet to packet inside one message: 4 -> 8 -> 4
	{ "TEST 06e: FIRST without TSETH",         MRC_RDMA_WRITE_FIRST,  Epsn(0),     0x24000,         2*PKT+50,   PKT,    0xC1, F_AR,          12,    MEM_WRITE, 0x24000,         PKT,   false, REPLY_ACK,  SamePsn(),   12, REL_NONE, Any(),  DROP_SAME },
	{ "TEST 06f: MIDDLE with TSETH",           MRC_RDMA_WRITE_MIDDLE, Epsn(0),     0x24000+PKT,     2*PKT+50,   PKT,    0xC2, F_AR | F_TS,   12,    MEM_WRITE, 0x24000+PKT,     PKT,   false, REPLY_ACK,  SamePsn(),   12, REL_NONE, Any(),  DROP_SAME },
	{ "TEST 06g: LAST without TSETH",          MRC_RDMA_WRITE_LAST,   Epsn(0),     0x24000+2*PKT,   2*PKT+50,   50,     0xC3, F_AR,          12,    MEM_WRITE, 0x24000+2*PKT,   50,    true,  REPLY_ACK,  SamePsn(),   13, REL_NONE, Any(),  DROP_SAME },
 
	// ---- 07: rtx bit set -> must not disturb parsing (transport ignores it for now) ----
	{ "TEST 07: ONLY with rtx",                MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x30000,         200,        200,    0xD0, F_AR | F_RTX,  13,    MEM_WRITE, 0x30000,         200,   true,  REPLY_ACK,  SamePsn(),   14, REL_NONE, Any(),  DROP_SAME },
 
	// ---- 08: payload sizes around the 64-byte word boundary (512-bit bus) ----
	//   stripper input = offset + 16 + payload bytes.  Branch exercised in rx_strip_mrc_eth:
	//   08a  4+16+1  = 21  -> one word                       FIRST, single-word branch
	//   08b  4+16+44 = 64  -> exactly one full word          FIRST, single-word branch
	//   08c  4+16+45 = 65  -> 2nd word has 1 byte            BODY, no remainder (output 61 B)
	//   08d  4+16+48 = 68  -> 2nd word has 4 bytes           BODY, no remainder (output exactly 64 B)
	//   08e  4+16+49 = 69  -> 2nd word has 5 bytes           BODY + REMAINDER (1 byte)
	//   08f  8+16+40 = 64  -> exactly one full word          FIRST, single-word branch, offset 8
	//   08g  8+16+48 = 72  -> 2nd word has 8 bytes           BODY, no remainder (output exactly 64 B)
	//   08h  8+16+49 = 73  -> 2nd word has 9 bytes           BODY + REMAINDER (1 byte)
	{ "TEST 08a: payload 1",                   MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x40000,         1,          1,      0x10, F_AR,          14,    MEM_WRITE, 0x40000,         1,     true,  REPLY_ACK,  SamePsn(),   15, REL_NONE, Any(),  DROP_SAME },
	{ "TEST 08b: payload 44",                  MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x40100,         44,         44,     0x20, F_AR,          15,    MEM_WRITE, 0x40100,         44,    true,  REPLY_ACK,  SamePsn(),   16, REL_NONE, Any(),  DROP_SAME },
	{ "TEST 08c: payload 45",                  MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x40200,         45,         45,     0x30, F_AR,          16,    MEM_WRITE, 0x40200,         45,    true,  REPLY_ACK,  SamePsn(),   17, REL_NONE, Any(),  DROP_SAME },
	{ "TEST 08d: payload 48",                  MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x40300,         48,         48,     0x40, F_AR,          17,    MEM_WRITE, 0x40300,         48,    true,  REPLY_ACK,  SamePsn(),   18, REL_NONE, Any(),  DROP_SAME },
	{ "TEST 08e: payload 49",                  MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x40400,         49,         49,     0x50, F_AR,          18,    MEM_WRITE, 0x40400,         49,    true,  REPLY_ACK,  SamePsn(),   19, REL_NONE, Any(),  DROP_SAME },
	{ "TEST 08f: payload 40 + TSETH",          MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x40500,         40,         40,     0x60, F_AR | F_TS,   19,    MEM_WRITE, 0x40500,         40,    true,  REPLY_ACK,  SamePsn(),   20, REL_NONE, Any(),  DROP_SAME },
	{ "TEST 08g: payload 48 + TSETH",          MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x40600,         48,         48,     0x70, F_AR | F_TS,   20,    MEM_WRITE, 0x40600,         48,    true,  REPLY_ACK,  SamePsn(),   21, REL_NONE, Any(),  DROP_SAME },
	{ "TEST 08h: payload 49 + TSETH",          MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x40700,         49,         49,     0x80, F_AR | F_TS,   21,    MEM_WRITE, 0x40700,         49,    true,  REPLY_ACK,  SamePsn(),   22, REL_NONE, Any(),  DROP_SAME },
 
	// ---- 09: RC "NAK once" behaviour (your original 07x group) ----
	{ "TEST 09a: ONLY ahead of epsn",          MRC_RDMA_WRITE_ONLY,   Epsn(150),   0x2000,          1024,       1024,   0xEE, F_AR,          0,     MEM_NONE,  0,               0,     false, REPLY_NAK,  LastPsn(1),  22, REL_NONE, Any(),  DROP_INC  },
	{ "TEST 09b: ONLY ahead of epsn",          MRC_RDMA_WRITE_ONLY,   Epsn(150),   0x2000,          1024,       1024,   0xEE, F_AR,          0,     MEM_NONE,  0,               0,     false, REPLY_NONE, Any(),       22, REL_NONE, Any(),  DROP_INC  },
	{ "TEST 09c: ONLY ahead of epsn",          MRC_RDMA_WRITE_ONLY,   Epsn(150),   0x2000,          1024,       1024,   0xEE, F_AR,          0,     MEM_NONE,  0,               0,     false, REPLY_NONE, Any(),       22, REL_NONE, Any(),  DROP_INC  },
	{ "TEST 09d: ONLY in order (resets NAK)",  MRC_RDMA_WRITE_ONLY,   Epsn(0),     0x2000,          1024,       1024,   0x11, F_AR,          22,    MEM_WRITE, 0x2000,          1024,  true,  REPLY_ACK,  SamePsn(),   23, REL_NONE, Any(),  DROP_SAME },
	{ "TEST 09e: ONLY ahead of epsn",          MRC_RDMA_WRITE_ONLY,   Epsn(150),   0x2000,          1024,       1024,   0xEE, F_AR,          0,     MEM_NONE,  0,               0,     false, REPLY_NAK,  LastPsn(1),  23, REL_NONE, Any(),  DROP_INC  },
	{ "TEST 09f: ONLY ahead of epsn",          MRC_RDMA_WRITE_ONLY,   Epsn(150),   0x2000,          1024,       1024,   0xEE, F_AR,          0,     MEM_NONE,  0,               0,     false, REPLY_NONE, Any(),       23, REL_NONE, Any(),  DROP_INC  },
	}
};
 
// =================================================================================================
// 24-bit PSN wrap-around, in order, across a multi-packet MRC write (0xFFFFFE, 0xFFFFFF, 0x000000)
// =================================================================================================
static const Scenario SCENARIO_MRC_PARSE_WRAP = {
	"MRC parse, PSN wrap",
	0xFFFFFE, -1,
	{
	// name                                    opcode                 psn          vaddr            dmaLen      payload fill  flags          mmsn | mem        memAddr          memLen last  | reply       ackPsn       msn | rel       cackPsn | drop
	{ "WRAP a: FIRST at 0xFFFFFE",             MRC_RDMA_WRITE_FIRST,  Epsn(0),     0x1000,          2*PKT+10,   PKT,    0x21, F_AR,          0,     MEM_WRITE, 0x1000,          PKT,   false, REPLY_ACK,  SamePsn(),   0,  REL_NONE, Any(),  DROP_SAME },
	{ "WRAP b: MIDDLE at 0xFFFFFF",            MRC_RDMA_WRITE_MIDDLE, Epsn(0),     0x1000+PKT,      2*PKT+10,   PKT,    0x22, F_AR,          0,     MEM_WRITE, 0x1000+PKT,      PKT,   false, REPLY_ACK,  SamePsn(),   0,  REL_NONE, Any(),  DROP_SAME },
	{ "WRAP c: LAST at 0x000000",              MRC_RDMA_WRITE_LAST,   Epsn(0),     0x1000+2*PKT,    2*PKT+10,   10,     0x23, F_AR,          0,     MEM_WRITE, 0x1000+2*PKT,    10,    true,  REPLY_ACK,  SamePsn(),   1,  REL_NONE, Any(),  DROP_SAME },
	}
};
 
static const std::vector<const Scenario*> SCENARIOS = {
	&SCENARIO_MRC_PARSE,
	&SCENARIO_MRC_PARSE_WRAP,
};
 
static const Scenario& SCENARIO = SCENARIO_MRC_PARSE;