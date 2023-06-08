#include <tracepoint/TracingSession.h>

#include <unistd.h>
#include <sys/mman.h>

using namespace tracepoint_control;

TracingSession::unique_fd::~unique_fd()
{
    reset(-1);
}

TracingSession::unique_fd::unique_fd() noexcept
    : m_fd(-1)
{
    return;
}

TracingSession::unique_fd::unique_fd(int fd) noexcept
    : m_fd(fd)
{
    return;
}

TracingSession::unique_fd::unique_fd(unique_fd&& other) noexcept
    : m_fd(other.m_fd)
{
    other.m_fd = -1;
}

TracingSession::unique_fd&
TracingSession::unique_fd::operator=(unique_fd&& other) noexcept
{
    int fd = other.m_fd;
    other.m_fd = -1;
    reset(fd);
    return *this;
}

TracingSession::unique_fd::operator bool() const noexcept
{
    return m_fd != -1;
}

void
TracingSession::unique_fd::reset() noexcept
{
    reset(-1);
}

void
TracingSession::unique_fd::reset(int fd) noexcept
{
    if (m_fd != -1)
    {
        close(m_fd);
    }
    m_fd = fd;
}

int
TracingSession::unique_fd::get() const noexcept
{
    return m_fd;
}

TracingSession::unique_mmap::~unique_mmap()
{
    reset(MAP_FAILED, 0);
}

TracingSession::unique_mmap::unique_mmap() noexcept
    : m_addr(MAP_FAILED)
    , m_size(0)
{
    return;
}

TracingSession::unique_mmap::unique_mmap(void* addr, size_t size) noexcept
    : m_addr(addr)
    , m_size(size)
{
    return;
}

TracingSession::unique_mmap::unique_mmap(unique_mmap&& other) noexcept
    : m_addr(other.m_addr)
    , m_size(other.m_size)
{
    other.m_addr = MAP_FAILED;
    other.m_size = 0;
}

TracingSession::unique_mmap&
TracingSession::unique_mmap::operator=(unique_mmap&& other) noexcept
{
    void* addr = other.m_addr;
    size_t size = other.m_size;
    other.m_addr = MAP_FAILED;
    other.m_size = 0;
    reset(addr, size);
    return *this;
}

TracingSession::unique_mmap::operator bool() const noexcept
{
    return m_addr != MAP_FAILED;
}

void
TracingSession::unique_mmap::reset() noexcept
{
    reset(MAP_FAILED, 0);
}

void
TracingSession::unique_mmap::reset(void* addr, size_t size) noexcept
{
    if (m_addr != MAP_FAILED)
    {
        munmap(m_addr, m_size);
    }
    m_addr = addr;
    m_size = size;
}

void*
TracingSession::unique_mmap::get() const noexcept
{
    return m_addr;
}

size_t
TracingSession::unique_mmap::get_size() const noexcept
{
    return m_size;
}
