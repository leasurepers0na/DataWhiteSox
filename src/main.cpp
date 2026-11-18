// DataWhiteSox (DWS) —— 行情采集主程序
//
// 用法：
//   dws --config config/dws.json
//
// 配置（JSON，账号不硬编码）：
// {
//   "mode": "mock" | "ctp",
//   "out_dir": "/data/market",
//   "instruments": ["rb2501", "cu2501"],
//   "variety_code": { "rb": 108, "cu": 102 },
//   "ctp": {
//     "front": "tcp://x.x.x.x:10211",
//     "broker_id": "", "user_id": "", "password": "",
//     "app_id": "", "auth_code": ""
//   }
// }
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "pipeline.hpp"
#include "source.hpp"

namespace {

std::string ReadFile(const std::string &p)
{
	std::ifstream in(p);
	if (!in.is_open()) return {};
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// 极简 JSON 取值（避免引入第三方依赖）
std::string JStr(const std::string &s, const std::string &k, const std::string &def = "")
{
	auto pos = s.find("\"" + k + "\"");
	if (pos == std::string::npos) return def;
	pos = s.find(':', pos);
	if (pos == std::string::npos) return def;
	pos = s.find('"', pos);
	if (pos == std::string::npos) return def;
	const auto end = s.find('"', pos + 1);
	if (end == std::string::npos) return def;
	return s.substr(pos + 1, end - pos - 1);
}

std::vector<std::string> JStrArray(const std::string &s, const std::string &k)
{
	std::vector<std::string> out;
	auto pos = s.find("\"" + k + "\"");
	if (pos == std::string::npos) return out;
	pos = s.find('[', pos);
	const auto end = s.find(']', pos);
	if (pos == std::string::npos || end == std::string::npos) return out;
	const std::string body = s.substr(pos + 1, end - pos - 1);
	size_t i = 0;
	while ((i = body.find('"', i)) != std::string::npos)
	{
		const auto e = body.find('"', i + 1);
		if (e == std::string::npos) break;
		out.push_back(body.substr(i + 1, e - i - 1));
		i = e + 1;
	}
	return out;
}

} // namespace

int main(int argc, char **argv)
{
	std::string cfgPath = "config/dws.json";
	for (int i = 1; i < argc; ++i)
		if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) cfgPath = argv[++i];

	const std::string cfg = ReadFile(cfgPath);
	if (cfg.empty())
	{
		std::fprintf(stderr, "[DWS] cannot read config: %s\n", cfgPath.c_str());
		return 1;
	}

	const std::string mode = JStr(cfg, "mode", "mock");
	const std::string outDir = JStr(cfg, "out_dir", "./out");
	auto instruments = JStrArray(cfg, "instruments");
	if (instruments.empty()) instruments = {"rb2501", "cu2501"};

	std::map<std::string, int> codeMap;
	{
		// variety_code: {"rb":108,"cu":102}
		auto pos = cfg.find("\"variety_code\"");
		if (pos != std::string::npos)
		{
			auto lb = cfg.find('{', pos), rb = cfg.find('}', pos);
			if (lb != std::string::npos && rb != std::string::npos)
			{
				std::string body = cfg.substr(lb + 1, rb - lb - 1);
				size_t i = 0;
				while ((i = body.find('"', i)) != std::string::npos)
				{
					auto e = body.find('"', i + 1);
					if (e == std::string::npos) break;
					std::string key = body.substr(i + 1, e - i - 1);
					auto c = body.find(':', e);
					auto comma = body.find(',', c);
					std::string val = body.substr(c + 1, (comma == std::string::npos ? body.size() : comma) - c - 1);
					codeMap[key] = std::atoi(val.c_str());
					i = (comma == std::string::npos) ? body.size() : comma + 1;
				}
			}
		}
	}

	std::printf("[DWS] mode=%s out=%s instruments=%zu\n", mode.c_str(), outDir.c_str(), instruments.size());

	dws::BinSink sink(outDir);
	auto onBar = [&](const std::string &inst, const dws::Bar1Min &b) {
		sink.Write(b);
		std::printf("[DWS] bar inst=%s day=%d t=%llu O=%.2f C=%.2f vol=%d\n",
					inst.c_str(), b.tradingday, (unsigned long long)b.time,
					b.open, b.close, b.volume);
	};
	dws::BarBuilder builder(onBar, codeMap);

	// 数据源：目前只有 mock；CTP 版本在 source_ctp.hpp（待前置就绪）
	std::unique_ptr<dws::ISource> src;
	if (mode == "mock")
	{
		auto m = std::make_unique<dws::MockSource>(instruments, 3);
		m->SetBaseTime(1735689600000LL);
		src = std::move(m);
	}
	else
	{
		std::fprintf(stderr, "[DWS] mode=%s 尚未实现（缺 CTP 库/前置）\n", mode.c_str());
		return 2;
	}

	if (!src->Connect() || !src->Subscribe(instruments))
	{
		std::fprintf(stderr, "[DWS] connect/subscribe failed\n");
		return 3;
	}

	std::vector<dws::Snapshot> batch;
	int idle = 0;
	for (;;)
	{
		if (!src->Poll(batch))
		{
			if (!src->Alive()) break;
			if (++idle > 20) break;      // mock 模式跑完即退出
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}
		idle = 0;
		for (const auto &s : batch) builder.OnSnapshot(s);
	}
	builder.Flush();
	sink.Close();
	src->Close();
	std::printf("[DWS] done\n");
	return 0;
}
