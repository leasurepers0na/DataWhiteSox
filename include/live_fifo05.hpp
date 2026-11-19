#pragma once
// 0.5s 子 bar：184B FutureData 的 DWS 写入端。
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <unistd.h>

namespace dws {

struct FutureData05
{
	std::int32_t code = 0;
	std::int32_t contract_month = 0;
	std::uint64_t time = 0;

	float open = 0, high = 0, close = 0, low = 0;
	float vwap = 0, twap = 0;

	std::int32_t volume = 0;
	double turnover = 0;
	std::int32_t position = 0;
	std::int32_t tradingdate = 0;

	std::int32_t up_kc = 0, down_kc = 0, up_pc = 0, down_pc = 0, oi_change = 0;

	float s5 = 0, s4 = 0, s3 = 0, s2 = 0, s1 = 0;
	float b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0;

	std::int32_t sv5 = 0, sv4 = 0, sv3 = 0, sv2 = 0, sv1 = 0;
	std::int32_t bv1 = 0, bv2 = 0, bv3 = 0, bv4 = 0, bv5 = 0;

	std::int16_t fill_flag = 0;
	double price_adj = 1.0;
	std::int32_t volume_all = 0;
	std::int32_t position_all = 0;
};

static_assert(sizeof(FutureData05) == 184, "FutureData05 must be 184 bytes");

class LiveFifo05
{
public:
	explicit LiveFifo05(std::string path) : m_path(std::move(path)) {}
	~LiveFifo05() { Close(); }

	bool Enabled() const { return !m_path.empty(); }

	void Write(const FutureData05 &b)
	{
		if (m_path.empty()) return;
		if (m_fd < 0 && !Open()) return;
		const char *p = reinterpret_cast<const char *>(&b);
		size_t off = 0;
		while (off < sizeof(b))
		{
			const ssize_t n = ::write(m_fd, p + off, sizeof(b) - off);
			if (n > 0)
			{
				off += static_cast<size_t>(n);
				continue;
			}
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			{
				// 0.5s 速率高，管道满时等可写，不能丢记录。
				struct pollfd pfd;
				pfd.fd = m_fd;
				pfd.events = POLLOUT;
				pfd.revents = 0;
				::poll(&pfd, 1, 100);
				continue;
			}
			std::fprintf(stderr, "[DWS][fifo05] write failed: %s\n", std::strerror(errno));
			Close();
			return;
		}
	}

	void Close()
	{
		if (m_fd >= 0)
		{
			::close(m_fd);
			m_fd = -1;
		}
	}

private:
	bool Open()
	{
		m_fd = ::open(m_path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
		if (m_fd < 0)
		{
			if (!m_warned)
			{
				std::fprintf(stderr, "[DWS][fifo05] no reader on %s: %s\n",
				             m_path.c_str(), std::strerror(errno));
				m_warned = true;
			}
			return false;
		}
		m_warned = false;
		return true;
	}

	std::string m_path;
	int m_fd = -1;
	bool m_warned = false;
};

} // namespace dws
