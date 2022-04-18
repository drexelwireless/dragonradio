// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "logging.hh"
#include "PHY.hh"

uint8_t PHY::team_ = 0;

NodeId PHY::node_id_ = 0;

bool PHY::log_invalid_headers_ = false;

std::shared_ptr<SnapshotCollector> PHY::snapshot_collector_;

std::shared_ptr<RadioPacket> PHY::mkRadioPacket(bool header_valid,
                                                bool payload_valid,
                                                const Header &h,
                                                size_t payload_len,
                                                unsigned char *payload_data)
{
    if (!header_valid) {
        if (log_invalid_headers_)
            logPHY(LOGDEBUG-1, "invalid header");

        return nullptr;
    } else if (!payload_valid) {
        std::shared_ptr<RadioPacket> pkt = std::make_shared<RadioPacket>(h);

        pkt->internal_flags.invalid_payload = 1;

        if (h.nexthop == node_id_)
            logPHY(LOGDEBUG-1, "invalid payload: curhop=%u; nexthop=%u; seq=%u",
                pkt->hdr.curhop,
                pkt->hdr.nexthop,
                (unsigned) pkt->hdr.seq);

        return pkt;
    } else {
        std::shared_ptr<RadioPacket> pkt = std::make_shared<RadioPacket>(h, payload_data, payload_len);

        if (!pkt->integrityIntact()) {
            pkt->internal_flags.invalid_payload = 1;

            logPHY(LOGERROR, "packet integrity not intact: seq=%u",
                (unsigned) pkt->hdr.seq);
        }

        // Cache payload size if this packet is not compressed
        if (!pkt->hdr.flags.compressed)
            pkt->payload_size = pkt->getPayloadSize();

        return pkt;
    }
}
