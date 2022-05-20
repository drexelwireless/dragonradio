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
void parseMAC(const std::string &s, struct sockaddr *addr);

/** @brief Parse an IP address */
void parseIP(const std::string &s, struct sockaddr *addr);

/** @brief Add a static ARP table entry */
void addStaticARPEntry(const std::optional<std::string> &dev, const std::string &ipaddr, const std::string &macaddr);

/** @brief Delete a static ARP table entry */
void deleteARPEntry(const std::optional<std::string> &dev, const std::string &ipaddr);

/** @brief Add an IP route */
void addRoute(const std::string &dst, const std::string &mask, const std::string &gateway);

/** @brief Delete an IP route */
void deleteRoute(const std::string &dst, const std::string &mask);

/** @brief A file descriptor */
class Fd {
public:
    Fd() : fd_(-1)
    {
    }

    explicit Fd(int fd) : fd_(fd)
    {
    }

    Fd(const Fd&) = delete;

    Fd(Fd&& other)
    {
        fd_ = other.fd_;
        other.fd_ = -1;
    }

    virtual ~Fd()
    {
        if (fd_ >= 0)
            close();
    }

    Fd& operator =(const Fd&) = delete;

    Fd& operator =(Fd&& other)
    {
        if (fd_ >= 0)
            close();

        fd_ = other.fd_;
        other.fd_ = -1;

        return *this;
    }

    Fd& operator =(int fd)
    {
        if (fd_ >= 0)
            close();

        fd_ = fd;

        return *this;
    }

    /** @brief The file descriptor */
    operator int() noexcept
    {
        return fd_;
    }

    /** @brief Release file descriptor
     * @return The file descriptor
     */
    int release() noexcept
    {
        int fd = fd_;

        fd_ = -1;
        return fd;
    }

    /** @brief Close file descriptor */
    void close()
    {
        if (::close(fd_) != 0)
            throw std::runtime_error(strerror(errno));

        fd_ = -1;
    }

protected:
    /** @brief File descriptor */
    int fd_;
};

/** @brief A socket */
class Socket : public Fd {
public:
    Socket() : Fd()
    {
    }

    explicit Socket(int fd) : Fd(fd)
    {
    }

    Socket(int domain, int type, int protocol)
    {
        if ((fd_ = socket(domain, type, protocol)) < 0)
            throw std::runtime_error(strerror(errno));
    }
};

#endif /* UTIL_NET_HH_ */
