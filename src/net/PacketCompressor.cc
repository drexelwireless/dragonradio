#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <functional>

#include "Logger.hh"
#include "net/PacketCompressor.hh"

using namespace std::placeholders;

PacketCompressor::PacketCompressor()
  : net_in(*this, nullptr, nullptr, std::bind(&PacketCompressor::netPush, this, _1))
  , net_out(*this, nullptr, nullptr)
  , radio_in(*this, nullptr, nullptr, std::bind(&PacketCompressor::radioPush, this, _1))
  , radio_out(*this, nullptr, nullptr)
{
}

void PacketCompressor::netPush(std::shared_ptr<NetPacket> &&pkt)
{
    buffer<unsigned char> buf(pkt->size());

    memcpy(buf.data(), pkt->data(), pkt->size());
    pkt->swap(buf);

    net_out.push(std::move(pkt));
}

void PacketCompressor::radioPush(std::shared_ptr<RadioPacket> &&pkt)
{
    buffer<unsigned char> buf(pkt->size());

    memcpy(buf.data(), pkt->data(), pkt->size());
    pkt->swap(buf);

    radio_out.push(std::move(pkt));
}
