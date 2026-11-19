#pragma once
// 仅用于无 CTP 官方库时的语法编译检查，不是官方 SDK。
#include <cstdint>

struct CThostFtdcReqUserLoginField
{
	char BrokerID[16];
	char UserID[16];
	char Password[16];
};

struct CThostFtdcReqAuthenticateField
{
	char BrokerID[16];
	char UserID[16];
	char AppID[16];
	char AuthCode[16];
};

struct CThostFtdcRspInfoField
{
	int ErrorID;
	char ErrorMsg[256];
};

struct CThostFtdcRspUserLoginField
{
	char TradingDay[9];
};

struct CThostFtdcRspAuthenticateField
{
	char UserID[16];
};

struct CThostFtdcDepthMarketDataField
{
	char InstrumentID[32];
	double LastPrice;
	double OpenPrice;
	double HighestPrice;
	double LowestPrice;
	double Turnover;
	double OpenInterest;
	double BidPrice1;
	double AskPrice1;
	int Volume;
	int BidVolume1;
	int AskVolume1;
	int UpdateMillisec;
	char TradingDay[9];
	char ActionDay[9];
	char UpdateTime[16];
};

class CThostFtdcMdSpi
{
public:
	virtual ~CThostFtdcMdSpi() = default;
	virtual void OnFrontConnected() {}
	virtual void OnFrontDisconnected(int) {}
	virtual void OnRspUserLogin(CThostFtdcRspUserLoginField *, CThostFtdcRspInfoField *, int, bool) {}
	virtual void OnRspAuthenticate(CThostFtdcRspAuthenticateField *, CThostFtdcRspInfoField *, int, bool) {}
	virtual void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *) {}
};

class CThostFtdcMdApi
{
public:
	static CThostFtdcMdApi *CreateFtdcMdApi(const char * = "", bool = false, bool = false) { return nullptr; }
	virtual ~CThostFtdcMdApi() = default;
	virtual void RegisterSpi(CThostFtdcMdSpi *) {}
	virtual void RegisterFront(char *) {}
	virtual void Init() {}
	virtual void Release() {}
	virtual int SubscribeMarketData(char **, int) { return 0; }
	virtual int ReqUserLogin(CThostFtdcReqUserLoginField *, int) { return 0; }
	virtual int ReqAuthenticate(CThostFtdcReqAuthenticateField *, int) { return 0; }
};
