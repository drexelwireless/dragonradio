#ifndef MGEN_HH_
#define MGEN_HH_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <netinet/in.h>

#include <fstream>
#include <functional>
#include <vector>
#include <stdexcept>

#include "datetime.hh"
#include "Tok.hh"

struct Send {
    int64_t timestamp;
    uint16_t flow;
    uint32_t seq;
    uint32_t frag;
    uint32_t tos;
    uint16_t src_port;
    in_addr_t dest_ip;
    uint16_t dest_port;
    uint32_t size;
};

struct Recv {
    int64_t timestamp;
    uint16_t flow;
    uint32_t seq;
    uint32_t frag;
    uint32_t tos;
    in_addr_t src_ip;
    uint16_t src_port;
    in_addr_t dest_ip;
    uint16_t dest_port;
    int64_t sent;
    uint32_t size;
};

std::vector<Send> parseSend(const char *path);

std::vector<Recv> parseRecv(const char *path);

#endif /* MGEN_HH_ */