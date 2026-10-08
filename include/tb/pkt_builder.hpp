#pragma once

#include <cstdint>
#include <vector>

#include "ib_transport_protocol/ib_transport_protocol.hpp"
#include "tb_config.hpp"
#include "utils.hpp"

struct WritePacket
{
	mrcOpCode             opcode;   // RC_RDMA_WRITE_ONLY / FIRST / MIDDLE / LAST
	uint32_t             psn;      // packet sequence number
	uint64_t             vaddr;    // RETH: remote virtual address  (FIRST / ONLY only)
	uint32_t             dmaLen;   // RETH: total message length     (FIRST / ONLY only)
	std::vector<uint8_t> payload;  // keep it a multiple of 4 bytes (RoCE pads payload to 4 B)

	// MRC additions
	bool     ackReq      = false; // BTH.a: ask the responder for an immediate SACK
	uint16_t msn         = 0;     // requestor message sequence number
	uint16_t rqmsn       = 0;     // only meaningful for Write-with-Immediate
	bool     rtx         = false; // retransmitted packet
	bool     hasTseth    = false; // include the timestamp header
	uint16_t txTimestamp = 0;
	uint32_t immData     = 0;     // only used for *_IMM opcodes
};

static bool hasReth(mrcOpCode op) { 
	// for RC
	// return op == RC_RDMA_WRITE_FIRST || op == RC_RDMA_WRITE_ONLY; 
	// for MRC
	return true;
}

static bool hasImmDt(uint8_t op) { return op == MRC_RDMA_WRITE_LAST_IMM || op == MRC_RDMA_WRITE_ONLY_IMM; }

// Returns [BTH][RETH?][payload] in wire order.
// static std::vector<uint8_t> buildIbPacket(const WritePacket& p)
// {
// 	std::vector<uint8_t> b;

// 	// ---- BTH, 12 bytes (IB spec vol.1, 9.2) ----
// 	put8 (b, p.opcode);        // [0]     OpCode
// 	put8 (b, 0x00);            // [1]     SE | M | PadCnt | TVer
// 	put16(b, 0xFFFF);          // [2..3]  Partition key (default)
// 	put8 (b, 0x00);            // [4]     FECN | BECN | reserved
// 	put24(b, LOCAL_QPN);       // [5..7]  Destination QP  = the module's QP
// 	put8 (b, 0x80);            // [8]     AckReq = 1 | reserved
// 	put24(b, p.psn);           // [9..11] PSN

// 	// ---- RETH, 16 bytes (only on FIRST / ONLY) ----
// 	if (hasReth(p.opcode))
// 	{
// 		put64(b, p.vaddr);     // Virtual address
// 		put32(b, RKEY);        // R_Key
// 		put32(b, p.dmaLen);    // DMA length (whole message)
// 	}

// 	// ---- Payload ----
// 	b.insert(b.end(), p.payload.begin(), p.payload.end());

// 	// NOTE: no ICRC here, the crc module in front of ib_transport_protocol already removed it.
// 	return b;
// }

static void dumpBytes(const char* tag, const std::vector<uint8_t>& b)
{
	std::cout << "---- " << tag << " (" << std::dec << b.size() << " bytes) ----\n";
	for (size_t i = 0; i < b.size(); i += 16)
	{
		std::cout << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
		for (size_t j = i; j < i + 16 && j < b.size(); ++j)
			std::cout << std::setw(2) << (int) b[j] << ' ';
		std::cout << '\n';
	}
	std::cout << std::dec << std::setfill(' ');
}

// Returns [BTH][METH][TSETH?][RETH][ImmDt?][payload] in wire order.
static std::vector<uint8_t> buildMrcPacket(const WritePacket& p)
{
	std::vector<uint8_t> b;

	// ---- BTH, 12 bytes (MRC spec Table 6-3) ----
	put8 (b, p.opcode);            // [0]     MRC opcode (0xC6..0xCB)
	put8 (b, 0x00);                // [1]     m | se | pad | tver=0  (all zero in MRC)
	put16(b, 0xFFFF);              // [2..3]  Partition key
	put8 (b, 0x00);                // [4]     var-res
	put24(b, LOCAL_QPN);           // [5..7]  Destination QP

	uint8_t flags = 0x00;
	if (p.ackReq)   flags |= 0x80; // bit 7: a   (AckReq)
	if (p.rtx)      flags |= 0x20; // bit 5: rtx (retransmission)
	if (p.hasTseth) flags |= 0x10; // bit 4: ts  (TSETH present)
	put8 (b, flags);               // [8]     a | R | rtx | ts | inv-res
	put24(b, p.psn);               // [9..11] PSN 

	// ---- METH, 4 bytes, on EVERY request packet (Table 6-8) ----
	put16(b, p.rqmsn);             // RQMSN
	put16(b, p.msn);               // MSN

	// ---- TSETH, 4 bytes, only when ts=1 (Table 6-6) ----
	if (p.hasTseth)
	{
		put16(b, p.txTimestamp);   // tx_timestamp
		put16(b, 0x0001);          // tsr=0 (128 ns) | reserved=0 | ftype=0x1
	}

	// ---- RETH, 16 bytes, on EVERY packet in MRC ----
	put64(b, p.vaddr);             // per-packet virtual address
	put32(b, RKEY);                // R_Key
	put32(b, p.dmaLen);            // DMA length (whole message)

	// ---- ImmDt, 4 bytes, only for LAST_IMM / ONLY_IMM ----
	if (hasImmDt(p.opcode))
		put32(b, p.immData);

	// ---- Payload ----
	b.insert(b.end(), p.payload.begin(), p.payload.end());

	// NOTE: still no ICRC, the crc module in front of ib_transport_protocol removes it.
	return b;
}

// Splits one RDMA write into MRC packets, with the per-packet VA the spec requires.
static std::vector<WritePacket> segmentMrcWrite(uint64_t baseVaddr,
                                                const std::vector<uint8_t>& data,
                                                uint32_t firstPsn,
                                                uint16_t msn,
                                                uint32_t pmtu,
                                                bool     withImm = false,
                                                uint32_t imm     = 0,
                                                uint16_t rqmsn   = 0)
{
	std::vector<WritePacket> pkts;
	const uint32_t total = data.size();
	const uint32_t nPkts = (total == 0) ? 1 : (total + pmtu - 1) / pmtu;

	for (uint32_t i = 0; i < nPkts; ++i)
	{
		const uint32_t off  = i * pmtu;
		const uint32_t len  = std::min<uint32_t>(pmtu, total - off);
		const bool     first = (i == 0);
		const bool     last  = (i == nPkts - 1);

		WritePacket p;
		if (first && last) p.opcode = withImm ? MRC_RDMA_WRITE_ONLY_IMM : MRC_RDMA_WRITE_ONLY;
		else if (first)    p.opcode = MRC_RDMA_WRITE_FIRST;
		else if (last)     p.opcode = withImm ? MRC_RDMA_WRITE_LAST_IMM : MRC_RDMA_WRITE_LAST;
		else               p.opcode = MRC_RDMA_WRITE_MIDDLE;

		p.psn     = (firstPsn + i) & 0xFFFFFF;  // PSN is 24 bits, wrap around
		p.vaddr   = baseVaddr + off;            // advances by PMTU each packet
		p.dmaLen  = total;                      // always the full message length
		p.payload.assign(data.begin() + off, data.begin() + off + len);
		p.msn     = msn;                        // same MSN for every packet of the message
		p.rqmsn   = withImm ? rqmsn : 0;        // undefined for plain writes, so send 0
		p.immData = imm;

		pkts.push_back(std::move(p));
	}
	return pkts;
}

static std::vector<uint8_t> makePayload(size_t len, uint8_t seed)
{
	std::vector<uint8_t> v(len);
	for (size_t i = 0; i < len; i++) v[i] = (uint8_t) (seed + i);
	return v;
}
