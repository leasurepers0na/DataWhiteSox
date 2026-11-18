#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CTP 行情数据源（编译时用 DWS_HAVE_CTP 宏隔离）
//
// 没有 CTP 库时本文件不产生任何依赖，DWS 仍可用 Mock 模式编译运行。
// 接入步骤见 README「接入 CTP」。
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "source.hpp"

#ifdef DWS_HAVE_CTP
#include "ThostFtdcMdApi.h"

namespace dws {

class CtpMdSource : public ISource, public CThostFtdcMdSpi
{
public:
	struct Config
	{
		std::string front;
		std::string broker_id;
		std::string user_id;
		std::string password;
		std::string app_id;
		std::string auth_code;
	};

	explicit CtpMdSource(Config cfg) : m_cfg(std::move(cfg)) {}
	~CtpMdSource() override { Close(); }

	bool Connect() override
	{
		m_api = CThostFtdcMdApi::CreateFtdcMdApi("./ctp_flow/", false, false);
		if (!m_api) return false;
		m_api->RegisterSpi(this);
		m_api->RegisterFront(const_cast<char *>(m_cfg.front.c_str()));
		m_api->Init();
		return true;
	}

	bool Subscribe(const std::vector<std::string> &instruments) override
	{
		m_subs = instruments;
		m_subscribed = false;   // 登录成功后由 OnFrontConnected 之后触发
		return true;
	}

	bool Poll(std::vector<Snapshot> &out) override
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (m_q.empty()) return false;
		out.clear();
		while (!m_q.empty())
		{
			out.push_back(std::move(m_q.front()));
			m_q.pop();
		}
		return true;
	}

	bool Alive() const override { return m_alive.load(); }

	void Close() override
	{
		if (m_api)
		{
			m_api->RegisterSpi(nullptr);
			m_api->Release();
			m_api = nullptr;
		}
		m_alive.store(false);
	}

	// ── CTP 回调 ──────────────────────────────────────────────────────
	void OnFrontConnected() override
	{
		CThostFtdcReqUserLoginField req;
		std::memset(&req, 0, sizeof(req));
		std::strncpy(req.BrokerID, m_cfg.broker_id.c_str(), sizeof(req.BrokerID) - 1);
		std::strncpy(req.UserID, m_cfg.user_id.c_str(), sizeof(req.UserID) - 1);
		std::strncpy(req.Password, m_cfg.password.c_str(), sizeof(req.Password) - 1);
		m_api->ReqUserLogin(&req, 0);
	}

	void OnFrontDisconnected(int reason) override
	{
		// 由外部循环负责重连；这里只标记不可用
		m_alive.store(false);
		std::fprintf(stderr, "[DWS][CTP] front disconnected: %d\n", reason);
	}

	void OnRspUserLogin(CThostFtdcRspUserLoginField *pRsp, CThostFtdcRspInfoField *pInfo, int, bool) override
	{
		if (pInfo && pInfo->ErrorID != 0)
		{
			std::fprintf(stderr, "[DWS][CTP] login failed: %d %s\n", pInfo->ErrorID, pInfo->ErrorMsg);
			return;
		}
		// 需要 AppID/AuthCode 的柜台要先 ReqAuthenticate，此处略（见 README TODO）
		m_alive.store(true);
		if (!m_subscribed && !m_subs.empty())
		{
			std::vector<char *> arr;
			arr.reserve(m_subs.size());
			for (auto &s : m_subs) arr.push_back(const_cast<char *>(s.c_str()));
			m_api->SubscribeMarketData(arr.data(), static_cast<int>(arr.size()));
			m_subscribed = true;
		}
	}

	void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *d) override
	{
		if (!d) return;
		Snapshot s;
		s.instrument = d->InstrumentID;
		s.last = d->LastPrice;
		s.open = d->OpenPrice;
		s.high = d->HighestPrice;
		s.low = d->LowestPrice;
		s.volume = d->Volume;
		s.turnover = d->Turnover;
		s.open_interest = d->OpenInterest;
		s.bid[0] = d->BidPrice1;
		s.ask[0] = d->AskPrice1;
		s.bid_vol[0] = d->BidVolume1;
		s.ask_vol[0] = d->AskVolume1;
		s.trading_day = d->TradingDay ? std::atoi(d->TradingDay) : 0;
		// UpdateTime(HH:MM:SS) + UpdateMillisec → epoch ms 由 BarBuilder 侧处理
		s.ts_ms = 0;   // TODO: 按 ActionDay + UpdateTime 拼毫秒
		std::lock_guard<std::mutex> lk(m_mtx);
		m_q.push(std::move(s));
	}

private:
	Config m_cfg;
	CThostFtdcMdApi *m_api = nullptr;
	std::vector<std::string> m_subs;
	bool m_subscribed = false;
	std::atomic<bool> m_alive{false};
	std::mutex m_mtx;
	std::queue<Snapshot> m_q;
};

} // namespace dws

#endif // DWS_HAVE_CTP
