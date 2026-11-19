#pragma once
// 0.5s 聚合：Snapshot(500ms) -> FutureData05(184B)
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "live_fifo05.hpp"
#include "pipeline.hpp"

namespace dws {

class BarBuilder05
{
public:
	using OnBar = std::function<void(const std::string &, const FutureData05 &)>;

	BarBuilder05(OnBar cb, std::map<std::string, int> variety_code)
		: m_cb(std::move(cb)), m_code(std::move(variety_code)) {}

	void OnSnapshot(const Snapshot &s)
	{
		const std::int64_t bucket = (s.ts_ms / 500) * 500;
		auto it = m_acc.find(s.instrument);
		if (it != m_acc.end() && it->second.bucket_ms != bucket)
		{
			Emit(s.instrument, it->second);
			it->second = Acc{};
		}
		m_acc[s.instrument].Feed(s, bucket);
	}

	void Flush()
	{
		for (auto &kv : m_acc)
			if (kv.second.started) Emit(kv.first, kv.second);
		m_acc.clear();
	}

private:
	struct Acc
	{
		bool started = false;
		double open = 0, high = 0, low = 0, close = 0;
		double vol_first = 0, vol_last = 0;
		double turn_first = 0, turn_last = 0;
		double oi_first = 0, oi_last = 0;
		std::int64_t bucket_ms = 0;
		int trading_day = 0;
		double bid[5] = {0}, ask[5] = {0};
		int bid_vol[5] = {0}, ask_vol[5] = {0};

		void Feed(const Snapshot &s, std::int64_t bucket)
		{
			if (!started)
			{
				started = true;
				open = high = low = close = s.last;
				vol_first = s.volume;
				turn_first = s.turnover;
				oi_first = s.open_interest;
				bucket_ms = bucket;
				trading_day = s.trading_day;
			}
			high = std::max(high, s.last);
			low = std::min(low, s.last);
			close = s.last;
			vol_last = s.volume;
			turn_last = s.turnover;
			oi_last = s.open_interest;
			for (int i = 0; i < 5; ++i)
			{
				bid[i] = s.bid[i];
				ask[i] = s.ask[i];
				bid_vol[i] = s.bid_vol[i];
				ask_vol[i] = s.ask_vol[i];
			}
		}

		void Finish(FutureData05 &b) const
		{
			b.time = static_cast<std::uint64_t>(bucket_ms);
			b.open = static_cast<float>(open);
			b.high = static_cast<float>(high);
			b.close = static_cast<float>(close);
			b.low = static_cast<float>(low);
			const double v = vol_last - vol_first;
			const double t = turn_last - turn_first;
			b.volume = static_cast<std::int32_t>(v < 0 ? 0 : v);
			b.turnover = t < 0 ? 0 : t;
			b.vwap = (b.volume > 0) ? static_cast<float>(b.turnover / b.volume)
			                       : static_cast<float>(close);
			b.twap = static_cast<float>((open + high + low + close) / 4.0);
			b.position = static_cast<std::int32_t>(oi_last);
			b.oi_change = static_cast<std::int32_t>(oi_last - oi_first);
			b.tradingdate = trading_day;
			b.s5 = static_cast<float>(ask[4]);
			b.s4 = static_cast<float>(ask[3]);
			b.s3 = static_cast<float>(ask[2]);
			b.s2 = static_cast<float>(ask[1]);
			b.s1 = static_cast<float>(ask[0]);
			b.b1 = static_cast<float>(bid[0]);
			b.b2 = static_cast<float>(bid[1]);
			b.b3 = static_cast<float>(bid[2]);
			b.b4 = static_cast<float>(bid[3]);
			b.b5 = static_cast<float>(bid[4]);
			b.sv5 = ask_vol[4];
			b.sv4 = ask_vol[3];
			b.sv3 = ask_vol[2];
			b.sv2 = ask_vol[1];
			b.sv1 = ask_vol[0];
			b.bv1 = bid_vol[0];
			b.bv2 = bid_vol[1];
			b.bv3 = bid_vol[2];
			b.bv4 = bid_vol[3];
			b.bv5 = bid_vol[4];
			b.fill_flag = 0;
			b.price_adj = 1.0;
			b.volume_all = b.volume;
			b.position_all = b.position;
		}
	};

	void Emit(const std::string &inst, const Acc &acc)
	{
		FutureData05 b;
		acc.Finish(b);
		// 每个 500ms 桶只有一条快照，桶内差分恒为 0；
		// 必须用上一桶的累计 volume/turnover 做差分。
		double v = 0, t = 0;
		const std::int64_t minute = acc.bucket_ms / 60000;
		auto pm = m_prevMinute.find(inst);
		const bool newMinute = (pm == m_prevMinute.end() || pm->second != minute);
		auto pv = m_prevVol.find(inst);
		if (!newMinute && pv != m_prevVol.end())
		{
			v = acc.vol_last - pv->second;
			t = acc.turn_last - m_prevTurn[inst];
		}
		m_prevMinute[inst] = minute;
		m_prevVol[inst] = acc.vol_last;
		m_prevTurn[inst] = acc.turn_last;
		if (v < 0) v = 0;
		if (t < 0) t = 0;
		b.volume = static_cast<std::int32_t>(v);
		b.turnover = t;
		b.vwap = (b.volume > 0) ? static_cast<float>(b.turnover / b.volume)
		                       : b.close;
		b.volume_all = b.volume;
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
	std::map<std::string, double> m_prevVol;
	std::map<std::string, double> m_prevTurn;
	std::map<std::string, std::int64_t> m_prevMinute;
};

} // namespace dws
