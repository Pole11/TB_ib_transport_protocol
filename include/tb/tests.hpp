#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

#include "ib_transport_protocol/ib_transport_protocol.hpp"
#include "tb_config.hpp"
#include "utils.hpp"
#include "pkt_builder.hpp"
#include "dut.hpp"

// What must happen to the payload
enum MemExpect  { MEM_WRITE,  MEM_NONE };                 // payload reaches memory / nothing is written
// What the module must send back
enum ReplyExpect{ REPLY_ACK,  REPLY_NAK, REPLY_NONE };    // ACK / NAK (seq. error) / no packet at all
// What must happen to regInvalidPsnDropCount
enum DropExpect { DROP_INC,   DROP_SAME, DROP_ANY };      // +1 / unchanged / not checked
// Flags inside BTH
enum : uint8_t { F_NONE = 0, F_AR = 0x1, F_RTX = 0x2, F_TS = 0x4 };  // request BTH flags
enum RelExpect { REL_NONE, REL_SACK, REL_NACK };                   // reliability-layer reply

struct Expected
{
	MemExpect   mem;
	// memory side (only checked if mem == MEM_WRITE)
	uint64_t    memAddr;
	uint32_t    memLen;
	bool        memLast;      // memCmd.lst: 1 = last chunk of the message
	// network side: the reply the module sends back (only checked if reply != REPLY_NONE)
	ReplyExpect reply;
	uint32_t    ackPsn;
	uint32_t    ackMsn;
	DropExpect  drop;
};

static void runWriteTest(Dut& dut, const std::string& name, const WritePacket& pkt, const Expected& exp)
{
	banner(name);

	// ---------------- (a) build and show the packet ----------------
	std::vector<uint8_t> ibBytes = buildMrcPacket(pkt);
	dumpBytes("TX packet", ibBytes);
	uint16_t udpLength = 8 + ibBytes.size() + 4;   // UDP header + IB packet + ICRC (see file header)

	std::cout << "[TB] Sending " << opcodeName(pkt.opcode) << std::endl;
	std::cout << "[TB]   DestQP=" << hex(LOCAL_QPN) << "  PSN=" << hex(pkt.psn) << ", " << pkt.psn;
	if (hasReth(pkt.opcode)) std::cout << "  RETH.vaddr=" << hex(pkt.vaddr) << "  RETH.len=" << std::dec << pkt.dmaLen;
	std::cout << std::dec << "  payload=" << pkt.payload.size() << " B" << std::endl;
	std::cout << "[TB]   ipUdpMeta.length (UDP length field) = " << udpLength << std::endl;
	std::cout << "[TB]   IB packet as it enters the module (" << ibBytes.size() << " B):" << std::endl;
	hexDump(ibBytes);

	// ---------------- (b) drive the RX interface ----------------
	dut.s_axis_rx_meta.write(ipUdpMeta(REMOTE_IP, REMOTE_UDP_PORT, RDMA_DEFAULT_PORT, udpLength));
	std::vector<net_axis<DATA_WIDTH> > beats = bytesToBeats(ibBytes);
	for (size_t i = 0; i < beats.size(); i++) dut.s_axis_rx_data.write(beats[i]);
	std::cout << "[TB]   pushed " << beats.size() << " beat(s) into s_axis_rx_data" << std::endl;

	uint32_t dropsBefore = dut.regInvalidPsnDropCount.to_uint();

	// ---------------- (c) run the module ----------------
	std::cout << "[TB] Running " << CYCLES_PER_TEST << " cycles... (module prints follow)" << std::endl;
	dut.run(CYCLES_PER_TEST);

	// ---------------- (d) collect the outputs ----------------
	std::cout << std::endl << "[TB] ---- Outputs ----" << std::endl;

	std::vector<memCmd> cmds;
	while (!dut.m_axis_mem_write_cmd.empty()) cmds.push_back(dut.m_axis_mem_write_cmd.read());
	for (size_t i = 0; i < cmds.size(); i++)
		std::cout << "[TB] m_axis_mem_write_cmd: op=" << opcodeName(cmds[i].op_code)
		          << " qpn=" << hex(cmds[i].qpn.to_uint()) << " addr=" << hex(cmds[i].addr.to_uint64())
		          << " len=" << std::dec << cmds[i].len.to_uint() << " lst=" << cmds[i].lst.to_uint()
		          << " host=" << cmds[i].host.to_uint() << std::endl;

	std::vector<uint8_t> memBytes;
	int  memBeats = 0;
	bool memLastSeen = false;
	while (!dut.m_axis_mem_write_data.empty())
	{
		net_axis<DATA_WIDTH> w = dut.m_axis_mem_write_data.read();
		appendBeatBytes(w, memBytes);
		memBeats++;
		memLastSeen = w.last;
	}
	std::cout << "[TB] m_axis_mem_write_data: " << memBeats << " beat(s), " << memBytes.size() << " B" << std::endl;
	if (!memBytes.empty()) hexDump(memBytes);

	std::vector<ipUdpMeta> txMetas;
	while (!dut.m_axis_tx_meta.empty()) txMetas.push_back(dut.m_axis_tx_meta.read());

	std::vector<std::vector<uint8_t> > txPackets(1);
	while (!dut.m_axis_tx_data.empty())
	{
		net_axis<DATA_WIDTH> w = dut.m_axis_tx_data.read();
		appendBeatBytes(w, txPackets.back());
		if (w.last) txPackets.push_back(std::vector<uint8_t>());
	}
	txPackets.pop_back();   // remove the empty "next packet" slot
	for (size_t i = 0; i < txMetas.size(); i++)
		std::cout << "[TB] m_axis_tx_meta: dst ip=" << hex(txMetas[i].their_address.to_uint64())
		          << " length=" << std::dec << txMetas[i].length.to_uint() << std::endl;
	for (size_t i = 0; i < txPackets.size(); i++)
	{
		std::cout << "[TB] m_axis_tx_data packet " << i << " (" << txPackets[i].size() << " B):" << std::endl;
		hexDump(txPackets[i]);
	}

	// ---------------- (e) checks ----------------
	std::cout << std::endl << "[TB] ---- Checks ----" << std::endl;

	// Memory side
	if (exp.mem == MEM_WRITE)
	{
		checkCount("number of memory write commands", cmds.size(), 1);
		if (cmds.size() == 1)
		{
			checkEq("memCmd.op_code", cmds[0].op_code, pkt.opcode);
			checkEq("memCmd.qpn    ", cmds[0].qpn.to_uint(), LOCAL_QPN);
			checkEq("memCmd.addr   ", cmds[0].addr.to_uint64(), exp.memAddr);
			checkCount("memCmd.len    ", cmds[0].len.to_uint(), exp.memLen);
			checkEq("memCmd.lst    ", cmds[0].lst.to_uint(), exp.memLast);
		}
		checkCount("bytes written to memory", memBytes.size(), pkt.payload.size());
		check(memBytes == pkt.payload, "memory data is identical to the payload that was sent");
		check(memLastSeen, "last memory data beat has TLAST set");
	}
	else
	{
		checkCount("number of memory write commands (nothing must be written)", cmds.size(), 0);
		checkCount("bytes written to memory (nothing must be written)", memBytes.size(), 0);
	}

	// Drop counter
	uint32_t dropsNow = dut.regInvalidPsnDropCount.to_uint();
	if (exp.drop == DROP_INC)       checkCount("invalid-PSN drop counter incremented", dropsNow, dropsBefore + 1);
	else if (exp.drop == DROP_SAME) checkCount("invalid-PSN drop counter unchanged", dropsNow, dropsBefore);
	else std::cout << "[TB] invalid-PSN drop counter " << dropsBefore << " -> " << dropsNow << " (not checked)" << std::endl;

	// Network side: ACK/NAK = [BTH 12B][AETH 4B], or nothing
	const bool nak = exp.reply == REPLY_NAK;
	checkCount("number of packets sent back (ACK/NAK)", txPackets.size(), exp.reply == REPLY_NONE ? 0 : 1);
	checkCount("number of tx meta entries", txMetas.size(), exp.reply == REPLY_NONE ? 0 : 1);
	if (exp.reply != REPLY_NONE && txMetas.size() == 1)
	{
		checkEq("tx meta: destination IP", txMetas[0].their_address.to_uint64(), REMOTE_IP);
		checkCount("tx meta: UDP length (BTH+AETH+ICRC)", txMetas[0].length.to_uint(), 12 + 4 + 4);
	}
	if (exp.reply != REPLY_NONE && txPackets.size() == 1 && txPackets[0].size() >= 16)
	{
		const std::vector<uint8_t>& a = txPackets[0];
		uint32_t syndrome = a[12];
		std::cout << "[TB] parsed reply: " << opcodeName(a[0]) << "  DestQP=" << hex(get24(a, 5))
		          << "  PSN=" << hex(get24(a, 9)) << "  AETH.syndrome=" << hex(syndrome, 2)
		          << (syndrome == 0x1F ? " (ACK)" : syndrome == 0x60 ? " (NAK: PSN sequence error)" : "")
		          << "  AETH.MSN=" << hex(get24(a, 13)) << std::endl;

		checkCount("reply length in bytes", a.size(), 16);
		checkEq("reply BTH.opcode (MRC_ACK)", a[0], MRC_ACK);
		checkEq("reply BTH.DestQP (remote QPN)", get24(a, 5), REMOTE_QPN);
		checkEq("reply BTH.PSN", get24(a, 9), exp.ackPsn);
		checkEq(nak ? "reply AETH.syndrome (NAK seq err)" : "reply AETH.syndrome (ACK)", syndrome, nak ? 0x60 : 0x1F);
		checkEq("reply AETH.MSN", get24(a, 13), exp.ackMsn);
	}
	else if (exp.reply != REPLY_NONE && txPackets.size() == 1)
	{
		check(false, "reply is shorter than BTH+AETH (16 B)");
	}

	// Things that must NOT happen for an incoming WRITE
	check(dut.m_axis_mem_read_cmd.empty(), "no memory READ command issued");
	check(dut.m_axis_rx_ack_meta.empty(),  "no ackMeta to the host (only used for our own requests)");
	check(dut.s_axis_rx_data.empty() && dut.s_axis_rx_meta.empty(), "module consumed the whole input");
}
