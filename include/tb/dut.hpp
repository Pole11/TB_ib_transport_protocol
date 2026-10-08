#pragma once

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

#include "axi_utils.hpp"
#include "ib_transport_protocol/ib_transport_protocol.hpp"
#include "dut_top.hpp"
#include "tb_config.hpp"
#include "utils.hpp"

struct Dut
{
	// RX from network (what we drive)
	stream<ipUdpMeta>             s_axis_rx_meta;
	stream<net_axis<DATA_WIDTH> > s_axis_rx_data;
	// TX to network (ACKs come out here)
	stream<ipUdpMeta>             m_axis_tx_meta;
	stream<net_axis<DATA_WIDTH> > m_axis_tx_data;
	// Local work requests (unused: we only test the receive side)
	stream<txMeta>                s_axis_sq_meta;
	stream<ackMeta>               m_axis_rx_ack_meta;
	// Host memory interface
	stream<memCmd>                m_axis_mem_write_cmd;
	stream<memCmd>                m_axis_mem_read_cmd;
	stream<net_axis<DATA_WIDTH> > m_axis_mem_write_data;
	stream<net_axis<DATA_WIDTH> > s_axis_mem_read_data;
	// QP setup
	stream<qpContext>             s_axis_qp_interface;
	stream<ifConnReq>             s_axis_qp_conn_interface;
	// Debug streams (exist because ib_transport_protocol.hpp does '#define DBG_IBV')
#ifdef DBG_IBV
	stream<psnPkg>                m_axis_dbg_0, m_axis_dbg_1, m_axis_dbg_2;
#endif
	// Status registers
	ap_uint<32> regInvalidPsnDropCount = 0;
	ap_uint<32> regRetransCount        = 0;
	ap_uint<32> regIbvCountRx          = 0;
	ap_uint<32> regIbvCountTx          = 0;

	// One call of the dataflow region == every internal process advances by one step.
	void tick();

	void run(int cycles) { for (int i = 0; i < cycles; i++) tick(); }

#ifdef DBG_IBV
	void drainDebug(stream<psnPkg>& s, int id)
	{
		psnPkg p(MRC_ACK, 0, 0, 0);   // psnPkg has no default constructor
		while (!s.empty())
		{
			s.read(p);
			if (PRINT_DBG_STREAMS)
				std::cout << std::dec << "[cyc " << std::setw(4) << g_cycle << "] [DBG" << id << "] "
				          << opcodeName(p.opcode) << " val1=" << hex(p.val1.to_uint())
				          << " val2=" << hex(p.val2.to_uint()) << " ctl=" << p.ctl.to_uint() << std::endl;
		}
	}
#endif
};

// Defined out of class: C/RTL co-sim inserts a prototype before the function containing the DUT
// call, which must therefore be at namespace scope.
inline void Dut::tick()
{
	// Capture the module's own prints so we can tag them with the cycle number.
	std::stringstream captured;
	std::streambuf* original = std::cout.rdbuf(captured.rdbuf());

	ib_transport_protocol_top(
		s_axis_rx_meta, s_axis_rx_data,
		m_axis_tx_meta, m_axis_tx_data,
		s_axis_sq_meta,
		m_axis_rx_ack_meta,
		m_axis_mem_write_cmd, m_axis_mem_read_cmd,
		m_axis_mem_write_data, s_axis_mem_read_data,
		s_axis_qp_interface, s_axis_qp_conn_interface,
#ifdef DBG_IBV
		m_axis_dbg_0, m_axis_dbg_1, m_axis_dbg_2,
#endif
		regInvalidPsnDropCount, regRetransCount, regIbvCountRx, regIbvCountTx);

	std::cout.rdbuf(original);

	std::string line;
	while (std::getline(captured, line))
		std::cout << std::dec << "[cyc " << std::setw(4) << std::setfill(' ') << g_cycle << "] " << line << std::endl;

#ifdef DBG_IBV
	drainDebug(m_axis_dbg_0, 0);
	drainDebug(m_axis_dbg_1, 1);
	drainDebug(m_axis_dbg_2, 2);
#endif
	g_cycle++;
}

