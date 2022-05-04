(sec:networking)=

# Networking

After starting up, the radio will create a `tap0` device with IP address `10.10.10.<NODEID>` and a netmask of `255.255.255.0`, where `<NODEID>` is the node ID. Packets sent to this subnet will in turn be sent over the radio. Available neighboring nodes will have an ARP entry on the tap interface with a MAC address of the form `c6:ff:ff:ff:ff:<NODEIDHEX>`, where `<NODEIDHEX>` is the hexadecimal representation of the node ID. The name of the `tap` device is controlled by the {py:attr}`dragonradio.config.Config.tap_iface`. The IP and MAC addresses assigned to nodes can be changed by setting the {py:attr}`dragonradio.config.Config.tap_ipaddr` and {py:attr}`dragonradio.config.Config.tap_macaddr` configuration variables, respectively. Both are `printf`-style format strings for converting a node ID to an address.

When using the `standalone-radio.py` script, a neighbor entry is created for as many nodes as are specified by the `-n` command-line parameter. The `sc2-radio.py` script performs node discovery, adding neighbors as they are found. By default, both scripts assume that the network will be fully-connected subject to intermediate link degradation.

## MANET Mode

The radio can also be place in MANET mode by either setting the {py:attr}`dragonradio.config.Config.manet` configuration variable or, equivalently, specifying the `--manet` flag on radio boot. In this mode, neighbors may be dropped from the neighbor table, subject to additional configuration parameters. The relevant parameters are:

 * {py:attr}`dragonradio.config.Config.arq_unreachable_timeout`. This the amount of time, in seconds, that the LLC will wait for a node to send some sort of traffic before marking it as unreachable.
 * {py:attr}`dragonradio.config.Config.arq_proactive_unreachable`. When true, the LLC proactively looks for traffic from nodes, marking them as unreachable if they have not been heard from after `arq_unreachable_timeout` seconds. When this flag is false, the LLC will only mark a node as unreachable if it has not received expected ARQ feedback from the node.
 * {py:attr}`dragonradio.config.Config.arq_purge_unreachable`. When true, unreachable nodes are purged from the neighbor table. When false, nodes are only marked unreachable, but not purged from the neighbor table.

When running the `sc2-radio.py` script, there is an additional parameter that is useful:

 * {py:attr}`dragonradio.config.Config.standard_hello_interval`. This is the interval between successive HELLO packets after node discovery.
