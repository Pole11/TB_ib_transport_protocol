#pragma once
/*
 * MRC control packets for the testbench: [BTH][xETH], no payload.
 *
 * The header bytes are written field by field from the spec tables (Tables 7-12 .. 7-22), so they are
 * an independent reference for the getters in mrc_headers.hpp.
 *
 * Use in tests_config.hpp, one row per packet:
 *     Ctrl("name", Probe(0x0001), BACK_TO_BACK),
 *     Ctrl("name", Sack(LOCAL_TX_PSN)),
 *     Ctrl("name", Nack(NACK_TRIMMED, 0x000105), 10),
 *     Ctrl("name", Truncated(Sack(LOCAL_TX_PSN), 12)),
 * The key fields are parameters, everything else gets a realistic default (QPNs from tb_config.hpp).
 *
 * In this testbench the module is the RESPONDER of the remote requester's writes. On a real link it
 * would receive PROBE requests and endpoint operations from that peer; SACK/NACK only reach it as
 * the requestor of its own writes. Any of them can be sent here, to exercise the parsing.
 */

#include <cstdint>
#include <string>
#include <vector>

#include "ib_transport_protocol/ib_transport_protocol.hpp"
#include "ib_transport_protocol/mrc_headers.hpp"
#include "tb_config.hpp"
#include "utils.hpp"

// =================================================================================================
// Field values
// =================================================================================================

struct SackFields                       // SETH (Table 7-14) + CC_STATE (Table 7-12)
{
	uint8_t  m = 0;           bool     pr = false;     int16_t  ackPsnOffset = 0;
	uint32_t entropy = 0;     uint16_t spdcid = 0;     uint16_t dpdcid = 0;
	uint32_t cackPsn = 0;     uint8_t  ccType = 0;     uint8_t  ccFl = 0;
	uint8_t  mpr = 0;         int16_t  sackOffset = 0; uint64_t sackBitmap = 0;
	uint16_t ccTxTimestamp = 0; uint16_t oooCount = 0; bool restoreCwnd = false;
	uint8_t  rcvCwndPen = 0;  uint32_t rcvdBytes = 0;
};
struct NackFields                       // NETH (Table 7-16)
{
	uint8_t  nackReason = 0;  uint8_t  vendorInfo = 0; uint32_t entropy = 0;
	uint16_t spdcid = 0;      uint16_t dpdcid = 0;     uint32_t nackPsn = 0;
	uint8_t  ccType = 0;      uint8_t  ccFl = 0;       uint16_t txTimestamp = 0;
};
struct ProbeFields                      // PETH (Table 7-18)
{
	uint8_t  vendorInfo = 0;  uint16_t probeId = 0;    uint16_t spdcid = 0;
	uint16_t dpdcid = 0;      uint16_t txTimestamp = 0; bool tsr = false; uint8_t ftype = 0;
};
struct EpReqFields                      // ERTH (Table 7-20)
{
	uint8_t  op = 0;          uint8_t  vendorInfo = 0; uint32_t portStatusMask = 0;
	uint16_t txTimestamp = 0; bool     tsr = false;    uint8_t  ftype = 0;
};
struct EpRspFields                      // EETH (Table 7-22)
{
	uint8_t  op = 0;          uint16_t txTimestamp = 0;
};

// =================================================================================================
// Header builders (wire order, big-endian)
// =================================================================================================

// The first 16 bits of every control header: type[15:12]=0, nh[11:7]=0, opcode-specific low bits
static std::vector<uint8_t> buildSethCc(const SackFields& f)
{
	std::vector<uint8_t> b;
	put16(b, ((f.m & 0x3) << 5) | ((f.pr ? 1 : 0) << 1)); // [0..1]   type | nh | m | r r r | pr | r
	put16(b, (uint16_t) f.ackPsnOffset);                   // [2..3]   ack_psn_offset / probe_id
	put32(b, f.entropy);                                   // [4..7]
	put16(b, f.spdcid);                                    // [8..9]
	put16(b, f.dpdcid);                                    // [10..11]
	put8 (b, 0x00);                                        // [12]     res
	put24(b, f.cackPsn);                                   // [13..15]
	put8 (b, ((f.ccType & 0xF) << 4) | (f.ccFl & 0xF));    // [16]
	put8 (b, f.mpr);                                       // [17]
	put16(b, (uint16_t) f.sackOffset);                     // [18..19]
	put64(b, f.sackBitmap);                                // [20..27]
	put16(b, f.ccTxTimestamp);                             // [28..29] CC_STATE
	put16(b, f.oooCount & 0x7FFF);                         // [30..31] r | ooo_count
	put8 (b, ((f.restoreCwnd ? 1 : 0) << 7) | (f.rcvCwndPen & 0x7F)); // [32] r_c | rcv_cwnd_pen
	put24(b, f.rcvdBytes);                                 // [33..35]
	return b;
}

static std::vector<uint8_t> buildNeth(const NackFields& f)
{
	std::vector<uint8_t> b;
	put16(b, 0x0000);                                      // [0..1]   type | nh | reserved
	put8 (b, f.nackReason);                                // [2]
	put8 (b, f.vendorInfo);                                // [3]
	put32(b, f.entropy);                                   // [4..7]
	put16(b, f.spdcid);                                    // [8..9]
	put16(b, f.dpdcid);                                    // [10..11]
	put8 (b, 0x00);                                        // [12]     res
	put24(b, f.nackPsn);                                   // [13..15]
	put8 (b, ((f.ccType & 0xF) << 4) | (f.ccFl & 0xF));    // [16]
	put8 (b, 0x00);                                        // [17]     reserved
	put16(b, f.txTimestamp);                               // [18..19]
	return b;
}

static std::vector<uint8_t> buildPeth(const ProbeFields& f)
{
	std::vector<uint8_t> b;
	put16(b, 0x0000);                                      // [0..1]   type | nh | reserved
	put8 (b, 0x00);                                        // [2]      reserved
	put8 (b, f.vendorInfo);                                // [3]
	put16(b, f.probeId);                                   // [4..5]
	put16(b, 0x0000);                                      // [6..7]   reserved
	put16(b, f.spdcid);                                    // [8..9]
	put16(b, f.dpdcid);                                    // [10..11]
	put16(b, f.txTimestamp);                               // [12..13]
	put16(b, ((f.tsr ? 1 : 0) << 15) | (f.ftype & 0xF));   // [14..15] tsr | reserved | ftype
	return b;
}

static std::vector<uint8_t> buildErth(const EpReqFields& f)
{
	std::vector<uint8_t> b;
	put16(b, f.op & 0x3);                                  // [0..1]   type | nh | reserved | op
	put8 (b, 0x00);                                        // [2]      reserved
	put8 (b, f.vendorInfo);                                // [3]
	put32(b, f.portStatusMask);                            // [4..7]
	put32(b, 0x00000000);                                  // [8..11]  reserved
	put16(b, f.txTimestamp);                               // [12..13]
	put16(b, ((f.tsr ? 1 : 0) << 15) | (f.ftype & 0xF));   // [14..15] tsr | reserved | ftype
	return b;
}

static std::vector<uint8_t> buildEeth(const EpRspFields& f)
{
	std::vector<uint8_t> b;
	put16(b, (f.op & 0x3) << 2);                           // [0..1]   type | nh | r | op | r
	put16(b, 0x0000);                                      // [2..3]   reserved
	for (int i = 0; i < 6; i++) put32(b, 0x00000000);      // [4..27]  reserved
	put16(b, f.txTimestamp);                               // [28..29]
	put16(b, 0x0000);                                      // [30..31] reserved
	put32(b, 0x00000000);                                  // [32..35] reserved
	return b;
}

// =================================================================================================
// Control packet
// =================================================================================================

struct CtrlPacket
{
	std::string          name;
	mrcOpCode            opcode;
	uint32_t             dqp;        // BTH.dqp (meaning per Table 7-6)
	uint32_t             psn;        // BTH.psn (meaning per Table 7-6)
	std::vector<uint8_t> hdr;        // extension header bytes, right after the BTH
	std::string          expectLog;  // what rx_ctrl_fsm prints when it receives this packet
};

static std::vector<uint8_t> buildCtrlPacket(const CtrlPacket& c)
{
	std::vector<uint8_t> b;
	put8 (b, c.opcode);        // [0]     opcode
	put8 (b, 0x00);            // [1]     m | se | pad | tver
	put16(b, 0xFFFF);          // [2..3]  p_key
	put8 (b, 0x00);            // [4]     var-res
	put24(b, c.dqp);           // [5..7]  dqp
	put8 (b, 0x00);            // [8]     a | R | rtx | tsh | inv-res
	put24(b, c.psn);           // [9..11] psn
	b.insert(b.end(), c.hdr.begin(), c.hdr.end());
	return b;
}

static CtrlPacket sackPacket(const SackFields& f)
{ return { "SACK", MRC_SACK, LOCAL_QPN, f.cackPsn, buildSethCc(f), "]: SACK qp" }; }
static CtrlPacket nackPacket(const NackFields& f)
{ return { "NACK", MRC_NACK, LOCAL_QPN, f.nackPsn, buildNeth(f), "]: NACK qp" }; }
static CtrlPacket probePacket(const ProbeFields& f)
{ return { "PROBE", MRC_PROBE_REQ, LOCAL_QPN, 0, buildPeth(f), "]: PROBE qp" }; }
static CtrlPacket epReqPacket(const EpReqFields& f, uint32_t id)
{ return { "EP REQ", MRC_EP_REQ, MRC_EV_PROBE_DQP.to_uint(), id & 0xFFFF, buildErth(f), "]: EP REQ" }; }
static CtrlPacket epRspPacket(const EpRspFields& f, uint32_t id)
{ return { "EP RSP", MRC_EP_RSP, MRC_EV_PROBE_DQP.to_uint(), id & 0xFFFF, buildEeth(f), "]: EP RSP" }; }

// =================================================================================================
// Helpers for scenario rows
// =================================================================================================

// SACK to the module as REQUESTOR (Table 7-6): dqp = our QP, BTH.psn = cack_psn
static CtrlPacket Sack(uint32_t cackPsn, int16_t ackPsnOffset = 0, uint64_t sackBitmap = 0, bool probeResponse = false)
{
	SackFields f;
	f.cackPsn = cackPsn & 0xFFFFFF;  f.ackPsnOffset = ackPsnOffset;  f.sackBitmap = sackBitmap;
	f.pr = probeResponse;            f.entropy = 0xE0000000 | f.cackPsn;
	f.spdcid = LOCAL_QPN;            f.dpdcid = REMOTE_QPN;
	return sackPacket(f);
}

// NACK to the module as REQUESTOR: BTH.psn = psn of the packet that triggered it
static CtrlPacket Nack(uint8_t reason, uint32_t nackPsn)
{
	NackFields f;
	f.nackReason = reason;  f.nackPsn = nackPsn & 0xFFFFFF;  f.entropy = 0xE0000000 | f.nackPsn;
	f.spdcid = LOCAL_QPN;   f.dpdcid = REMOTE_QPN;           f.ccType = 0x2;
	return nackPacket(f);
}

// Reliability probe from the remote requester to the module as RESPONDER (does not consume a PSN)
static CtrlPacket Probe(uint16_t probeId, uint16_t txTimestamp = 0)
{
	ProbeFields f;
	f.probeId = probeId;  f.spdcid = REMOTE_QPN;  f.dpdcid = LOCAL_QPN;
	f.txTimestamp = txTimestamp;  f.ftype = txTimestamp ? (uint8_t) MRC_FTYPE_TS.to_uint() : 0;
	return probePacket(f);
}

// Endpoint request: dqp = 0x2, the requestor-private id travels in BTH.psn[15:0]
static CtrlPacket EpReq(uint8_t op, uint16_t id, uint32_t portStatusMask = 0)
{
	EpReqFields f;
	f.op = op;  f.portStatusMask = (op == EP_OP_PORT_STATUS) ? portStatusMask : 0;
	return epReqPacket(f, id);
}

// Endpoint response: reflects the id of the request
static CtrlPacket EpRsp(uint8_t op, uint16_t id, uint16_t txTimestamp = 0)
{
	EpRspFields f;
	f.op = op;  f.txTimestamp = txTimestamp;
	return epRspPacket(f, id);
}

// Malformed: keep only the first 'bytes' bytes of the extension header (0 = BTH only).
// The module must still consume it and report it as short.
static CtrlPacket Truncated(CtrlPacket p, size_t bytes)
{
	if (bytes < p.hdr.size()) p.hdr.resize(bytes);
	p.name      = p.name + " truncated";
	p.expectLog = "short control packet";
	return p;
}

// =================================================================================================
// Example values with every field distinct and non-zero (used by the parser unit checks)
// =================================================================================================
static SackFields exampleSack()
{
	SackFields f;
	f.m = SACK_M_SKIP;  f.pr = true;  f.ackPsnOffset = -2;
	f.entropy = 0xDEADBEEF;  f.spdcid = LOCAL_QPN;  f.dpdcid = REMOTE_QPN;
	f.cackPsn = 0x000010;  f.ccType = 0x0;  f.ccFl = 0xA;  f.mpr = 0x02;
	f.sackOffset = 5;  f.sackBitmap = 0x8000000000000001ULL;
	f.ccTxTimestamp = 0xBEEF;  f.oooCount = 0x1234;  f.restoreCwnd = true;
	f.rcvCwndPen = 0x15;  f.rcvdBytes = 0xABCDEF;
	return f;
}
static NackFields exampleNack()
{
	NackFields f;
	f.nackReason = NACK_PSN_OOR_WINDOW;  f.vendorInfo = 0x77;  f.entropy = 0xCAFEF00D;
	f.spdcid = LOCAL_QPN;  f.dpdcid = REMOTE_QPN;  f.nackPsn = 0x123456;
	f.ccType = 0x2;  f.ccFl = 0x3;  f.txTimestamp = 0x4321;
	return f;
}
static ProbeFields exampleProbe()
{
	ProbeFields f;
	f.vendorInfo = 0x5A;  f.probeId = 0x9999;  f.spdcid = REMOTE_QPN;  f.dpdcid = LOCAL_QPN;
	f.txTimestamp = 0x1111;  f.tsr = true;  f.ftype = MRC_FTYPE_TS.to_uint();
	return f;
}
static EpReqFields exampleEpReq()
{
	EpReqFields f;
	f.op = EP_OP_EV_PROBE;  f.vendorInfo = 0x42;  f.portStatusMask = 0x0000000F;
	f.txTimestamp = 0x2222;  f.tsr = false;  f.ftype = MRC_FTYPE_TS.to_uint();
	return f;
}
static EpRspFields exampleEpRsp()
{
	EpRspFields f;
	f.op = EP_OP_EV_PROBE;  f.txTimestamp = 0x3333;
	return f;
}