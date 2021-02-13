// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "util/net.hh"

#include <sys/types.h>
#include <sys/ioctl.h>
#include <netinet/if_ether.h>

struct sockaddr parseMAC(const std::string &s)
{
    struct sockaddr addr;

    addr.sa_family = ARPHRD_ETHER;

    if (sscanf(s.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               (unsigned char*) &addr.sa_data[0],
               (unsigned char*) &addr.sa_data[1],
               (unsigned char*) &addr.sa_data[2],
               (unsigned char*) &addr.sa_data[3],
               (unsigned char*) &addr.sa_data[4],
               (unsigned char*) &addr.sa_data[5]) != 6)
        throw std::domain_error("Illegally formatted MAC address");

    return addr;
}

struct sockaddr parseIP(const std::string &s)
{
    struct sockaddr addr;

    addr.sa_family = AF_INET;

    if (inet_pton(AF_INET, s.c_str(), &((struct sockaddr_in*) &addr)->sin_addr) == 0)
        throw std::domain_error("Illegally formatted IP address");

    return addr;
}
