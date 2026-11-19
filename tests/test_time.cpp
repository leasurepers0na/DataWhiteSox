#include "time_util.hpp"

#include <cstdio>

int main()
{
	int fails = 0;
	auto check = [&](std::int64_t got, std::int64_t want, const char *label) {
		if (got != want) {
			std::printf("FAIL %-28s got=%lld want=%lld\n", label,
			            static_cast<long long>(got), static_cast<long long>(want));
			++fails;
		} else {
			std::printf("PASS %-28s %lld\n", label, static_cast<long long>(got));
		}
	};

	check(dws::MakeEpochMs(20250102, "21:00:00", 500), 1735822800500LL, "2025-01-02 21:00:00.500");
	check(dws::MakeEpochMs(20250102, "09:00:00", 0), 1735779600000LL, "2025-01-02 09:00:00.000");
	check(dws::MakeEpochMs(20240229, "21:00:00", 0), 1709211600000LL, "2024-02-29 21:00:00.000");
	check(dws::MakeEpochMs(20250101, "00:00:00", 0), 1735660800000LL, "2025-01-01 00:00:00.000");

	// 非法输入必须返回 0
	check(dws::MakeEpochMs(20250229, "09:00:00", 0), 0, "invalid 2025-02-29");
	check(dws::MakeEpochMs(20250102, "24:00:00", 0), 0, "invalid 24:00:00");
	check(dws::MakeEpochMs(20250102, "09:00:00", 1000), 0, "invalid millisec 1000");
	check(dws::MakeEpochMs(0, "09:00:00", 0), 0, "invalid action_day 0");

	std::printf(fails == 0 ? "ALL PASS\n" : "%d FAIL\n", fails);
	return fails == 0 ? 0 : 1;
}
