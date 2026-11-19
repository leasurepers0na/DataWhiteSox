#pragma once
// 快照 → 1min bar → 月度二进制（与 FR 的 FutureData1Min 记录布局一致）
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "source.hpp"

namespace dws {

#pragma pack(push, 1)
struct Bar1Min
{
	std::int32_t code = 0;            // @0
	std::int32_t contract_month = 0;  // @4
	std::uint64_t time = 0;           // @8
	float open = 0, high = 0, close = 0, low = 0;   // @16
	std::int32_t volume = 0;          // @32
	std::int32_t pad0 = 0;            // @36
	double turnover = 0;              // @40
	double vwap = 0;                  // @48
	std::int32_t position = 0;        // @56
	std::int32_t tradingday = 0;      // @60
	std::int32_t up_kc = 0;           // @64
	std::int32_t down_kc = 0;         // @68
	std::int32_t up_pc = 0;           // @72
	std::int32_t down_pc = 0;         // @76
	std::int32_t oi_change = 0;       // @80
	std::int16_t fill_flag = 0;       // @84
	std::int16_t pad1 = 0;            // @86
	double price_adj = 0;             // @88
};
#pragma pack(pop)
static_assert(sizeof(Bar1Min) == 96, "Bar1Min must be 96 bytes");

struct Acc
{
	bool started = false;
	double open = 0, high = 0, low = 0, close = 0;
	double vol_first = 0, vol_last = 0;
	double turn_first = 0, turn_last = 0;
	double oi_first = 0, oi_last = 0;
	std::int64_t minute_ms = 0;
	int trading_day = 0;

	void Feed(const Snapshot &s)
	{
		if (!started)
		{
			started = true;
			open = high = low = close = s.last;
			vol_first = s.volume;
			turn_first = s.turnover;
			oi_first = s.open_interest;
			minute_ms = (s.ts_ms / 60000) * 60000;
			trading_day = s.trading_day;
		}
		high = std::max(high, s.last);
		low = std::min(low, s.last);
		close = s.last;
		vol_last = s.volume;
		turn_last = s.turnover;
		oi_last = s.open_interest;
	}

	void Finish(Bar1Min &b) const
	{
		b.time = static_cast<std::uint64_t>(minute_ms);
		b.open = static_cast<float>(open);
		b.high = static_cast<float>(high);
		b.close = static_cast<float>(close);
		b.low = static_cast<float>(low);
		const double v = vol_last - vol_first;
		const double t = turn_last - turn_first;
		b.volume = static_cast<std::int32_t>(v < 0 ? 0 : v);
		b.turnover = t < 0 ? 0 : t;
		b.vwap = (b.volume > 0) ? (b.turnover / b.volume) : close;
		b.position = static_cast<std::int32_t>(oi_last);
		b.oi_change = static_cast<std::int32_t>(oi_last - oi_first);
		b.tradingday = trading_day;
		b.price_adj = 1.0;
	}
};

inline void SplitInstrument(const std::string &inst, std::string &variety, int &month)
{
	size_t i = 0;
	while (i < inst.size() && !std::isdigit(static_cast<unsigned char>(inst[i]))) ++i;
	variety = inst.substr(0, i);
	month = (i < inst.size()) ? std::atoi(inst.c_str() + i) : 0;
}

// ── 月度二进制写入器 ─────────────────────────────────────────────────────
class BinSink
{
public:
	explicit BinSink(std::string dir) : m_dir(std::move(dir))
	{
		std::error_code ec;
		std::filesystem::create_directories(m_dir, ec);
	}
	~BinSink() { Close(); }

	void Write(const Bar1Min &b)
	{
		const int month = b.tradingday / 100;
		if (month != m_month) OpenMonth(month);
		if (!m_file || !m_file->is_open()) return;
		m_file->write(reinterpret_cast<const char *>(&b), sizeof(b));
		auto &d = m_days[b.tradingday];
		if (d.second == 0) d.first = m_total;
		d.second += 1;
		++m_total;
	}

	void Close()
	{
		if (m_file)
		{
			m_file->flush();
			PatchRecordCount();
			m_file->close();
			delete m_file;
			m_file = nullptr;
			WriteIdx();
		}
	}

private:
	void WriteHeader()
	{
		// FR 的月度读取路径：先校验 "FBM1" + rec_size，再 seek 20 + off*rec。
		// 头布局：magic[4] + rec_size(u32) + record_count(u64) + reserved(u32) = 20 字节。
		const std::uint32_t rec = static_cast<std::uint32_t>(sizeof(Bar1Min));
		const std::uint64_t zero = 0;
		const std::uint32_t reserved = 0;
		m_file->write("FBM1", 4);
		m_file->write(reinterpret_cast<const char *>(&rec), 4);
		m_file->write(reinterpret_cast<const char *>(&zero), 8);
		m_file->write(reinterpret_cast<const char *>(&reserved), 4);
	}

	void PatchRecordCount()
	{
		const std::uint64_t total = static_cast<std::uint64_t>(m_total);
		m_file->seekp(8, std::ios::beg);
		m_file->write(reinterpret_cast<const char *>(&total), 8);
		m_file->seekp(0, std::ios::end);
	}

	void OpenMonth(int month)
	{
		Close();
		m_month = month;
		m_total = 0;
		m_days.clear();
		const std::string base = m_dir + "/" + std::to_string(month);
		m_file = new std::ofstream(base + ".bin", std::ios::binary | std::ios::trunc);
		m_idx_base = base;
		if (m_file && m_file->is_open())
			WriteHeader();
	}

	void WriteIdx()
	{
		std::ofstream idx(m_idx_base + ".idx", std::ios::binary | std::ios::trunc);
		if (!idx.is_open()) return;
		const std::int32_t n = static_cast<std::int32_t>(m_days.size());
		idx.write(reinterpret_cast<const char *>(&n), 4);
		for (const auto &kv : m_days)
		{
			const std::int32_t day = kv.first;
			const std::int64_t off = kv.second.first;
			const std::int64_t cnt = kv.second.second;
			idx.write(reinterpret_cast<const char *>(&day), 4);
			idx.write(reinterpret_cast<const char *>(&off), 8);
			idx.write(reinterpret_cast<const char *>(&cnt), 8);
		}
	}

	std::string m_dir;
	std::string m_idx_base;
	std::ofstream *m_file = nullptr;
	int m_month = 0;
	long long m_total = 0;
	std::map<int, std::pair<long long, long long>> m_days;   // day -> (offset, count)
};

// ── 组装器：把快照汇聚成 bar ─────────────────────────────────────────────
class BarBuilder
{
public:
	using OnBar = std::function<void(const std::string &inst, const Bar1Min &)>;

	BarBuilder(OnBar cb, std::map<std::string, int> variety_code)
		: m_cb(std::move(cb)), m_code(std::move(variety_code)) {}

	void OnSnapshot(const Snapshot &s)
	{
		std::string variety;
		int month = 0;
		SplitInstrument(s.instrument, variety, month);
		const std::int64_t minute = (s.ts_ms / 60000) * 60000;

		auto it = m_acc.find(s.instrument);
		if (it != m_acc.end() && it->second.minute_ms != minute)
		{
			Emit(s.instrument, it->second);
			it->second = Acc{};
		}
		m_acc[s.instrument].Feed(s);
	}

	void Flush()
	{
		for (auto &kv : m_acc)
			if (kv.second.started) Emit(kv.first, kv.second);
		m_acc.clear();
	}

private:
	void Emit(const std::string &inst, const Acc &acc)
	{
		Bar1Min b;
		acc.Finish(b);
		std::string variety;
		int month = 0;
		SplitInstrument(inst, variety, month);
		b.contract_month = month;
		auto it = m_code.find(variety);
		b.code = (it == m_code.end()) ? 0 : it->second;
		if (m_cb) m_cb(inst, b);
	}

	OnBar m_cb;
	std::map<std::string, int> m_code;
	std::map<std::string, Acc> m_acc;
};

} // namespace dws
