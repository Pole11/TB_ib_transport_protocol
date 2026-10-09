#pragma once

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>

#include "axi_utils.hpp"
#include "ib_transport_protocol/ib_transport_protocol.hpp"
#include "tb_config.hpp"

static int g_failures = 0;
static int g_cycle    = 0;   // global "clock": number of DUT calls so far

static std::string hex(uint64_t v, int width = 0)
{
	std::stringstream ss;
	ss << "0x" << std::hex << std::setfill('0') << std::setw(width) << v;
	return ss.str();
}

static void check(bool ok, const std::string& what)
{
	std::cout << std::dec << (ok ? "  [PASS] " : "  [FAIL] ") << what << std::endl;
	if (!ok) g_failures++;
}

// Compare a value with its expected value. Addresses/fields print in hex, counts/lengths in decimal.
static void checkEq(const std::string& what, uint64_t got, uint64_t expected, bool decimal = false)
{
	std::stringstream ss;
	ss << what << " = ";
	if (decimal) ss << std::dec << got;      else ss << hex(got);
	if (got != expected)
	{
		ss << "   (expected ";
		if (decimal) ss << std::dec << expected; else ss << hex(expected);
		ss << ")";
	}
	check(got == expected, ss.str());
}
static void checkCount(const std::string& what, uint64_t got, uint64_t expected) { checkEq(what, got, expected, true); }

static void banner(const std::string& title)
{
	std::cout << std::endl << "=====================================================================" << std::endl;
	std::cout << "  " << title << std::endl;
	std::cout << "=====================================================================" << std::endl;
}

// Print bytes as a hex dump, 16 bytes per line (truncated after MAX_DUMP_BYTES).
static void hexDump(const std::vector<uint8_t>& bytes, const std::string& indent = "      ")
{
	size_t n = std::min(bytes.size(), MAX_DUMP_BYTES);
	for (size_t i = 0; i < n; i++)
	{
		if (i % 16 == 0) std::cout << indent << std::hex << std::setfill('0') << std::setw(4) << i << ": ";
		std::cout << std::hex << std::setfill('0') << std::setw(2) << (unsigned) bytes[i] << " ";
		if (i % 16 == 15 || i + 1 == n) std::cout << std::endl;
	}
	if (n < bytes.size())
		std::cout << indent << "... (" << std::dec << bytes.size() - n << " more bytes)" << std::endl;
	std::cout << std::dec;
}

// Wire-order byte array  ->  stream beats (byte i of the beat goes to data(8i+7, 8i), keep[i] = 1)
static std::vector<net_axis<DATA_WIDTH> > bytesToBeats(const std::vector<uint8_t>& bytes)
{
	std::vector<net_axis<DATA_WIDTH> > beats;
	for (size_t offset = 0; offset < bytes.size(); offset += BYTES_PER_BEAT)
	{
		net_axis<DATA_WIDTH> beat;
		beat.data = 0;
		beat.keep = 0;
		for (int i = 0; i < BYTES_PER_BEAT && offset + i < bytes.size(); i++)
		{
			beat.data(8 * i + 7, 8 * i) = bytes[offset + i];
			beat.keep[i] = 1;
		}
		beat.last = (offset + BYTES_PER_BEAT >= bytes.size());
		beats.push_back(beat);
	}
	return beats;
}

// Stream beat -> the bytes that are valid according to 'keep'
static void appendBeatBytes(const net_axis<DATA_WIDTH>& beat, std::vector<uint8_t>& out)
{
	for (int i = 0; i < BYTES_PER_BEAT; i++)
		if (beat.keep[i])
			out.push_back((uint8_t) beat.data(8 * i + 7, 8 * i).to_uint());
}

// Big-endian field writers (network byte order)
static void put8 (std::vector<uint8_t>& b, uint32_t v) { b.push_back(v & 0xFF); }
static void put16(std::vector<uint8_t>& b, uint32_t v) { put8(b, v >> 8);  put8(b, v); }
static void put24(std::vector<uint8_t>& b, uint32_t v) { put8(b, v >> 16); put16(b, v); }
static void put32(std::vector<uint8_t>& b, uint32_t v) { put16(b, v >> 16); put16(b, v); }
static void put64(std::vector<uint8_t>& b, uint64_t v) { put32(b, v >> 32); put32(b, (uint32_t) v); }

static uint32_t get24(const std::vector<uint8_t>& b, int i) { return (b[i] << 16) | (b[i + 1] << 8) | b[i + 2]; }

static std::string opcodeName(unsigned op)
{
	switch (op)
	{
	case MRC_RDMA_WRITE_FIRST:  return "RC_RDMA_WRITE_FIRST";
	case MRC_RDMA_WRITE_MIDDLE: return "RC_RDMA_WRITE_MIDDLE";
	case MRC_RDMA_WRITE_LAST:   return "RC_RDMA_WRITE_LAST";
	case MRC_RDMA_WRITE_ONLY:   return "RC_RDMA_WRITE_ONLY";
	case MRC_ACK:               return "MRC_ACK";
	case MRC_RDMA_WRITE_LAST_IMM: return "MRC_RDMA_WRITE_LAST_IMM";
	case MRC_RDMA_WRITE_ONLY_IMM: return "MRC_RDMA_WRITE_ONLY_IMM";
	case MRC_EP_REQ:              return "MRC_EP_REQ";
	case MRC_EP_RSP:              return "MRC_EP_RSP";
	case MRC_SACK:                return "MRC_SACK";
	case MRC_NACK:                return "MRC_NACK";
	case MRC_PROBE_REQ:           return "MRC_PROBE_REQ";
	default:                   return "opcode " + hex(op, 2);
	}
}
