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
#include "net/Net.hh"
#include "net/PacketCompressor.hh"

#define DEBUG 0

#if DEBUG
#define logCompress(...) logEvent(__VA_ARGS__)
#else /* !DEBUG */
#define logCompress(...)
#endif /* !DEBUG */

using namespace std::placeholders;

uint16_t ip_checksum_update(uint16_t cksum, const void *data_, size_t count)
{
    char     *data = (char*) data_;
    uint32_t acc = cksum;

    for (size_t i = 0; i + 1 < count; i += 2) {
        uint16_t word;

        memcpy(&word, data + i, 2);
        acc += ntohs(word);

        if (acc > 0xffff)
            acc -= 0xffff;
    }

    if (count & 1) {
        uint16_t word = 0;

        memcpy(&word, data + count - 1, 1);
        acc += ntohs(word);

        if (acc > 0xffff)
            acc -= 0xffff;
    }

    return acc;
}

uint16_t ip_checksum(const void *data, size_t count)
{
    return htons(~ip_checksum_update(0, data, count));
}

uint16_t udp_checksum(const struct ip *iph, const struct udphdr *udph, size_t udp_len)
{
    uint16_t cksum = 0;
    uint16_t pseudo_proto = ntohs(iph->ip_p);

    cksum = ip_checksum_update(cksum, &iph->ip_src.s_addr, sizeof(in_addr_t));
    cksum = ip_checksum_update(cksum, &iph->ip_dst.s_addr, sizeof(in_addr_t));
    cksum = ip_checksum_update(cksum, &pseudo_proto, sizeof(uint16_t));
    cksum = ip_checksum_update(cksum, &udph->uh_ulen, sizeof(u_int16_t));
    cksum = ip_checksum_update(cksum, udph, udp_len);
    return htons(~cksum);
}

/*****************************************************************/
/*                                                               */
/* CRC LOOKUP TABLE                                              */
/* ================                                              */
/* The following CRC lookup table was generated automagically    */
/* by the Rocksoft^tm Model CRC Algorithm Table Generation       */
/* Program V1.0 using the following model parameters:            */
/*                                                               */
/*    Width   : 4 bytes.                                         */
/*    Poly    : 0x04C11DB7L                                      */
/*    Reverse : TRUE.                                            */
/*                                                               */
/* For more information on the Rocksoft^tm Model CRC Algorithm,  */
/* see the document titled "A Painless Guide to CRC Error        */
/* Detection Algorithms" by Ross Williams                        */
/* (ross@guest.adelaide.edu.au.). This document is likely to be  */
/* in the FTP archive "ftp.adelaide.edu.au/pub/rocksoft".        */
/*                                                               */
/*****************************************************************/

const uint32_t CRC32_TABLE[256] =
{
 0x00000000L, 0x77073096L, 0xEE0E612CL, 0x990951BAL,
 0x076DC419L, 0x706AF48FL, 0xE963A535L, 0x9E6495A3L,
 0x0EDB8832L, 0x79DCB8A4L, 0xE0D5E91EL, 0x97D2D988L,
 0x09B64C2BL, 0x7EB17CBDL, 0xE7B82D07L, 0x90BF1D91L,
 0x1DB71064L, 0x6AB020F2L, 0xF3B97148L, 0x84BE41DEL,
 0x1ADAD47DL, 0x6DDDE4EBL, 0xF4D4B551L, 0x83D385C7L,
 0x136C9856L, 0x646BA8C0L, 0xFD62F97AL, 0x8A65C9ECL,
 0x14015C4FL, 0x63066CD9L, 0xFA0F3D63L, 0x8D080DF5L,
 0x3B6E20C8L, 0x4C69105EL, 0xD56041E4L, 0xA2677172L,
 0x3C03E4D1L, 0x4B04D447L, 0xD20D85FDL, 0xA50AB56BL,
 0x35B5A8FAL, 0x42B2986CL, 0xDBBBC9D6L, 0xACBCF940L,
 0x32D86CE3L, 0x45DF5C75L, 0xDCD60DCFL, 0xABD13D59L,
 0x26D930ACL, 0x51DE003AL, 0xC8D75180L, 0xBFD06116L,
 0x21B4F4B5L, 0x56B3C423L, 0xCFBA9599L, 0xB8BDA50FL,
 0x2802B89EL, 0x5F058808L, 0xC60CD9B2L, 0xB10BE924L,
 0x2F6F7C87L, 0x58684C11L, 0xC1611DABL, 0xB6662D3DL,
 0x76DC4190L, 0x01DB7106L, 0x98D220BCL, 0xEFD5102AL,
 0x71B18589L, 0x06B6B51FL, 0x9FBFE4A5L, 0xE8B8D433L,
 0x7807C9A2L, 0x0F00F934L, 0x9609A88EL, 0xE10E9818L,
 0x7F6A0DBBL, 0x086D3D2DL, 0x91646C97L, 0xE6635C01L,
 0x6B6B51F4L, 0x1C6C6162L, 0x856530D8L, 0xF262004EL,
 0x6C0695EDL, 0x1B01A57BL, 0x8208F4C1L, 0xF50FC457L,
 0x65B0D9C6L, 0x12B7E950L, 0x8BBEB8EAL, 0xFCB9887CL,
 0x62DD1DDFL, 0x15DA2D49L, 0x8CD37CF3L, 0xFBD44C65L,
 0x4DB26158L, 0x3AB551CEL, 0xA3BC0074L, 0xD4BB30E2L,
 0x4ADFA541L, 0x3DD895D7L, 0xA4D1C46DL, 0xD3D6F4FBL,
 0x4369E96AL, 0x346ED9FCL, 0xAD678846L, 0xDA60B8D0L,
 0x44042D73L, 0x33031DE5L, 0xAA0A4C5FL, 0xDD0D7CC9L,
 0x5005713CL, 0x270241AAL, 0xBE0B1010L, 0xC90C2086L,
 0x5768B525L, 0x206F85B3L, 0xB966D409L, 0xCE61E49FL,
 0x5EDEF90EL, 0x29D9C998L, 0xB0D09822L, 0xC7D7A8B4L,
 0x59B33D17L, 0x2EB40D81L, 0xB7BD5C3BL, 0xC0BA6CADL,
 0xEDB88320L, 0x9ABFB3B6L, 0x03B6E20CL, 0x74B1D29AL,
 0xEAD54739L, 0x9DD277AFL, 0x04DB2615L, 0x73DC1683L,
 0xE3630B12L, 0x94643B84L, 0x0D6D6A3EL, 0x7A6A5AA8L,
 0xE40ECF0BL, 0x9309FF9DL, 0x0A00AE27L, 0x7D079EB1L,
 0xF00F9344L, 0x8708A3D2L, 0x1E01F268L, 0x6906C2FEL,
 0xF762575DL, 0x806567CBL, 0x196C3671L, 0x6E6B06E7L,
 0xFED41B76L, 0x89D32BE0L, 0x10DA7A5AL, 0x67DD4ACCL,
 0xF9B9DF6FL, 0x8EBEEFF9L, 0x17B7BE43L, 0x60B08ED5L,
 0xD6D6A3E8L, 0xA1D1937EL, 0x38D8C2C4L, 0x4FDFF252L,
 0xD1BB67F1L, 0xA6BC5767L, 0x3FB506DDL, 0x48B2364BL,
 0xD80D2BDAL, 0xAF0A1B4CL, 0x36034AF6L, 0x41047A60L,
 0xDF60EFC3L, 0xA867DF55L, 0x316E8EEFL, 0x4669BE79L,
 0xCB61B38CL, 0xBC66831AL, 0x256FD2A0L, 0x5268E236L,
 0xCC0C7795L, 0xBB0B4703L, 0x220216B9L, 0x5505262FL,
 0xC5BA3BBEL, 0xB2BD0B28L, 0x2BB45A92L, 0x5CB36A04L,
 0xC2D7FFA7L, 0xB5D0CF31L, 0x2CD99E8BL, 0x5BDEAE1DL,
 0x9B64C2B0L, 0xEC63F226L, 0x756AA39CL, 0x026D930AL,
 0x9C0906A9L, 0xEB0E363FL, 0x72076785L, 0x05005713L,
 0x95BF4A82L, 0xE2B87A14L, 0x7BB12BAEL, 0x0CB61B38L,
 0x92D28E9BL, 0xE5D5BE0DL, 0x7CDCEFB7L, 0x0BDBDF21L,
 0x86D3D2D4L, 0xF1D4E242L, 0x68DDB3F8L, 0x1FDA836EL,
 0x81BE16CDL, 0xF6B9265BL, 0x6FB077E1L, 0x18B74777L,
 0x88085AE6L, 0xFF0F6A70L, 0x66063BCAL, 0x11010B5CL,
 0x8F659EFFL, 0xF862AE69L, 0x616BFFD3L, 0x166CCF45L,
 0xA00AE278L, 0xD70DD2EEL, 0x4E048354L, 0x3903B3C2L,
 0xA7672661L, 0xD06016F7L, 0x4969474DL, 0x3E6E77DBL,
 0xAED16A4AL, 0xD9D65ADCL, 0x40DF0B66L, 0x37D83BF0L,
 0xA9BCAE53L, 0xDEBB9EC5L, 0x47B2CF7FL, 0x30B5FFE9L,
 0xBDBDF21CL, 0xCABAC28AL, 0x53B39330L, 0x24B4A3A6L,
 0xBAD03605L, 0xCDD70693L, 0x54DE5729L, 0x23D967BFL,
 0xB3667A2EL, 0xC4614AB8L, 0x5D681B02L, 0x2A6F2B94L,
 0xB40BBE37L, 0xC30C8EA1L, 0x5A05DF1BL, 0x2D02EF8DL
};

/** @brief Initial value */
const uint32_t CRC32_XINIT = 0xFFFFFFFFL;

/** @brief Final xor value */
const uint32_t CRC32_XOROT = 0xFFFFFFFFL;

uint32_t crc32(const void *data_, size_t count)
{
    const uint8_t *data = (const uint8_t*) data_;
    uint32_t      result = CRC32_XINIT;

    for (size_t i = 0; i < count; i++)
        result = CRC32_TABLE[(result ^ *data++) & 0xFFL] ^ (result >> 8);

    return (result ^ CRC32_XOROT);
}

PacketCompressor::PacketCompressor(bool enabled)
  : net_in(*this, nullptr, nullptr, std::bind(&PacketCompressor::netPush, this, _1))
  , net_out(*this, nullptr, nullptr)
  , radio_in(*this, nullptr, nullptr, std::bind(&PacketCompressor::radioPush, this, _1))
  , radio_out(*this, nullptr, nullptr)
  , enabled_(enabled)
{
    struct in_addr in;

    assert(inet_aton(kIntIPNet, &in) != 0);
    int_net_ = ntohl(in.s_addr);

    assert(inet_aton(kIntIPNetmask, &in) != 0);
    int_netmask_ = ntohl(in.s_addr);

    assert(inet_aton(kExtIPNet, &in) != 0);
    ext_net_ = ntohl(in.s_addr);

    assert(inet_aton(kExtIPNetmask, &in) != 0);
    ext_netmask_ = ntohl(in.s_addr);
}

void PacketCompressor::netPush(std::shared_ptr<NetPacket> &&pkt)
{
    if (enabled_)
        compress(*pkt);

    net_out.push(std::move(pkt));
}

void PacketCompressor::radioPush(std::shared_ptr<RadioPacket> &&pkt)
{
    if (pkt->hdr.flags.compressed)
        decompress(*pkt);

    radio_out.push(std::move(pkt));
}

const u_int8_t kExpectedTTL = 254;

const int32_t kExpectedLatitude = htonl((999+180)*60000);
const int32_t kExpectedLongitude = htonl((999+180)*60000);
const int32_t kExpectedAltitude = htonl(static_cast<int32_t>(-999));

class CompressionBuffer : public buffer<unsigned char>
{
public:
    CompressionBuffer() = delete;

    CompressionBuffer(NetPacket &pkt_)
      : buffer(pkt_.size() + sizeof(CompressionFlags))
      , pkt(pkt_)
      , flags({0})
      , inoff(0)
      , outoff(0)
    {
        // We haven't done any compression yet
        flags.type = kUncompressed;

        // Copy extended header
        copyBytesOut(sizeof(ExtendedHeader));

        // Reserve room for flags
        outoff += sizeof(CompressionFlags);
    }

    void copyBytesOut(size_t count)
    {
        assert(size() >= outoff + count);
        memcpy(data() + outoff, pkt.data() + inoff, count);
        inoff += count;
        outoff += count;
    }

    template<class T>
    void copyOut(const T &val)
    {
        assert(size() >= outoff + sizeof(T));
        memcpy(data() + outoff, &val, sizeof(T));
        outoff += sizeof(T);
    }

    void flush(void)
    {
        ssize_t saved = inoff - outoff;

        logCompress("%ld bytes saved", saved);

        copyBytesOut(pkt.size() - inoff);
        memcpy(data() + sizeof(ExtendedHeader), &flags, sizeof(CompressionFlags));
        pkt.swap(*this);
        pkt.ehdr().data_len = static_cast<ssize_t>(pkt.ehdr().data_len) - saved;
        pkt.resize(outoff);
    }

    NetPacket &pkt;

    CompressionFlags flags;

    size_t inoff;

    size_t outoff;
};

void PacketCompressor::compress(NetPacket &pkt)
{
    CompressionBuffer buf(pkt);

    // Test for compressible Ethernet header
    {
        ether_addr         eaddr = { { 0xc6, 0xff, 0xff, 0xff, 0xff, 0x00 } };
        const ether_header *ehdr = pkt.getEthernetHdr();

        if (ehdr == nullptr)
            return;

        // Test source address
        eaddr.ether_addr_octet[5] = pkt.hdr.curhop;

        if (memcmp(eaddr.ether_addr_octet,
                   ehdr->ether_shost,
                   sizeof(ether_addr)) != 0) {
            logCompress("Ethernet source doesn't match");
            return;
        }

        // Test destination address
        eaddr.ether_addr_octet[5] = pkt.hdr.nexthop;

        if (memcmp(eaddr.ether_addr_octet,
                   ehdr->ether_dhost,
                   sizeof(ether_addr)) != 0) {
            logCompress("Ethernet destination doesn't match");
            return;
        }

        // Make sure this is an IP packet
        if (ntohs(ehdr->ether_type) != ETHERTYPE_IP) {
            logCompress("Not IP");
            return;
        }
    }

    // Compress Ethernet header
    buf.inoff += sizeof(struct ether_header);
    buf.flags.type = kEthernet;

    pkt.hdr.flags.compressed = 1;

    // Get IP header
    {
        const struct ip *iph = pkt.getIPHdr();
        size_t          ip_len = sizeof(ExtendedHeader) + pkt.ehdr().data_len - buf.inoff;

        // Must be IPv4
        if (iph->ip_v != 4) {
            logCompress("Not IPv4");
            goto done;
        }

        // Must have standard 20 byte (5 32-bit word) header
        if (iph->ip_hl != 5) {
            logCompress("IP header length not 20 bytes");
            goto done;
        }

        // Length must match packet data length
        if (ntohs(iph->ip_len) != ip_len) {
            logCompress("IP length doesn't match");
            goto done;
        }

        // Must have don't fragment bit set
        if (ntohs(iph->ip_off) != IP_DF) {
            logCompress("IP offset not \"don't fragment\"");
            goto done;
        }

        // Must have valid checksum
        uint16_t cksum = ip_checksum(pkt.data() + buf.inoff, 4*iph->ip_hl);

        if (iph->ip_sum == 0xffffff || cksum != 0) {
            logCompress("Bad IP checksum");
            goto done;
        }

        // Compress IP header
        buf.copyOut(iph->ip_tos);
        buf.copyOut(iph->ip_id);
        if (iph->ip_ttl != kExpectedTTL) {
            buf.flags.read_ttl = 1;
            buf.copyOut(iph->ip_ttl);
        }

        {
            in_addr_t saddr = ntohl(iph->ip_src.s_addr);
            in_addr_t daddr = ntohl(iph->ip_dst.s_addr);

            // Internal network has the form 10.10.10.<NODE>
            if ((saddr & int_netmask_) == int_net_ &&
                (saddr & 0xff) == pkt.ehdr().src &&
                (daddr & int_netmask_) == int_net_ &&
                (daddr & 0xff) == pkt.ehdr().dest) {
                buf.flags.ipaddr_type = kIPInternal;
            // External network has the form 192.168.(100 + <NODE>).N
            } else if ((saddr & ext_netmask_) == int_net_ &&
                       ((saddr >> 8) & 0xff) == 100u + pkt.ehdr().src &&
                       (daddr & int_netmask_) == int_net_ &&
                       ((daddr >> 8) & 0xff) == 100u + pkt.ehdr().dest) {
                buf.copyOut(static_cast<uint8_t>(saddr & 0xff));
                buf.copyOut(static_cast<uint8_t>(daddr & 0xff));
                buf.flags.ipaddr_type = kIPExternal;
            } else {
                buf.copyOut(iph->ip_src.s_addr);
                buf.copyOut(iph->ip_dst.s_addr);
                buf.flags.ipaddr_type = kIPUncompressed;
            }
        }

        buf.inoff += sizeof(struct ip);
        buf.flags.type = kIP;

        //Handle UDP
        if (iph->ip_p == IPPROTO_UDP) {
            const struct udphdr *udph = pkt.getUDPHdr();
            size_t              udp_len = sizeof(ExtendedHeader) + pkt.ehdr().data_len - buf.inoff;

            // Length must match packet data length
            if (ntohs(udph->uh_ulen) != udp_len) {
                logCompress("UDP length doesn't match");
                goto ip_done;
            }

            // Checksum must match
            {
                uint16_t cksum = udp_checksum(iph, udph, udp_len);

                if (udph->uh_sum == 0 || cksum != 0) {
                    logCompress("Bad UDP checksum: 0x%x 0x%x", udph->uh_sum, cksum);
                    goto ip_done;
                }
            }

            // Compress UDP header
            buf.copyOut(udph->uh_sport);
            buf.copyOut(udph->uh_dport);

            buf.inoff += sizeof(struct udphdr);
            buf.flags.type = kUDP;

            // Handle MGEN
            const struct mgenhdr *mgenh = pkt.getMGENHdr();

            if (mgenh == nullptr)
                goto done;

            // Length must match packet data length
            size_t  mgen_len = sizeof(ExtendedHeader) + pkt.ehdr().data_len - buf.inoff;
            ssize_t mgen_padlen = 0;

            if (mgenh->version == MGEN_VERSION)
                mgen_padlen = mgen_len - (sizeof(struct mgenhdr) + sizeof(struct mgenstdaddr) + sizeof(struct mgenrest)) - 4;
            else /* mgenh->version == DARPA_MGEN_VERSION */
                mgen_padlen = mgen_len - (sizeof(struct darpa_mgenhdr) + sizeof(struct mgenstdaddr) + sizeof(struct darpa_mgenrest)) - 4;

            if (mgen_padlen < 0) {
                logCompress("MGEN no room for CRC32");
                goto done;
            }

            if (ntohs(mgenh->messageSize) != mgen_len) {
                logCompress("MGEN length doesn't match");
                goto done;
            }

            // Flags must be last buffer + checksum
            if (mgenh->flags != MGEN_LAST_BUFFER + MGEN_CHECKSUM) {
                logCompress("MGEN flags not set to MGEN_LAST_BUFFER + MGEN_CHECKSUM");
                goto done;
            }

            // Flags ID should be UDP destination port
            if (ntohl(mgenh->mgenFlowId) != ntohs(udph->uh_dport)) {
                logCompress("MGEN flow ID not equal to UDP destination port");
                goto done;
            }

            // Check for reserved field in DARPA MGEN
            if (mgenh->version == DARPA_MGEN_VERSION) {
                const struct darpa_mgenhdr *darpa_mgenh = reinterpret_cast<const struct darpa_mgenhdr*>(mgenh);

                if (darpa_mgenh->reserved != 0) {
                    logCompress("DARPA MGEN header reserved field not 0");
                    goto done;
                }
            }

            // Check for default MGEN address
            const struct mgenstdaddr *mgenaddr;

            if (mgenh->version == MGEN_VERSION) {
                if (mgen_len < sizeof(struct mgenhdr) + sizeof(struct mgenstdaddr)) {
                    logCompress("Not enough room for MGEN header plus address");
                    goto done;
                }

                mgenaddr = reinterpret_cast<const struct mgenstdaddr*>(reinterpret_cast<const char*>(mgenh) + sizeof(struct mgenhdr));
            } else if (mgenh->version == DARPA_MGEN_VERSION) {
                if (mgen_len < sizeof(struct darpa_mgenhdr) + sizeof(struct mgenstdaddr)) {
                    logCompress("Not enough room for DARPA MGEN header plus address");
                    goto done;
                }

                mgenaddr = reinterpret_cast<const struct mgenstdaddr*>(reinterpret_cast<const char*>(mgenh) + sizeof(struct darpa_mgenhdr));
            } else
                goto done;

            // MGEN destination address must match destination IP address
            if (mgenaddr->dstPort != udph->uh_dport ||
                mgenaddr->dstAddrType != MGEN_IPv4 ||
                mgenaddr->dstAddrLen != 4 ||
                mgenaddr->dstIPAddr != iph->ip_dst.s_addr) {
                logCompress("MGEN destination address unexpected");
                goto done;
            }

            // MGEN host address must match destination IP address
            if (mgenaddr->hostPort != 0 ||
                mgenaddr->hostAddrType != MGEN_INVALID_ADDRESS ||
                mgenaddr->hostAddrLen != 0) {
                logCompress("MGEN host address unexpected");
                goto done;
            }

            // Compress the rest of the headers
            if (mgenh->version == MGEN_VERSION) {
                if (mgen_len < sizeof(struct mgenhdr) + sizeof(struct mgenstdaddr) + sizeof(struct mgenrest)) {
                    logCompress("Not enough room for MGEN header plus address plus rest");
                    goto done;
                }

                const struct mgenrest *mgenrest = reinterpret_cast<const struct mgenrest*>(reinterpret_cast<const char*>(mgenaddr) + sizeof(struct mgenstdaddr));

                if (mgenrest->latitude != kExpectedLatitude ||
                    mgenrest->longitude != kExpectedLongitude ||
                    mgenrest->altitude != kExpectedAltitude ||
                    mgenrest->gpsStatus != MGEN_INVALID_GPS ||
                    mgenrest->reserved != 0 ||
                    mgenrest->payloadLen != 0) {
                    logCompress("MGEN geo info unexpected");
                    goto done;
                }
            } else /* mgenh->version == DARPA_MGEN_VERSION */ {
                if (mgen_len < sizeof(struct darpa_mgenhdr) + sizeof(struct mgenstdaddr) + sizeof(struct darpa_mgenrest)) {
                    logCompress("Not enough room for DARPA MGEN header plus address plus rest");
                    goto done;
                }

                const struct darpa_mgenrest *mgenrest = reinterpret_cast<const struct darpa_mgenrest*>(reinterpret_cast<const char*>(mgenaddr) + sizeof(struct mgenstdaddr));

                if (mgenrest->tos != iph->ip_tos ||
                    mgenrest->latitude != kExpectedLatitude ||
                    mgenrest->longitude != kExpectedLongitude ||
                    mgenrest->altitude != kExpectedAltitude ||
                    mgenrest->gpsStatus != MGEN_INVALID_GPS ||
                    mgenrest->reserved != 0 ||
                    mgenrest->payloadLen != 0) {
                    logCompress("MGEN geo info unexpected");
                    goto done;
                }
            }

            // Check CRC32
            const char *mgen_data = reinterpret_cast<const char*>(mgenh);
            uint32_t   cksum;
            uint32_t   mgen_cksum;

            cksum = crc32(mgen_data, mgen_len - 4);
            memcpy(&mgen_cksum, mgen_data + mgen_len - 4, sizeof(uint32_t));

            if (htonl(cksum) != mgen_cksum) {
                logCompress("Bad MGEN checksum: mgen_len=%lu; cksum=0x%x; mgen_cksum=0x%x",
                    mgen_len,
                    cksum,
                    mgen_cksum);
                goto done;
            }

            // Compress MGEN
            if (mgenh->version == MGEN_VERSION) {
                buf.copyOut(mgenh->sequenceNumber);
                buf.copyOut(mgenh->txTimeSeconds);
                buf.copyOut(mgenh->txTimeMicroseconds);

                buf.inoff += sizeof(struct mgenhdr) + sizeof(mgenstdaddr) + sizeof(mgenrest);
                buf.flags.type = kMGEN;
            } else /* mgenh->version == DARPA_MGEN_VERSION */ {
                const struct darpa_mgenhdr *darpa_mgenh = reinterpret_cast<const struct darpa_mgenhdr*>(mgenh);

                buf.copyOut(darpa_mgenh->sequenceNumber);
                buf.copyOut(darpa_mgenh->txTimeSeconds);
                buf.copyOut(darpa_mgenh->txTimeMicroseconds);

                buf.inoff += sizeof(struct darpa_mgenhdr) + sizeof(mgenstdaddr) + sizeof(darpa_mgenrest);
                buf.flags.type = kDARPAMGEN;
            }

            // Copy out padding
            buf.copyBytesOut(mgen_padlen);

            // Skip crc
            buf.inoff += 4;
        } else {
        ip_done:
            buf.copyOut(iph->ip_p);
        }
    }

done:
    buf.flush();
}

class DecompressionBuffer : public buffer<unsigned char>
{
public:
    DecompressionBuffer() = delete;

    DecompressionBuffer(RadioPacket &pkt_)
      : buffer(pkt_.size())
      , pkt(pkt_)
      , flags({0})
      , inoff(0)
      , outoff(0)
    {
        // Copy extended header
        copyBytesOut(sizeof(ExtendedHeader));

        // Read flags
        memcpy(&flags, pkt.data() + inoff, sizeof(CompressionFlags));
        inoff += sizeof(CompressionFlags);
    }

    void copyBytesOut(size_t count)
    {
        resize(outoff + count);
        memcpy(data() + outoff, pkt.data() + inoff, count);
        inoff += count;
        outoff += count;
    }

    template<class T>
    void copyOut(const T &val)
    {
        resize(outoff + sizeof(T));
        memcpy(data() + outoff, &val, sizeof(T));
        outoff += sizeof(T);
    }

    template<class T>
    T read(void)
    {
        T val;

        memcpy(&val, pkt.data() + inoff, sizeof(T));
        inoff += sizeof(T);
        return val;
    }

    void flush(void)
    {
        ssize_t extra = outoff - inoff;

        copyBytesOut(pkt.size() - inoff);
        pkt.swap(*this);
        pkt.ehdr().data_len = static_cast<ssize_t>(pkt.ehdr().data_len) + extra;
        pkt.resize(outoff);
    }

    RadioPacket &pkt;

    CompressionFlags flags;

    size_t inoff;

    size_t outoff;
};

void PacketCompressor::decompress(RadioPacket &pkt)
{
    DecompressionBuffer buf(pkt);

    // Test for compressed Ethernet header
    if (buf.flags.type < kEthernet)
        goto done;

    // Decompress Ethernet header
    {
        ether_header ehdr = { { 0xc6, 0xff, 0xff, 0xff, 0xff, pkt.hdr.nexthop }
                            , { 0xc6, 0xff, 0xff, 0xff, 0xff, pkt.hdr.curhop }
                            , htons(ETHERTYPE_IP)
                            };

        buf.copyOut(ehdr);
    }

    // Test for compressed IP header
    if (buf.flags.type < kIP)
        goto done;

    // Decompress IP header
    struct ip iph;

    iph.ip_v = 4;
    iph.ip_hl = 5;
    iph.ip_tos = buf.read<u_int8_t>();
    iph.ip_len = 0;
    iph.ip_id = buf.read<u_short>();
    iph.ip_off = htons(IP_DF);
    if (buf.flags.read_ttl)
        iph.ip_ttl = buf.read<u_int8_t>();
    else
        iph.ip_ttl = kExpectedTTL;
    if (buf.flags.type >= kUDP)
        iph.ip_p = IPPROTO_UDP;
    else
        iph.ip_p = buf.read<u_int8_t>();
    iph.ip_sum = 0;

    if (buf.flags.ipaddr_type == kIPUncompressed) {
        iph.ip_src.s_addr = buf.read<in_addr_t>();
        iph.ip_dst.s_addr = buf.read<in_addr_t>();
    } else if (buf.flags.ipaddr_type == kIPInternal) {
        iph.ip_src.s_addr = htonl(int_net_ + pkt.ehdr().src);
        iph.ip_dst.s_addr = htonl(int_net_ + pkt.ehdr().dest);
    } else /* buf.flags.ipaddr_type == kIPExternal */ {
        iph.ip_src.s_addr = htonl(ext_net_ + ((100 + pkt.ehdr().src) << 8) + buf.read<uint8_t>());
        iph.ip_dst.s_addr = htonl(ext_net_ + ((100 + pkt.ehdr().dest) << 8) + buf.read<uint8_t>());
    }

    buf.copyOut(iph);

    // Test for compressed UDP header
    if (buf.flags.type < kUDP)
        goto done;

    // Decompress UDP header
    struct udphdr udph;

    udph.uh_sport = buf.read<u_int16_t>();
    udph.uh_dport = buf.read<u_int16_t>();
    udph.uh_ulen = 0;
    udph.uh_sum = 0;

    buf.copyOut(udph);

    // Test for compressed MGEN header
    if (buf.flags.type == kMGEN) {
        // Compress MGEN header
        struct mgenhdr mgenh;

        mgenh.messageSize = 0;
        mgenh.version = MGEN_VERSION;
        mgenh.flags = MGEN_LAST_BUFFER + MGEN_CHECKSUM;
        mgenh.mgenFlowId = htonl(ntohs(udph.uh_dport));
        mgenh.sequenceNumber = buf.read<uint32_t>();
        mgenh.txTimeSeconds = buf.read<mgen_secs_t>();
        mgenh.txTimeMicroseconds = buf.read<mgen_usecs_t>();
        buf.copyOut(mgenh);

        // Compress MGEN address info
        struct mgenstdaddr mgenaddr;

        mgenaddr.dstPort = udph.uh_dport;
        mgenaddr.dstAddrType = MGEN_IPv4;
        mgenaddr.dstAddrLen = 4;
        mgenaddr.dstIPAddr = iph.ip_dst.s_addr;
        mgenaddr.hostPort = 0;
        mgenaddr.hostAddrType = MGEN_INVALID_ADDRESS;
        mgenaddr.hostAddrLen = 0;
        buf.copyOut(mgenaddr);

        // Compress MGEN geo and payload info
        struct mgenrest mgenrest;

        mgenrest.latitude = kExpectedLatitude;
        mgenrest.longitude = kExpectedLongitude;
        mgenrest.altitude = kExpectedAltitude;
        mgenrest.gpsStatus = MGEN_INVALID_GPS;
        mgenrest.reserved = 0;
        mgenrest.payloadLen = 0;
        buf.copyOut(mgenrest);
    } else if (buf.flags.type == kDARPAMGEN) {
        // Compress MGEN header
        struct darpa_mgenhdr mgenh;

        mgenh.messageSize = 0;
        mgenh.version = DARPA_MGEN_VERSION;
        mgenh.flags = MGEN_LAST_BUFFER + MGEN_CHECKSUM;
        mgenh.mgenFlowId = htonl(ntohs(udph.uh_dport));
        mgenh.sequenceNumber = buf.read<uint32_t>();
        mgenh.reserved = 0;
        mgenh.txTimeSeconds = buf.read<mgen_secs_t>();
        mgenh.txTimeMicroseconds = buf.read<mgen_usecs_t>();
        buf.copyOut(mgenh);

        // Compress MGEN address info
        struct mgenstdaddr mgenaddr;

        mgenaddr.dstPort = udph.uh_dport;
        mgenaddr.dstAddrType = MGEN_IPv4;
        mgenaddr.dstAddrLen = 4;
        mgenaddr.dstIPAddr = iph.ip_dst.s_addr;
        mgenaddr.hostPort = 0;
        mgenaddr.hostAddrType = MGEN_INVALID_ADDRESS;
        mgenaddr.hostAddrLen = 0;
        buf.copyOut(mgenaddr);

        // Compress MGEN geo and payload info
        struct darpa_mgenrest mgenrest;

        mgenrest.tos = iph.ip_tos;
        mgenrest.latitude = kExpectedLatitude;
        mgenrest.longitude = kExpectedLongitude;
        mgenrest.altitude = kExpectedAltitude;
        mgenrest.gpsStatus = MGEN_INVALID_GPS;
        mgenrest.reserved = 0;
        mgenrest.payloadLen = 0;
        buf.copyOut(mgenrest);
    }

    if (buf.flags.type == kMGEN || buf.flags.type == kDARPAMGEN) {
        // Copy out MGEN padding
        size_t mgen_padlen = sizeof(ExtendedHeader) + pkt.ehdr().data_len - buf.inoff;

        buf.copyBytesOut(mgen_padlen);

        // Append checksum
        buf.copyOut<uint32_t>(0);
    }

done:
    buf.flush();

    // Fix up MGEN length and checksum
    if (buf.flags.type == kMGEN || buf.flags.type == kDARPAMGEN) {
        struct udphdr  *udph = pkt.getUDPHdr();
        struct mgenhdr *mgenh = reinterpret_cast<struct mgenhdr*>(reinterpret_cast<char*>(udph) + sizeof(struct udphdr));
        char           *mgen_data = reinterpret_cast<char*>(mgenh);
        size_t         mgen_len = pkt.ehdr().data_len - (sizeof(ether_header) + sizeof(struct ip) + sizeof(struct udphdr));

        mgenh->messageSize = htons(mgen_len);

        uint32_t cksum = htonl(crc32(mgen_data, mgen_len - 4));

        memcpy(mgen_data + mgen_len - 4, &cksum, sizeof(uint32_t));
    }

    // Fix up UDP length and checksum
    if (buf.flags.type >= kUDP) {
        struct ip     *iph = pkt.getIPHdr();
        struct udphdr *udph = pkt.getUDPHdr();
        size_t        udp_len = pkt.ehdr().data_len - (sizeof(ether_header) + sizeof(struct ip));

        udph->uh_ulen = htons(udp_len);
        udph->uh_sum = udp_checksum(iph, udph, udp_len);
    }

    // Fix up IP length and checksum
    if (buf.flags.type >= kIP) {
        struct ip *iph = pkt.getIPHdr();
        size_t    ip_len = pkt.ehdr().data_len - sizeof(ether_header);

        iph->ip_len = htons(ip_len);
        iph->ip_sum = ip_checksum(iph, sizeof(struct ip));
    }

    // Cache payload size
    pkt.payload_size = pkt.getPayloadSize();
}
