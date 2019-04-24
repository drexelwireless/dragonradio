#ifndef FIREWALL_HH_
#define FIREWALL_HH_

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <mutex>
#include <unordered_set>

#include "spinlock_mutex.hh"
#include "Clock.hh"
#include "Packet.hh"
#include "net/MandatedOutcome.hh"
#include "net/Processor.hh"
#include "stats/TimeWindowEstimator.hh"

using PortSet = std::unordered_set<uint16_t>;

/** @brief A flow processor. */
template <class T>
class Firewall : public Processor<T>
{
public:
    Firewall()
      : enabled_(false)
      , allow_broadcasts_(false)
    {
    }

    virtual ~Firewall() = default;

    /** @brief Get enabled flagd */
    bool getEnabled(void)
    {
        return enabled_;
    }

    /** @brief Set enabled flag */
    void setEnabled(bool enabled)
    {
        enabled_ = enabled;
    }

    /** @brief Get flag indicating whether or not bradcast packets are allowed */
    bool getAllowBroadcasts(void)
    {
        return allow_broadcasts_;
    }

    /** @brief Set flag indicating whether or not bradcast packets are allowed */
    void setAllowBroadcasts(bool allowed)
    {
        allow_broadcasts_ = allowed;
    }

    /** @brief Get allowed ports */
    PortSet getAllowedPorts(void)
    {
        std::lock_guard<spinlock_mutex> lock(mutex_);

        return allowed_;
    }

    /** @brief Set allowed ports */
    void setAllowedPorts(const PortSet &allowed)
    {
        std::lock_guard<spinlock_mutex> lock(mutex_);

        allowed_ = allowed;
    }

protected:
    /** @brief Mutex for ports */
    spinlock_mutex mutex_;

    /** @brief Is the fireweall enabled? */
    bool enabled_;

    /** @brief Should we allow broadcast packets? */
    bool allow_broadcasts_;

    /** @brief allowed ports */
    PortSet allowed_;

    /** @brief Filter a packet */
    virtual bool process(T &pkt) override
    {
        if (!enabled_)
            return true;

        // First check for a broadcast
        if (pkt->isFlagSet(kBroadcast) && allow_broadcasts_)
            return true;

        // Then look at the port
        std::lock_guard<spinlock_mutex> lock(mutex_);
        const struct ip                 *iph;
        uint8_t                         ip_p;

        iph = pkt->getIPHdr(&ip_p);
        if (!iph)
            return true;

        switch (ip_p) {
            case IPPROTO_UDP:
            {
                const struct udphdr *udph = pkt->getUDPHdr();

                return allowed_.find(ntohs(udph->uh_dport)) != allowed_.end();
            }
            break;

            case IPPROTO_TCP:
            {
                const struct tcphdr *tcph = pkt->getTCPHdr();

                return allowed_.find(ntohs(tcph->th_dport)) != allowed_.end();
            }
            break;

            default:
                return false;
        }
    }
};

using NetFirewall = Firewall<std::shared_ptr<NetPacket>>;

using RadioFirewall = Firewall<std::shared_ptr<RadioPacket>>;

#endif /* FIREWALL_HH_ */
