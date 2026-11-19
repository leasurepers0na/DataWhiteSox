#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CTP 时间转换：ActionDay + UpdateTime + UpdateMillisec → epoch 毫秒（UTC+8）
//
// CTP 的 UpdateTime 是交易所本地时间 HH:MM:SS，ActionDay 是自然日 YYYYMMDD；
// 夜盘 21:00 的 ActionDay 仍是当天，TradingDay 才是下一交易日，所以这里
// 只负责「时间」；交易日由 CTP 的 TradingDay 字段单独传。
//
// 不依赖 timegm / 本地时区，用 Howard Hinnant 的 days_from_civil。
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <cstdio>

namespace dws {

inline bool IsLeapYear(int y)
{
	return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

inline int DaysInMonth(int y, int m)
{
	static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (m == 2 && IsLeapYear(y)) return 29;
	return (m >= 1 && m <= 12) ? kDays[m - 1] : 0;
}

inline bool IsValidDate(int y, int m, int d)
{
	return y >= 1970 && y <= 2200 && m >= 1 && m <= 12 &&
	       d >= 1 && d <= DaysInMonth(y, m);
}

// days since 1970-01-01 (Howard Hinnant, public domain)
inline std::int64_t DaysFromCivil(int y, unsigned m, unsigned d)
{
	y -= m <= 2;
	const int era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = static_cast<unsigned>(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return static_cast<std::int64_t>(era) * 146097 + static_cast<int>(doe) - 719468;
}

// 返回 0 表示输入非法（CTP 某些字段可能为空）
inline std::int64_t MakeEpochMs(int action_day, const char *update_time, int millisec)
{
	if (action_day <= 0 || update_time == nullptr) return 0;

	int hh = 0, mm = 0, ss = 0;
	if (std::sscanf(update_time, "%d:%d:%d", &hh, &mm, &ss) != 3) return 0;
	if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) return 0;
	if (millisec < 0 || millisec > 999) return 0;

	const int y = action_day / 10000;
	const int m = (action_day / 100) % 100;
	const int d = action_day % 100;
	if (!IsValidDate(y, m, d)) return 0;

	const std::int64_t days = DaysFromCivil(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
	const std::int64_t sec = days * 86400LL + hh * 3600LL + mm * 60LL + ss;
	return sec * 1000LL - 8LL * 3600 * 1000 + millisec;
}

} // namespace dws
