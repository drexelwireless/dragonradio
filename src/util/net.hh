// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef UTIL_NET_HH_
#define UTIL_NET_HH_

#include <sys/types.h>
#include <string.h>

#include "Packet.hh"

/** @brief Compute broadcast address from address and netmask  */
inline uint32_t mkBroadcastAddress(uint32_t addr, uint32_t netmask)
{
    return (addr & netmask) | (0xffffffff & ~netmask);
}

/** @brief Determine whether or not an Ethernet address is a broadcast address */
inline bool isEthernetBroadcast(const u_char *host)
{
    return memcmp(host, "\xff\xff\xff\xff\xff\xff", 6) == 0;
}

/** @brief Parse a MAC address */
struct sockaddr parseMAC(const std::string &s);

/** @brief Parse an IP address */
struct sockaddr parseIP(const std::string &s);

class Socket {
public:
    Socket() : fd_(0)
    {
    }

    explicit Socket(int fd) : fd_(fd)
    {
    }

    Socket(int domain, int type, int protocol)
    {
        if ((fd_ = socket(domain, type, protocol)) < 0)
            throw std::runtime_error(strerror(errno));
    }

    Socket(const Socket&) = delete;

    Socket(Socket&& other)
    {
        fd_ = other.fd_;
        other.fd_ = 0;
    }

    ~Socket()
    {
        if (fd_ != 0)
            close(fd_);
    }

    Socket& operator =(Socket& other)
    {
        fd_ = other.fd_;
        other.fd_ = 0;

        return *this;
    }

    Socket& operator =(Socket&& other)
    {
        fd_ = other.fd_;
        other.fd_ = 0;

        return *this;
    }

    operator int()
    {
        return fd_;
    }

protected:
    int fd_;
};

#endif /* UTIL_NET_HH_ */
