# DataWhiteSox (DWS)

通用行情数据获取框架。首个数据源是 **CTP 行情**（SimNow / openctp / 期货公司前置均可对接），
产出的 1min bar 二进制与 **FactorReplay (FR)** 的记录布局完全一致，可直接被 FR 消费。

## 1. 定位

```
ISource ──snapshot──> BarBuilder ──bar──> BinSink ──> YYYYMM.bin + .idx
                                                        ↑
                                                   FactorReplay 直接读
```

- `ISource`：数据源抽象。现在有 `MockSource`（离线验证），CTP 实现在 `source_ctp.hpp`
- `BarBuilder`：500ms 快照 → 1min bar
- `BinSink`：月度二进制 + 索引

## 2. 构建

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j 8
```

产物：`build/dws`

## 3. 配置

```json
{
  "mode": "mock",
  "out_dir": "/data/market",
  "instruments": ["rb2501", "cu2501"],
  "variety_code": { "rb": 108, "cu": 102 },
  "ctp": {
    "front": "tcp://x.x.x.x:10211",
    "broker_id": "",
    "user_id": "",
    "password": "",
    "app_id": "",
    "auth_code": ""
  }
}
```

**账号绝不硬编码**，可写在配置或通过环境变量注入（见 `ctp` 段）。

## 4. 运行

```bash
./dws --config config/dws.json
```

mock 模式：

```
[DWS] mode=mock out=./out instruments=2
[DWS] bar inst=rb2501 day=20250101 t=1735689600000 O=3000.00 C=3000.29 vol=2
...
[DWS] done
```

产物结构：

```
out/
  202501.bin      # 96 字节/条，与 FR 的 FutureData1Min 一致
  202501.idx      # int32 ndays + 每日 (day, offset, count)
```

## 5. 接入 CTP

1. 拿到期货公司（或 SimNow/openctp）的：
   - 行情前置地址 `tcp://ip:port`
   - `BrokerID`
   - 行情账号 / 密码
   - `AppID` / `AuthCode`（很多公司需要）
2. 把 CTP 官方 Linux x64 库放到 `third_party/ctp/`：
   ```
   libthostmduserapi_se.so
   libthosttraderapi_se.so（只做行情则不需要）
   头文件 ThostFtdcMdApi.h / ThostFtdcUserApiStruct.h / ThostFtdcUserApiDataType.h
   ```
3. 把 `mode` 改成 `ctp`，填好配置。
4. 编译时链接：
   ```cmake
   target_include_directories(dws PRIVATE third_party/ctp)
   target_link_libraries(dws PRIVATE ${CMAKE_SOURCE_DIR}/third_party/ctp/libthostmduserapi_se.so)
   ```

### CTP 关键点

```
· OnRtnDepthMarketData 里 UpdateTime/UpdateMillisec 拼成 epoch ms
· 夜盘 >= 20:00 归下一交易日
· 期货公司每日凌晨维护会断连 → 必须自动重连 + 重订阅
· 同一账号的行情连接数有限（通常 1~3）
```

## 6. 扩展交易所

`instruments` 里写全即可，CTP 一次订阅覆盖 SHFE/INE/DCE/CZCE/CFFEX/GFEX：

```json
"instruments": ["rb2501", "cu2501", "i2501", "m2501", "CF501", "lc2501"]
```

## 7. 与 FR 的对接

```
DWS 持续写 YYYYMM.bin
  ↓
FR 的 real 模式：前摇用历史，之后消费 DWS 的实时数据
FR 的 his 模式：收盘后直接用 DWS 产出的月度文件回放
```

## 8. 当前状态

```
✅ ISource 抽象 + MockSource
✅ 500ms → 1min bar
✅ 月度二进制 + 索引（与 FR 布局一致）
⏳ CTP 适配（缺库与前置，代码将放在 source_ctp.hpp）
⏳ 断线重连
```
