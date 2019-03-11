local mgen = Proto("mgen", "MGEN Protocol")

--------------------------------------------------------------------------------
-- Protocol Fields
--------------------------------------------------------------------------------

local pf_size               = ProtoField.uint16("mgen.size", "Size")
local pf_version            = ProtoField.uint8("mgen.version", "Version")
local pf_flags              = ProtoField.uint8("mgen.flags", "Flags", base.HEX)
local pf_flow_id            = ProtoField.uint32("mgen.flow_id", "Flow ID")
local pf_seq                = ProtoField.uint32("mgen.seq", "Sequence Number")
local pf_tx_time_secs       = ProtoField.uint32("mgen.tx_time_secs", "TX time (sec)")
local pf_tx_time_secs_darpa = ProtoField.uint64("mgen.tx_time_secs", "TX time (sec)")
local pf_tx_time_usecs      = ProtoField.uint32("mgen.tx_time_secs", "TX time (usec)")

local pf_flag_continues = ProtoField.bool("mgen.flags.continues", "Continues",
                            8,
                            {"this is a fragment", "this is not a fragment"},
                            0x01,
                            "is the message a fragment?")

local pf_flag_end = ProtoField.bool("mgen.flags.end", "End of message",
                            8,
                            {"this is the end of the message", "this is not the end of the message"},
                            0x02,
                            "is this the end of the message?")

local pf_flag_checksum = ProtoField.bool("mgen.flags.checksum", "Checksum",
                            8,
                            {"this message has a checksum", "this message does not have a checksum"},
                            0x04,
                            "does this message have a checksum?")

local pf_flag_last_buffer = ProtoField.bool("mgen.flags.last_buffer", "Last buffer",
                            8,
                            {"this message is the last buffer in a fragment", "this message is not the last buffer in a fragment"},
                            0x08,
                            "is this message the last buffer in a fragment?")

local pf_flag_checksum_error = ProtoField.bool("mgen.flags.checksum_error", "Checksum error",
                            8,
                            {"this message has a checksum error", "this message does not have a checksum error"},
                            0x10,
                            "does this message have a checksum error?")

local address_types = {
        [0] = "Invalid address",
        [1] = "IPv4",
        [2] = "IPv6"
}

local pf_dst_port      = ProtoField.uint16("mgen.dst_port", "Destination port")
local pf_dst_addr_type = ProtoField.uint8("mgen.dst_addr_type", "Destination address type", base.Dec, address_types)
local pf_dst_addr_len  = ProtoField.uint8("mgen.dst_addr_len", "Destination address len")
local pf_dst_addr_ipv4 = ProtoField.ipv4("mgen.dst_addr", "Destination address")

local pf_host_port      = ProtoField.uint16("mgen.host_port", "Host port")
local pf_host_addr_type = ProtoField.uint8("mgen.host_addr_type", "Host address type", base.Dec, address_types)
local pf_host_addr_len  = ProtoField.uint8("mgen.host_addr_len", "Host address len")
local pf_host_addr_ipv4 = ProtoField.ipv4("mgen.host_addr", "Host address")

local tos_classes = {
        [0] = "Leaky bucket",
        [1] = "VOIP",
        [2] = "FTP",
        [3] = "HTTP"
}

local pf_tos_darpa      = ProtoField.uint8("mgen.tos", "TOS", base.HEX)
local pf_tos_priority   = ProtoField.uint8("mgen.tos.priority", "Priority", base.DEC, nil, 0xf0)
local pf_tos_priority_f = ProtoField.float("mgen.tos.priority_f", "Priority")
local pf_tos_class      = ProtoField.uint8("mgen.tos.class", "Class", base.DEC, tos_classes, 0x0f)

local gps_status = {
        [0] = "Invalid",
        [1] = "Stale",
        [2] = "Current"
}

local pf_latitude = ProtoField.uint32("mgen.latitude", "Latitude")
local pf_longitude = ProtoField.uint32("mgen.longitude", "Longitude")
local pf_altitude = ProtoField.int32("mgen.altitude", "Altitude")
local pf_gps_status = ProtoField.uint8("mgen.gps_status", "GPS status", base.Dec, gps_status)

local pf_reserved = ProtoField.uint8("mgen.reserved", "Reserved")

local pf_payload_len = ProtoField.uint16("mgen.payload_len", "Payload length")
local pf_payload = ProtoField.bytes("mgen.payload", "Payload")

local pf_padding_len = ProtoField.uint16("mgen.padding_len", "Padding length")
local pf_padding = ProtoField.bytes("mgen.padding", "Padding")

local pf_checksum = ProtoField.uint32("mgen.checksum", "Checksum", base.HEX)

mgen.fields = { pf_size

              , pf_version

              , pf_flags
              , pf_flag_continues
              , pf_flag_end
              , pf_flag_checksum
              , pf_flag_last_buffer
              , pf_flag_checksum_error

              , pf_flow_id

              , pf_seq

              , pf_tx_time_secs
              , pf_tx_time_secs_darpa
              , pf_tx_time_usecs

              , pf_dst_port
              , pf_dst_addr_type
              , pf_dst_addr_len
              , pf_dst_addr_ipv4

              , pf_host_port
              , pf_host_addr_type
              , pf_host_addr_len
              , pf_host_addr_ipv4

              , pf_tos_darpa
              , pf_tos_priority
              , pf_tos_priority_f
              , pf_tos_class

              , pf_latitude
              , pf_longitude
              , pf_altitude
              , pf_gps_status
              , pf_reserved

              , pf_payload_len
              , pf_payload
              , pf_padding_len
              , pf_padding

              , pf_checksum
              }

--------------------------------------------------------------------------------
-- Preferences
--------------------------------------------------------------------------------

local default_settings =
{
    port         = 5001,
    heur_enabled = true,
}

mgen.prefs.port  = Pref.uint("Port number", default_settings.port,
                             "The UDP port number for MGEN")

mgen.prefs.heur  = Pref.bool("Heuristic enabled", default_settings.heur_enabled,
                             "Whether heuristic dissection is enabled or not")

function mgen.prefs_changed()
    default_settings.heur_enabled = mgen.prefs.heur

    if default_settings.port ~= mgen.prefs.port then
        -- Remove old port, if not 0
        if default_settings.port ~= 0 then
            DissectorTable.get("udp.port"):remove(default_settings.port, mgen)
        end

        -- Set our new default
        default_settings.port = mgen.prefs.port

        -- Add new port, if not 0
        if default_settings.port ~= 0 then
            DissectorTable.get("udp.port"):add(default_settings.port, mgen)
        end
    end

end

--------------------------------------------------------------------------------
-- Fields
--------------------------------------------------------------------------------

local version_field = Field.new("mgen.version")

local flag_checksum_field = Field.new("mgen.flags.checksum")

local dst_addr_type_field = Field.new("mgen.dst_addr_type")
local dst_addr_len_field = Field.new("mgen.dst_addr_len")

local host_addr_type_field = Field.new("mgen.host_addr_type")
local host_addr_len_field = Field.new("mgen.host_addr_len")

local longitude_field = Field.new("mgen.longitude")

local payload_len_field = Field.new("mgen.payload_len")

local MGEN_HDR_LEN = 20

local MGEN_VERSION = 2
local DARPA_MGEN_VERSION = 4

local IPV4 = 1
local IPV6 = 2

mgen.dissector = function (tvbuf,pktinfo,root)
    -- Set the protocol column to show our protocol name
    pktinfo.cols.protocol:set("MGEN")

    -- We want to check that the packet size is rational during dissection, so
    -- let's get the length of the packet buffer (Tvb).
    local pktlen = tvbuf:reported_length_remaining()

    -- We start by adding our protocol to the dissection display tree. A call to
    -- tree:add() returns the child created, so we can add more "under" it using
    -- that return value. The second argument is how much of the buffer/packet
    -- this added tree item covers/represents.
    local tree = root:add(mgen, tvbuf:range(0,pktlen))

    if pktlen < MGEN_HDR_LEN then
        return 0
    end

    off = 0

    local addField = function (field, sz)
        subtree = tree:add(field, tvbuf:range(off, sz))
        off = off + sz
        return subtree
    end

    addField(pf_size, 2)
    addField(pf_version, 1)

    version = version_field()()

    tree:add(pf_flags, tvbuf:range(off,1))
    tree:add(pf_flag_continues, tvbuf:range(off,1))
    tree:add(pf_flag_end, tvbuf:range(off,1))
    tree:add(pf_flag_checksum, tvbuf:range(off,1))
    tree:add(pf_flag_last_buffer, tvbuf:range(off,1))
    tree:add(pf_flag_checksum_error, tvbuf:range(off,1))
    off = off + 1

    addField(pf_flow_id, 4)
    addField(pf_seq, 4)

    if version == DARPA_MGEN_VERSION then
        addField(pf_tx_time_secs_darpa, 8)
    else
        addField(pf_tx_time_secs, 4)
    end
    addField(pf_tx_time_usecs, 4)

    -- Destination
    addField(pf_dst_port, 2)
    addField(pf_dst_addr_type, 1)
    addField(pf_dst_addr_len, 1)

    dst_addr_type = dst_addr_type_field()()
    dst_addr_len = dst_addr_len_field()()

    if dst_addr_type == IPV4 and dst_addr_len == 4 then
        tree:add(pf_dst_addr_ipv4, tvbuf:range(off,4))
    end
    off = off + dst_addr_len

    -- Host
    addField(pf_host_port, 2)
    addField(pf_host_addr_type, 1)
    addField(pf_host_addr_len, 1)

    host_addr_type = host_addr_type_field()()
    host_addr_len = host_addr_len_field()()

    if host_addr_type == IPV4 and host_addr_len == 4 then
        tree:add(pf_host_addr_ipv4, tvbuf:range(off,4))
    end
    off = off + host_addr_len

    if version == DARPA_MGEN_VERSION then
        -- See:
        -- https://sc2colosseum.freshdesk.com/support/solutions/articles/22000220454-type-of-service-tos-field
        priority = 1.0 + (tvbuf:range(off,1):uint() / 16) / 10.0

        subtree = tree:add(pf_tos_darpa, tvbuf:range(off, 1))
        subtree:add(pf_tos_priority, tvbuf:range(off, 1))
        subtree:add(pf_tos_priority_f, tvbuf:range(off, 1), priority):set_generated()
        subtree:add(pf_tos_class, tvbuf:range(off, 1))

        off = off + 1
    end

    -- GPS info
    longitude = tvbuf:range(off,4):uint()
    tree:add(pf_longitude, tvbuf:range(off,4), longitude/60000.0-180.0)
    off = off + 4

    latitude = tvbuf:range(off,4):uint()
    tree:add(pf_latitude, tvbuf:range(off,4), latitude/60000.0-180.0)
    off = off + 4

    addField(pf_altitude, 4)
    addField(pf_gps_status, 1)
    addField(pf_reserved, 1)

    -- Payload
    addField(pf_payload_len, 2)

    payload_len = payload_len_field()()

    addField(pf_payload, payload_len)

    -- Padding
    if flag_checksum_field()() then
        padding_len = pktlen - off - 4
    else
        padding_len = pktlen - off
    end
    tree:add(pf_padding_len, padding_len):set_generated()

    addField(pf_padding, padding_len)

    -- Checksum
    if flag_checksum_field()() then
        addField(pf_checksum, 4)
    end
end

DissectorTable.get("udp.port"):add(default_settings.port, mgen)

local function heur_dissect_mgen(tvbuf,pktinfo,root)
    if not default_settings.heur_enabled then
        return false
    end

    if tvbuf:len() < MGEN_HDR_LEN then
        return false
    end

    local tvbr = tvbuf:range(0,MGEN_HDR_LEN)

    -- First 2 bytes are size
    local size = tvbr:range(0,2):uint()

    if size ~= tvbuf:len() then
        return false
    end

    -- Next byte is version
    local version = tvbr:range(2,1):uint()

    return version == 2 or version == 4
end

mgen:register_heuristic("udp",heur_dissect_mgen)
