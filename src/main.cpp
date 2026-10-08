/*
 * test_rdma_write.cpp
 * -------------------------------------------------------------------------------------------------
 * Stand-alone testbench for ib_transport_protocol<> (RoCE v2 transport layer), RDMA WRITE only.
 *
 * WHAT THIS TESTBENCH DOES
 *   The testbench plays two roles around ONE instance of the module:
 *     - the REMOTE REQUESTER: it hand-builds RDMA WRITE packets and pushes them into the RX side
 *     - the LOCAL HOST MEMORY: it collects the memory-write commands + data the module produces
 *   and then it checks the ACK/NAK packet the module sends back on its TX side.
 *
 *                 +------------------------------------------------------------+
 *   s_axis_rx_meta|                                                            |m_axis_mem_write_cmd
 *   ------------->|                                                            |------------------->
 *   s_axis_rx_data|                ib_transport_protocol<512, 0>               |m_axis_mem_write_data
 *   ------------->|                                                            |------------------->
 *                 |   (state table, MSN table, conn table live inside)         |
 *   s_axis_qp_*   |                                                            |m_axis_tx_meta
 *   ------------->|                                                            |------------------->
 *                 |                                                            |m_axis_tx_data (ACK)
 *                 +------------------------------------------------------------+------------------->
 *
 * WHAT THE MODULE RECEIVES (important!)
 *   The module does NOT get a raw Ethernet/IP/UDP frame. In rocev2.cpp the chain is
 *       crc (strips ICRC) -> ipv4 (strips IP header) -> udp (strips UDP header) -> ib_transport_protocol
 *   So on its RX input it sees:
 *     s_axis_rx_meta : ipUdpMeta { their_address, their_port, my_port, length }
 *                      'length' is the UDP LENGTH FIELD = 8 (UDP hdr) + IB headers + payload + 4 (ICRC)
 *     s_axis_rx_data : 512-bit beats that start directly with the BTH:
 *                      [BTH 12B][RETH 16B][payload]        (no ICRC: already removed upstream)
 *   The "verb" is simply the BTH opcode (e.g. 0x0A = RC_RDMA_WRITE_ONLY).
 *
 * BYTE ORDER
 *   Byte 0 of the packet (first on the wire) sits in data(7,0) of the first beat, byte 1 in data(15,8),
 *   ... exactly like the real CMAC/AXI stream. Multi-byte header fields are big-endian on the wire;
 *   we build the packet as a plain byte array in wire order, so what you read here is what a real
 *   NIC would send.
 *
 * HOW TO READ THE OUTPUT
 *   - "[cyc   N]" lines are the module's own std::cout prints, tagged with the call ("cycle") in which
 *     they happened. Cycles with no prints are not shown.
 *   - "[TB]" lines are testbench prints.
 *   - "[PASS]" / "[FAIL]" lines are checks. main() returns non-zero if anything failed.
 * -------------------------------------------------------------------------------------------------
 */

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <set>

#include "axi_utils.hpp"
#include "ib_transport_protocol/ib_transport_protocol.hpp"
#include "rocev2_config.hpp"
#include "tb_config.hpp"
#include "utils.hpp"
#include "pkt_builder.hpp"
#include "dut.hpp"
#include "tests.hpp"
#include "tests_config.hpp"

using namespace hls;

static uint32_t resolvePsn(const PsnRef& r, uint32_t epsn, uint32_t last_psn, uint32_t same)
{
	int64_t base = r.base == PsnRef::EPSN ? epsn : r.base == PsnRef::LAST ? last_psn : r.base == PsnRef::SAME ? same : r.base == PsnRef::REL ? FIRST_RX_PSN : 0;
	return (uint32_t)((base + r.offset) & 0xFFFFFF);
}

int main()
{
	Dut dut;
	const Scenario& sc = SCENARIO;   // edit include/config/tests_config.hpp to change the tests

	// ---------------------------------------------------------------------------------------------
	banner("STEP 0: configure the QP (no packets yet) -- scenario: " + std::string(sc.name));
	// qpContext(state, qp_num, remote_psn, local_psn, r_key, virtual_address)
	//   local_psn  -> resp_epsn    = PSN the module EXPECTS on incoming requests  <-- matters here
	//   remote_psn -> req_next_psn = PSN the module USES when it sends requests
	//   virtual_address must not be 0xDEADBEEF / 0xFEEDBEEF (magic values in qp_interface).
	dut.s_axis_qp_interface.write(qpContext(READY_RECV, LOCAL_QPN, LOCAL_TX_PSN, sc.initialEpsn, RKEY, 0));
	// Connection table: where to send ACKs for this QP (remote QPN, IP, UDP port)
	dut.s_axis_qp_conn_interface.write(ifConnReq(LOCAL_QPN, REMOTE_QPN, REMOTE_IP, REMOTE_UDP_PORT));
	std::cout << "[TB] QP " << hex(LOCAL_QPN) << " -> expects PSN " << hex(sc.initialEpsn)
	          << ", ACKs go to QP " << hex(REMOTE_QPN) << " @ " << hex(REMOTE_IP) << std::endl;
	dut.run(20);

	uint32_t epsn     = sc.initialEpsn & 0xFFFFFF;
	uint32_t last_psn = sc.initialLastPsn < 0 ? ((epsn - 1) & 0xFFFFFF) : (uint32_t)sc.initialLastPsn;

	for (size_t i = 0; i < sc.cases.size(); i++)
	{
		const TestCase& c = sc.cases[i];
		uint32_t psn = resolvePsn(c.psn, epsn, last_psn, 0);

		WritePacket p = { c.opcode, psn, c.vaddr, c.dmaLen, makePayload(c.payloadLen, c.payloadFill) };
		p.ackReq   = c.flags & F_AR;
		p.rtx      = c.flags & F_RTX;
		p.hasTseth = c.flags & F_TS;
		p.msn      = c.mmsn;
		Expected    e = { c.mem, c.memAddr, c.memLen, c.memLast, c.reply,
		                  resolvePsn(c.ackPsn, epsn, last_psn, psn), c.ackMsn, c.drop };

		std::cout << "[TB] state before: epsn=" << hex(epsn) << " last_psn=" << hex(last_psn) << std::endl;
		runWriteTest(dut, c.name, p, e);

		if (e.mem == MEM_WRITE) { 
			std::set<uint32_t> received;          // PSNs accepted at or above epsn
			if (c.mem == MEM_WRITE) {
				received.insert(psn);
				while (received.erase(epsn)) epsn = (epsn + 1) & 0xFFFFFF;
				last_psn = psn;
			}
		}
	}

	// ---------------------------------------------------------------------------------------------
	banner("SUMMARY");
	std::cout << std::dec;
	std::cout << "[TB] regIbvCountRx          = " << dut.regIbvCountRx.to_uint() << std::endl;
	std::cout << "[TB] regIbvCountTx          = " << dut.regIbvCountTx.to_uint() << std::endl;
	std::cout << "[TB] regInvalidPsnDropCount = " << dut.regInvalidPsnDropCount.to_uint() << std::endl;
	checkCount("packets received by the module", dut.regIbvCountRx.to_uint(), sc.cases.size());
	size_t expectedReplies = 0;
	for (size_t i = 0; i < sc.cases.size(); i++) if (sc.cases[i].reply != REPLY_NONE) expectedReplies++;
	checkCount("packets sent by the module (ACK/NAK)", dut.regIbvCountTx.to_uint(), expectedReplies);

	std::cout << std::endl << (g_failures == 0 ? "*** ALL CHECKS PASSED ***" : "*** SOME CHECKS FAILED ***")
	          << "  (" << std::dec << g_failures << " failure(s), " << g_cycle << " cycles simulated)" << std::endl;
	return g_failures == 0 ? 0 : 1;
}
