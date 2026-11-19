#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DataWhiteSox (DWS) —— 通用行情数据获取框架
//
// 设计目标：
//   把「行情来源」与「数据处理」解耦。
//   第一个实现是 CTP 行情（SimNow / openctp / 期货公司前置都可对接），
//   以后接 Level-2、其它柜台、交易所直连，只要实现 ISource 即可。
//
// 数据流：
//   ISource ──snapshot──> BarBuilder ──bar──> BinSink ──> YYYYMM.bin/.idx
//                                                          ↑
//                                              FactorReplay 直接消费
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace dws {

// ── 行情快照（CTP DepthMarketData 的归一化形态）────────────────────────────
struct Snapshot
{
	std::string instrument;   ///< 合约代码，如 "rb2501"
	std::int64_t ts_ms = 0;   ///< 交易所时间（epoch 毫秒，UTC+8 语义）
	double last = 0.0;        ///< 最新价
	double open = 0.0;
	double high = 0.0;
	double low = 0.0;
	double volume = 0.0;      ///< 累计成交量（当日）
	double turnover = 0.0;    ///< 累计成交额（当日）
	double open_interest = 0.0;
	double bid[5] = {0, 0, 0, 0, 0};
	double ask[5] = {0, 0, 0, 0, 0};
	int bid_vol[5] = {0, 0, 0, 0, 0};
	int ask_vol[5] = {0, 0, 0, 0, 0};
	int trading_day = 0;      ///< 交易日 YYYYMMDD（夜盘归下一交易日）
};

// ── 数据源接口 ────────────────────────────────────────────────────────────
class ISource
{
public:
	virtual ~ISource() = default;

	/// 连接/登录；返回 false 表示失败（调用方可重试）
	virtual bool Connect() = 0;

	/// 订阅合约列表
	virtual bool Subscribe(const std::vector<std::string> &instruments) = 0;

	/// 取下一批快照；返回 false 表示暂时没有（调用方可以 sleep 后再试）
	virtual bool Poll(std::vector<Snapshot> &out) = 0;

	/// 是否仍然连接
	virtual bool Alive() const = 0;

	/// 主动断开
	virtual void Close() = 0;
};

// ── Mock 数据源：无前置时用历史/合成数据把链路跑通 ────────────────────────
class MockSource : public ISource
{
public:
	MockSource(std::vector<std::string> instruments, int minutes = 5,
	           int trading_day = 20250101,
	           std::int64_t base_ms = 1735689600000LL)
		: m_inst(std::move(instruments)), m_minutes(minutes),
		  m_trading_day(trading_day), m_base_ms(base_ms) {}

	bool Connect() override { m_alive = true; return true; }
	bool Subscribe(const std::vector<std::string> &) override { return m_alive; }

	bool Poll(std::vector<Snapshot> &out) override
	{
		if (!m_alive) return false;
		// 每根 bar 内产生 120 条快照（500ms 一条），与真实 CTP 节奏一致
		if (m_tick >= m_minutes * 120) return false;
		out.clear();
		for (size_t k = 0; k < m_inst.size(); ++k)
		{
			Snapshot s;
			s.instrument = m_inst[k];
			s.ts_ms = m_base_ms + m_tick * 500;
			const double px = 3000.0 + 10.0 * k + 0.01 * m_tick;
			s.last = s.open = s.high = s.low = px;
			s.volume = m_tick + 1;
			s.turnover = s.volume * px * 10.0;
			s.open_interest = 100000 + m_tick;
			for (int i = 0; i < 5; ++i)
			{
				s.bid[i] = px - 1.0 * (i + 1);
				s.ask[i] = px + 1.0 * (i + 1);
				s.bid_vol[i] = 10 + i;
				s.ask_vol[i] = 12 + i;
			}
			s.trading_day = m_trading_day;
			out.push_back(std::move(s));
		}
		++m_tick;
		return true;
	}

	bool Alive() const override { return m_alive; }
	void Close() override { m_alive = false; }

	void SetBaseTime(std::int64_t ms) { m_base_ms = ms; }

private:
	std::vector<std::string> m_inst;
	int m_minutes;
	int m_tick = 0;
	int m_trading_day = 20250101;
	bool m_alive = false;
	std::int64_t m_base_ms = 1735689600000LL;   // 2025-01-01 08:00 UTC+8
};

} // namespace dws
