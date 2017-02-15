#include "tcp_socket.h"

#include <cassert>
#include <cerrno>
#include <algorithm>
#include <system_error>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "loop.h"
#include "stream.h"


namespace siren {

TCPSocket::TCPSocket(Loop *loop)
  : loop_(loop)
{
    assert(loop != nullptr);
    initialize();
}


TCPSocket::TCPSocket(Loop *loop, int fd) noexcept
  : loop_(loop),
    fd_(fd)
{
}


TCPSocket::TCPSocket(TCPSocket &&other) noexcept
  : loop_(other.loop_)
{
    other.move(this);
}


TCPSocket::~TCPSocket()
{
    if (isValid()) {
        finalize();
    }
}


TCPSocket &
TCPSocket::operator=(TCPSocket &&other) noexcept
{
    if (&other != this) {
        finalize();
        loop_ = other.loop_;
        other.move(this);
    }

    return *this;
}


void
TCPSocket::initialize()
{
    fd_ = loop_->socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (fd_ < 0) {
        throw std::system_error(errno, std::system_category(), "socket() failed");
    }
}


void
TCPSocket::finalize()
{
    if (loop_->close(fd_) < 0 && errno != EINTR) {
        throw std::system_error(errno, std::system_category(), "close() failed");
    }
}


void
TCPSocket::move(TCPSocket *other) noexcept
{
    other->fd_ = fd_;
    fd_ = -1;
}


void
TCPSocket::setReuseAddress(bool reuseAddress)
{
    int onOff = reuseAddress;

    if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &onOff, sizeof(onOff)) < 0) {
        throw std::system_error(errno, std::system_category(), "setsockopt() failed");
    }
}


void
TCPSocket::setNoDelay(bool noDelay)
{
    int onOff = noDelay;

    if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &onOff, sizeof(onOff)) < 0) {
        throw std::system_error(errno, std::system_category(), "setsockopt() failed");
    }
}


void
TCPSocket::setLinger(bool linger1, int interval)
{
    linger value;
    value.l_onoff = linger1;
    value.l_linger = interval;

    if (setsockopt(fd_, SOL_SOCKET, SO_LINGER, &value, sizeof(value)) < 0) {
        throw std::system_error(errno, std::system_category(), "setsockopt() failed");
    }
}


void
TCPSocket::setKeepAlive(bool keepAlive, int interval)
{
    int onOff = keepAlive;

    if (setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &onOff, sizeof(onOff)) < 0) {
        throw std::system_error(errno, std::system_category(), "setsockopt() failed");
    }

    if (keepAlive) {
        assert(interval >= 1);

        if (setsockopt(fd_, IPPROTO_TCP, TCP_KEEPIDLE, &interval, sizeof(interval)) < 0) {
            throw std::system_error(errno, std::system_category(), "setsockopt() failed");
        }

        int count = 3;

        if (setsockopt(fd_, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count)) < 0) {
            throw std::system_error(errno, std::system_category(), "setsockopt() failed");
        }

        interval = std::max(interval / count, 1);

        if (setsockopt(fd_, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) < 0) {
            throw std::system_error(errno, std::system_category(), "setsockopt() failed");
        }
    }
}


void
TCPSocket::listen(const IPEndpoint &ipEndpoint, int backlog)
{
    assert(isValid());
    sockaddr_in name;
    name.sin_family = AF_INET;
    name.sin_addr.s_addr = htonl(ipEndpoint.address);
    name.sin_port = htons(ipEndpoint.portNumber);

    if (bind(fd_, reinterpret_cast<sockaddr *>(&name), sizeof(name)) < 0) {
        throw std::system_error(errno, std::system_category(), "bind() failed");
    }

    if (::listen(fd_, backlog) < 0) {
        throw std::system_error(errno, std::system_category(), "listen() failed");
    }
}


TCPSocket
TCPSocket::accept(IPEndpoint *ipEndpoint, int timeout)
{
    assert(isValid());
    sockaddr_in name;
    socklen_t nameSize = sizeof(name);
    int subFD = loop_->accept(fd_, reinterpret_cast<sockaddr *>(&name), &nameSize, timeout);

    if (subFD < 0) {
        throw std::system_error(errno, std::system_category(), "accept() failed");
    }

    if (ipEndpoint != nullptr) {
        *ipEndpoint = IPEndpoint(name);
    }

    return TCPSocket(loop_, subFD);
}


void
TCPSocket::connect(const IPEndpoint &ipEndpoint, int timeout)
{
    assert(isValid());
    sockaddr_in name;
    name.sin_family = AF_INET;
    name.sin_addr.s_addr = htonl(ipEndpoint.address);
    name.sin_port = htons(ipEndpoint.portNumber);

    if (loop_->connect(fd_, reinterpret_cast<sockaddr *>(&name), sizeof(name), timeout) < 0) {
        throw std::system_error(errno, std::system_category(), "connect() failed");
    }
}


IPEndpoint
TCPSocket::getLocalEndpoint() const
{
    assert(isValid());
    sockaddr_in name;
    socklen_t nameSize = sizeof(name);

    if (getsockname(fd_, reinterpret_cast<sockaddr *>(&name), &nameSize) < 0) {
        throw std::system_error(errno, std::system_category(), "getsockname() failed");
    }

    return IPEndpoint(name);
}


IPEndpoint
TCPSocket::getRemoteEndpoint() const
{
    assert(isValid());
    sockaddr_in name;
    socklen_t nameSize = sizeof(name);

    if (getpeername(fd_, reinterpret_cast<sockaddr *>(&name), &nameSize) < 0) {
        throw std::system_error(errno, std::system_category(), "getpeername() failed");
    }

    return IPEndpoint(name);
}


std::size_t
TCPSocket::read(Stream *stream, int timeout)
{
    assert(isValid());
    assert(stream != nullptr);
    void *buffer = stream->getBuffer();
    std::size_t bufferSize = stream->getBufferSize();
    ssize_t numberOfBytes = loop_->read(fd_, buffer, bufferSize, timeout);

    if (numberOfBytes < 0) {
        throw std::system_error(errno, std::system_category(), "read() failed");
    }

    stream->pickData(numberOfBytes);
    return numberOfBytes;
}


std::size_t
TCPSocket::write(Stream *stream, int timeout)
{
    assert(isValid());
    assert(stream != nullptr);
    const void *data = stream->getData();
    std::size_t dataSize = stream->getDataSize();
    ssize_t numberOfBytes = loop_->write(fd_, data, dataSize, timeout);

    if (numberOfBytes < 0) {
        throw std::system_error(errno, std::system_category(), "write() failed");
    }

    stream->dropData(numberOfBytes);
    return numberOfBytes;
}


void
TCPSocket::closeRead()
{
    assert(isValid());

    if (shutdown(fd_, SHUT_RD) < 0) {
        throw std::system_error(errno, std::system_category(), "shutdown() failed");
    }
}


void
TCPSocket::closeWrite()
{
    assert(isValid());

    if (shutdown(fd_, SHUT_WR) < 0) {
        throw std::system_error(errno, std::system_category(), "shutdown() failed");
    }
}

} // namespace siren