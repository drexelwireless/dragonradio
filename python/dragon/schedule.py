import numpy as np

def pureTDMASchedule(nodes):
    """Create a pure TDMA schedule that gives each node a single slot.

    Args:
        nodes: The nodes

    Returns:
        A schedule consisting of a 1 X nslots array of node IDs.
    """
    nslots = len(nodes)
    sched = np.zeros((1, nslots), dtype=int)

    for i in range(0, len(nodes)):
        sched[0][i] = nodes[i]

    return sched

def fullChannelMACSchedule(nchannels, nslots, nodes, k):
    """Create a greedy schedule that gives each node its own channel.

    Args:
        nchannels: The number of channels
        nslots: The number of time slots
        nodes: The nodes
        k: The desired channel separation

    Returns:
        A schedule consisting of a nchannels X nslots array of node IDs.
    """
    sched = np.zeros((nchannels, nslots), dtype=int)

    # Each node gets its own channel. Any leftover nodes don't get anything
    nodes = nodes[:nchannels]

    i = 0
    while len(nodes) != 0:
        if np.all(sched[i] == 0):
            sched[i] = nodes[0]
            nodes = nodes[1:]
            i += k
        else:
            i += 1

        if i >= nchannels:
            i = 0

    return sched

def fairMACSchedule(nchannels, nslots, nodes, k):
    """Create a schedule that distributes slots evenly amongst nodes.

    Args:
        nchannels: The number of channels
        nslots: The number of time slots
        nodes: The nodes
        k: The desired channel separation

    Returns:
        A schedule consisting of a nchannels X nslots array of node IDs.
    """
    sched = np.zeros((nchannels, nslots), dtype=int)

    # How many slots to fill per assignment
    slotsper = 1

    # Current slot
    slot = 0

    # How many channels we've filled in the current slot
    filled = 0

    # Current channel
    chan = 0

    # Remaining nodes to assign
    rem_nodes = nodes

    while slot < nslots:
        if np.all(sched[chan,slot:slot+slotsper] == 0):
            sched[chan,slot:slot+slotsper] = rem_nodes[0]
            rem_nodes = rem_nodes[1:]
            chan += k
            filled += 1
        else:
            chan += 1

        if chan >= nchannels:
            chan = 0

        if filled == nchannels:
            slot += slotsper
            filled = 0

        if not rem_nodes:
            rem_nodes = nodes

    return sched
