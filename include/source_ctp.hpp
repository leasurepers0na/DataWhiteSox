#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CTP 行情数据源（编译时用 DWS_HAVE_CTP 宏隔离）
//
// 没有 CTP 库时本文件不产生任何依赖，DWS 仍可用 Mock 模式编译运行。
// 接入步骤见 README「接入 CTP」。
//
// 断线重连：回调只改状态并 notify；supervisor 线程负责销毁旧 API、
// 退避重建、重新鉴权/登录/订阅。Release 不在回调线程里调用。
// ─────────────────────────────────────────────────────────────────────────────
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "source.hpp"
#include "time_util.hpp"

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
		if (m_supervisor.joinable()) return true;
		m_stop.store(false);
		m_supervisor = std::thread([this]() { Supervise(); });
		return true;
	}

	bool Subscribe(const std::vector<std::string> &instruments) override
	{
		{
			std::lock_guard<std::mutex> lk(m_subMtx);
			m_subs = instruments;
		}
		m_subscribed.store(false);
		if (m_state.load() == kLoggedIn) SendSubscribe();
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

	bool Alive() const override { return m_state.load() == kLoggedIn; }

	void Close() override
	{
		m_stop.store(true);
		m_cv.notify_all();
		if (m_supervisor.joinable()) m_supervisor.join();
		StopApi();
	}

	// ── CTP 回调 ──────────────────────────────────────────────────────
	void OnFrontConnected() override
	{
		m_state.store(kConnecting);
		// MdApi 没有 ReqAuthenticate（认证是 TraderApi 的），连接后直接登录。
		SendLogin();
	}

	void OnFrontDisconnected(int reason) override
	{
		m_state.store(kDown);
		m_cv.notify_all();
		std::fprintf(stderr, "[DWS][CTP] front disconnected: %d\n", reason);
	}

	void OnRspUserLogin(CThostFtdcRspUserLoginField *, CThostFtdcRspInfoField *pInfo,
	                    int, bool) override
	{
		if (pInfo && pInfo->ErrorID != 0)
		{
			std::fprintf(stderr, "[DWS][CTP] login failed: %d %s\n", pInfo->ErrorID, pInfo->ErrorMsg);
			m_state.store(kDown);
			m_cv.notify_all();
			return;
		}
		m_state.store(kLoggedIn);
		m_cv.notify_all();
		SendSubscribe();
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
		// ActionDay 是自然日（夜盘 21:00 仍是当天）；交易日由 TradingDay 表示
		const int action_day = (d->ActionDay && d->ActionDay[0])
		                           ? std::atoi(d->ActionDay)
		                           : s.trading_day;
		s.ts_ms = MakeEpochMs(action_day, d->UpdateTime, d->UpdateMillisec);
		if (s.ts_ms == 0)
		{
			std::fprintf(stderr, "[DWS][CTP] bad time: ActionDay=%s UpdateTime=%s ms=%d\n",
			             d->ActionDay, d->UpdateTime, d->UpdateMillisec);
			return;
		}
		std::lock_guard<std::mutex> lk(m_mtx);
		m_q.push(std::move(s));
	}

private:
	enum State { kDown = 0, kConnecting = 1, kLoggedIn = 2 };

	bool StartApi()
	{
		if (m_api.load()) return true;
		CThostFtdcMdApi *api = CThostFtdcMdApi::CreateFtdcMdApi("./ctp_flow/", false, false);
		if (!api) return false;
		m_api.store(api);
		m_state.store(kConnecting);
		api->RegisterSpi(this);
		api->RegisterFront(const_cast<char *>(m_cfg.front.c_str()));
		api->Init();
		return true;
	}

	void StopApi()
	{
		CThostFtdcMdApi *api = m_api.exchange(nullptr);
		m_state.store(kDown);
		m_subscribed.store(false);
		if (api)
		{
			api->RegisterSpi(nullptr);
			api->Release();
		}
	}

	void Supervise()
	{
		int backoff = 1;
		while (!m_stop.load())
		{
			if (!StartApi())
			{
				std::this_thread::sleep_for(std::chrono::seconds(backoff));
				backoff = std::min(backoff * 2, 30);
				continue;
			}
			{
				std::unique_lock<std::mutex> lk(m_stateMtx);
				m_cv.wait_for(lk, std::chrono::seconds(20),
				              [this]() { return m_stop.load() || m_state.load() == kLoggedIn; });
				if (m_stop.load()) break;
				if (m_state.load() == kLoggedIn)
				{
					m_cv.wait(lk, [this]() {
						return m_stop.load() || m_state.load() != kLoggedIn;
					});
				}
			}
			StopApi();
			if (m_stop.load()) break;
			std::this_thread::sleep_for(std::chrono::seconds(backoff));
			backoff = std::min(backoff * 2, 30);
		}
		StopApi();
	}

	void SendLogin()
	{
		CThostFtdcMdApi *api = m_api.load();
		if (!api) return;
		CThostFtdcReqUserLoginField req;
		std::memset(&req, 0, sizeof(req));
		std::strncpy(req.BrokerID, m_cfg.broker_id.c_str(), sizeof(req.BrokerID) - 1);
		std::strncpy(req.UserID, m_cfg.user_id.c_str(), sizeof(req.UserID) - 1);
		std::strncpy(req.Password, m_cfg.password.c_str(), sizeof(req.Password) - 1);
		api->ReqUserLogin(&req, ++m_reqid);
	}

	void SendSubscribe()
	{
		if (m_subscribed.exchange(true)) return;
		std::vector<std::string> subs;
		{
			std::lock_guard<std::mutex> lk(m_subMtx);
			subs = m_subs;
		}
		CThostFtdcMdApi *api = m_api.load();
		if (subs.empty() || !api)
		{
			m_subscribed.store(false);
			return;
		}
		std::vector<char *> arr;
		arr.reserve(subs.size());
		for (auto &s : subs) arr.push_back(const_cast<char *>(s.c_str()));
		if (api->SubscribeMarketData(arr.data(), static_cast<int>(arr.size())) != 0)
			m_subscribed.store(false);
	}

	Config m_cfg;
	std::atomic<CThostFtdcMdApi *> m_api{nullptr};
	std::vector<std::string> m_subs;
	std::mutex m_subMtx;
	std::atomic<bool> m_subscribed{false};
	std::atomic<int> m_reqid{0};
	std::atomic<int> m_state{kDown};
	std::atomic<bool> m_stop{false};
	std::thread m_supervisor;
	std::mutex m_stateMtx;
	std::condition_variable m_cv;
	std::mutex m_mtx;
	std::queue<Snapshot> m_q;
};

} // namespace dws

#endif // DWS_HAVE_CTP
