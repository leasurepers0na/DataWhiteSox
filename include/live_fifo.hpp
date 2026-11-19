#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// 可选实时 FIFO：把每根 1min Bar1Min 原样写 96B 到命名管道。
// 供 FR real 模式后续读取；不配置 live_fifo 时完全不参与。
// ─────────────────────────────────────────────────────────────────────────────
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "pipeline.hpp"

namespace dws {

class LiveFifo
{
public:
	explicit LiveFifo(std::string path) : m_path(std::move(path)) {}
	~LiveFifo() { Close(); }

	bool Enabled() const { return !m_path.empty(); }

	void Write(const Bar1Min &b)
	{
		if (m_path.empty()) return;
		if (m_fd < 0 && !Open()) return;
		const ssize_t n = ::write(m_fd, &b, sizeof(b));
		if (n != static_cast<ssize_t>(sizeof(b)))
		{
			std::fprintf(stderr, "[DWS][fifo] write failed: %s\n", std::strerror(errno));
			Close();
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
				std::fprintf(stderr, "[DWS][fifo] no reader on %s: %s\n",
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
