#pragma once
/*
 * MRC control headers: parser unit checks (no DUT)
 * -------------------------------------------------------------------------------------------------
 * Pushes each control header straight through rx_split_udp_meta + rx_split_ctrl + rx_process_ctrl
 * and checks EVERY getter against the field values the header was built from (ctrl_pkt_builder.hpp).
 *
 * Uses its own template instance (INSTID = TB_UNIT_INSTID), so it never shares static state with the
 * processes inside the DUT, and can run on any bus width, not only DATA_WIDTH.
 * The control packets through the real DUT are scenario rows instead (tests_config.hpp).
 * -------------------------------------------------------------------------------------------------
 */

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

#include "ib_transport_protocol/ib_transport_protocol.hpp"
#include "ib_transport_protocol/mrc_rx_ctrl.hpp"
#include "tb_config.hpp"
#include "utils.hpp"
#include "pkt_ctrl_builder.hpp"

static const int      TB_UNIT_INSTID = 99;        // template instance used by the unit checks only
static const uint32_t EV_PROBE_ID    = 0x00ABCD;  // requestor-private id carried in BTH.psn[15:0]

template <int W>
static std::vector<net_axis<W> > bytesToBeatsW(const std::vector<uint8_t>& bytes)
{
	std::vector<net_axis<W> > beats;
	const size_t B = W / 8;
	for (size_t off = 0; off < bytes.size(); off += B)
	{
		net_axis<W> w;
		w.data = 0;
		w.keep = 0;
		for (size_t i = 0; i < B && off + i < bytes.size(); i++)
		{
			w.data(8*i+7, 8*i) = bytes[off + i];
			w.keep[i] = 1;
		}
		w.last = (off + B >= bytes.size());
		beats.push_back(w);
	}
	if (beats.empty())   // what the BTH shifter emits for a BTH-only packet: one empty last beat
	{
		net_axis<W> w;
		w.data = 0;
		w.keep = 0;
		w.last = 1;
		beats.push_back(w);
	}
	return beats;
}

// One control header through the three RX processes, as they see it after the BTH shifter
template <int W>
static bool parseCtrlUnit(const CtrlPacket& c, mrcCtrlMeta<W>& m)
{
	static stream<net_axis<W> >    shifted, dataOut, ctrlOut;
	static stream<bool>            splitData, splitUdp;
	static stream<ipUdpMeta>       udpIn, udpData, udpCtrl;
	static stream<mrcCtrlBthMeta>  bthMeta;
	static stream<mrcCtrlMeta<W> > out;

	// what rx_process_ibh writes for a control packet
	splitData.write(true);
	splitUdp.write(true);
	bthMeta.write(mrcCtrlBthMeta(c.opcode, 0xFFFF, c.dqp, c.psn, false, false));
	udpIn.write(ipUdpMeta(REMOTE_IP, REMOTE_UDP_PORT, RDMA_DEFAULT_PORT, 8 + 12 + c.hdr.size() + 4));
	std::vector<net_axis<W> > beats = bytesToBeatsW<W>(c.hdr);
	for (size_t i = 0; i < beats.size(); i++) shifted.write(beats[i]);

	for (int cycle = 0; cycle < 64; cycle++)
	{
		rx_split_udp_meta<TB_UNIT_INSTID>(udpIn, splitUdp, udpData, udpCtrl);
		rx_split_ctrl<W, TB_UNIT_INSTID>(shifted, splitData, dataOut, ctrlOut);
		rx_process_ctrl<W, TB_UNIT_INSTID>(ctrlOut, bthMeta, udpCtrl, out);
	}

	check(dataOut.empty() && udpData.empty(), c.name + ": nothing went to the data path");
	check(shifted.empty() && ctrlOut.empty(), c.name + ": all header words consumed");
	checkCount(c.name + ": parsed metas", out.size(), 1);
	bool ok = (out.size() == 1);
	while (!out.empty()) out.read(m);
	return ok;
}

template <int W>
static void runCtrlParseUnitTests()
{
	banner("MRC CONTROL HEADERS: parser unit checks, " + std::to_string(W) + "-bit bus");
	mrcCtrlMeta<W> m;

	{
		SackFields f = exampleSack();
		CtrlPacket c = sackPacket(f);
		std::cout << "[TB] " << c.name << std::endl;
		if (parseCtrlUnit<W>(c, m))
		{
			SackExHeader<W> h = m.getSackHeader();
			checkEq("SETH length ok      ", m.lengthOk, 1);
			checkEq("BTH.dqp             ", (uint64_t) m.dest_qp, c.dqp);
			checkEq("SETH.m              ", (uint64_t) h.getMark(), f.m);
			checkEq("SETH.pr             ", h.isProbeResponse(), f.pr);
			checkEq("SETH.ack_psn_offset ", (uint64_t) h.getAckPsnOffset().to_int64(), (uint64_t) (int64_t) f.ackPsnOffset);
			checkEq("SETH ack_psn        ", (uint64_t) h.getAckPsn(), (f.cackPsn + f.ackPsnOffset) & 0xFFFFFF);
			checkEq("SETH.entropy        ", (uint64_t) h.getEntropy(), f.entropy);
			checkEq("SETH.spdcid         ", (uint64_t) h.getSpdcid(), f.spdcid);
			checkEq("SETH.dpdcid         ", (uint64_t) h.getDpdcid(), f.dpdcid);
			checkEq("SETH.cack_psn       ", (uint64_t) h.getCackPsn(), f.cackPsn);
			checkEq("SETH.cc_type        ", (uint64_t) h.getCcType(), f.ccType);
			checkEq("SETH.cc_fl          ", (uint64_t) h.getCcFlags(), f.ccFl);
			checkEq("SETH.mpr            ", (uint64_t) h.getMpr(), f.mpr);
			checkEq("SETH sack base psn  ", (uint64_t) h.getSackBasePsn(), (f.cackPsn + f.sackOffset) & 0xFFFFFF);
			checkEq("SETH.sack_bitmap    ", (uint64_t) h.getSackBitmap(), f.sackBitmap);
			checkEq("CC.tx_timestamp     ", (uint64_t) h.getCcTxTimestamp(), f.ccTxTimestamp);
			checkEq("CC.ooo_count        ", (uint64_t) h.getOooCount(), f.oooCount);
			checkEq("CC.restore_cwnd     ", h.getRestoreCwnd(), f.restoreCwnd);
			checkEq("CC.rcv_cwnd_pen     ", (uint64_t) h.getRcvCwndPenalty(), f.rcvCwndPen);
			checkEq("CC.rcvd_bytes       ", (uint64_t) h.getRcvdBytes(), f.rcvdBytes);
			checkEq("UDP source address  ", (uint64_t) m.srcAddress, REMOTE_IP);
		}
	}
	{
		NackFields f = exampleNack();
		CtrlPacket c = nackPacket(f);
		std::cout << "[TB] " << c.name << std::endl;
		if (parseCtrlUnit<W>(c, m))
		{
			NackExHeader<W> h = m.getNackHeader();
			checkEq("NETH length ok      ", m.lengthOk, 1);
			checkEq("NETH.nack_reason    ", (uint64_t) h.getNackReason(), f.nackReason);
			checkEq("NETH.vendor_info    ", (uint64_t) h.getVendorInfo(), f.vendorInfo);
			checkEq("NETH.entropy        ", (uint64_t) h.getEntropy(), f.entropy);
			checkEq("NETH.spdcid         ", (uint64_t) h.getSpdcid(), f.spdcid);
			checkEq("NETH.dpdcid         ", (uint64_t) h.getDpdcid(), f.dpdcid);
			checkEq("NETH.nack_psn       ", (uint64_t) h.getNackPsn(), f.nackPsn);
			checkEq("NETH.cc_type        ", (uint64_t) h.getCcType(), f.ccType);
			checkEq("NETH.cc_fl          ", (uint64_t) h.getCcFlags(), f.ccFl);
			checkEq("NETH.tx_timestamp   ", (uint64_t) h.getTxTimestamp(), f.txTimestamp);
		}
	}
	{
		ProbeFields f = exampleProbe();
		CtrlPacket c = probePacket(f);
		std::cout << "[TB] " << c.name << std::endl;
		if (parseCtrlUnit<W>(c, m))
		{
			ProbeExHeader<W> h = m.getProbeHeader();
			checkEq("PETH length ok      ", m.lengthOk, 1);
			checkEq("PETH.vendor_info    ", (uint64_t) h.getVendorInfo(), f.vendorInfo);
			checkEq("PETH.probe_id       ", (uint64_t) h.getProbeId(), f.probeId);
			checkEq("PETH.spdcid         ", (uint64_t) h.getSpdcid(), f.spdcid);
			checkEq("PETH.dpdcid         ", (uint64_t) h.getDpdcid(), f.dpdcid);
			checkEq("PETH.tx_timestamp   ", (uint64_t) h.getTxTimestamp(), f.txTimestamp);
			checkEq("PETH.tsr            ", h.getTsr(), f.tsr);
			checkEq("PETH.ftype          ", (uint64_t) h.getFtype(), f.ftype);
		}
	}
	{
		EpReqFields f = exampleEpReq();
		CtrlPacket c = epReqPacket(f, EV_PROBE_ID);
		std::cout << "[TB] " << c.name << std::endl;
		if (parseCtrlUnit<W>(c, m))
		{
			EpReqExHeader<W> h = m.getEpReqHeader();
			checkEq("ERTH length ok      ", m.lengthOk, 1);
			checkEq("ERTH.op             ", (uint64_t) h.getOp(), f.op);
			checkEq("ERTH.vendor_info    ", (uint64_t) h.getVendorInfo(), f.vendorInfo);
			checkEq("ERTH.port_status    ", (uint64_t) h.getPortStatusMask(), f.portStatusMask);
			checkEq("ERTH.tx_timestamp   ", (uint64_t) h.getTxTimestamp(), f.txTimestamp);
			checkEq("ERTH.tsr            ", h.getTsr(), f.tsr);
			checkEq("ERTH.ftype          ", (uint64_t) h.getFtype(), f.ftype);
			checkEq("BTH.dqp (EV probe)  ", (uint64_t) m.dest_qp, MRC_EV_PROBE_DQP.to_uint());
			checkEq("BTH.psn[15:0] (id)  ", m.psn(15, 0).to_uint64(), EV_PROBE_ID & 0xFFFF);
		}
	}
	{
		EpRspFields f = exampleEpRsp();
		CtrlPacket c = epRspPacket(f, EV_PROBE_ID);
		std::cout << "[TB] " << c.name << std::endl;
		if (parseCtrlUnit<W>(c, m))
		{
			EpRspExHeader<W> h = m.getEpRspHeader();
			checkEq("EETH length ok      ", m.lengthOk, 1);
			checkEq("EETH.op             ", (uint64_t) h.getOp(), f.op);
			checkEq("EETH.tx_timestamp   ", (uint64_t) h.getTxTimestamp(), f.txTimestamp);
		}
	}
	{
		CtrlPacket c = Truncated(sackPacket(exampleSack()), 12);
		std::cout << "[TB] " << c.name << std::endl;
		if (parseCtrlUnit<W>(c, m))
			checkEq("truncated SACK flagged (lengthOk)", m.lengthOk, 0);
	}
	{
		CtrlPacket c = Truncated(sackPacket(exampleSack()), 0);
		std::cout << "[TB] " << c.name << std::endl;
		if (parseCtrlUnit<W>(c, m))
			checkEq("BTH-only SACK flagged (lengthOk)", m.lengthOk, 0);
	}
	{
		// a long header followed by a short one: nothing of the first may survive in the second
		CtrlPacket big   = epRspPacket(exampleEpRsp(), 0);
		big.hdr.assign(big.hdr.size(), 0xFF);
		CtrlPacket small = probePacket(ProbeFields());
		std::cout << "[TB] 36 B of 0xFF, then an all-zero PETH (16 B)" << std::endl;
		parseCtrlUnit<W>(big, m);
		if (parseCtrlUnit<W>(small, m))
		{
			checkEq("no stale bytes in raw[191:128]", m.raw(191, 128).to_uint64(), 0);
			checkEq("no stale bytes in raw[287:224]", m.raw(MRC_CTRL_HDR_SIZE-1, 224).to_uint64(), 0);
		}
	}
}