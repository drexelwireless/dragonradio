// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "logging.hh"
#include "util/capabilities.hh"
#include "util/net.hh"

#include <sys/types.h>
#include <sys/ioctl.h>
#include <net/route.h>
#include <netinet/if_ether.h>

void parseMAC(const std::string &s, struct sockaddr *addr)
{
    addr->sa_family = ARPHRD_ETHER;

    if (sscanf(s.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               (unsigned char*) &(addr->sa_data[0]),
               (unsigned char*) &(addr->sa_data[1]),
               (unsigned char*) &(addr->sa_data[2]),
               (unsigned char*) &(addr->sa_data[3]),
               (unsigned char*) &(addr->sa_data[4]),
               (unsigned char*) &(addr->sa_data[5])) != 6)
        throw std::domain_error("Illegally formatted MAC address");
}

void parseIP(const std::string &s, struct sockaddr *addr)
{
    addr->sa_family = AF_INET;

    if (inet_pton(AF_INET, s.c_str(), &((struct sockaddr_in*) addr)->sin_addr) == 0)
        throw std::domain_error("Illegally formatted IP address");
}

void addStaticARPEntry(const std::optional<std::string> &dev,
                       const std::string &ipaddr,
                       const std::string &macaddr)
{
    RaiseCaps     caps({CAP_NET_ADMIN});
    struct arpreq req = {{0}};

    if (dev)
        strncpy(req.arp_dev, dev->c_str(), sizeof(req.arp_dev)-1);

    parseIP(ipaddr, &req.arp_pa);
    parseMAC(macaddr, &req.arp_ha);

    req.arp_flags = ATF_PERM | ATF_COM;

    Socket sockfd(AF_INET, SOCK_DGRAM, 0);

    if (ioctl(sockfd, SIOCSARP, &req) < 0)
        throw std::runtime_error(strerror(errno));
}

void deleteARPEntry(const std::optional<std::string> &dev,
                    const std::string &ipaddr)
{
    RaiseCaps     caps({CAP_NET_ADMIN});
    struct arpreq req = {{0}};

    if (dev)
        strncpy(req.arp_dev, dev->c_str(), sizeof(req.arp_dev)-1);

    parseIP(ipaddr, &req.arp_pa);

    Socket sockfd(AF_INET, SOCK_DGRAM, 0);

    if (ioctl(sockfd, SIOCDARP, &req) < 0)
        throw std::runtime_error(strerror(errno));
}

void addRoute(const std::string &dst,
              const std::string &mask,
              const std::string &gateway)
{
    RaiseCaps      caps({CAP_NET_ADMIN});
    struct rtentry route = {0};

    parseIP(dst, &route.rt_dst);
    parseIP(gateway, &route.rt_gateway);
    parseIP(mask, &route.rt_genmask);
    route.rt_flags = RTF_UP | RTF_GATEWAY;

    Socket sockfd(AF_INET, SOCK_DGRAM, 0);

    if (ioctl(sockfd, SIOCADDRT, &route) < 0)
        throw std::runtime_error(strerror(errno));
}

void deleteRoute(const std::string &dst,
                 const std::string &mask)
{
    RaiseCaps          caps({CAP_NET_ADMIN});
    struct rtentry     route = {0};

    parseIP(dst, &route.rt_dst);
    parseIP(mask, &route.rt_genmask);

    Socket sockfd(AF_INET, SOCK_DGRAM, 0);

    if (ioctl(sockfd, SIOCDELRT, &route) < 0)
        throw std::runtime_error(strerror(errno));
}
