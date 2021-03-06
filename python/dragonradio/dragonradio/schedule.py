# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""MAC schedule construction"""
import numpy as np

def bestScheduleChannel(sched, node_id):
    """Choose the best single channel for the given node to use from the
    schedule.

    If the schedule allows the given node to transmit on multiple channels, pick
    the channel with the most air time.

    Args:
        sched: A schedule
        node_id: A node

    Returns:
        The best channel to transmit on
    """
    if not (sched == node_id).any():
        raise ValueError("No slot for node {}".format(node_id))

    return (sched == node_id).sum(axis=1).argmax()

def pureTDMASchedule(nodes):
    """Create a pure TDMA schedule that gives each node a single slot.

    Args:
        nodes: The nodes

    Returns:
        A schedule consisting of a 1 X nslots array of node IDs.
    """
    nslots = len(nodes)
    sched = np.zeros((1, nslots), dtype=int)

    for i, node in enumerate(nodes):
        sched[0][i] = node

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
    # Assign nodes to channels
    channels = [[] for _ in range(0, nchannels)]

    chan = 0

    for i, node in enumerate(nodes):
        while chan < nchannels:
            if len(channels[chan]) == i // nchannels:
                channels[chan].append(node)
                chan += k
                break

            chan += 1

        if chan >= nchannels:
            chan = 0

    # Create a schedule where nodes alternate slots in their assigned channel
    sched = np.zeros((nchannels, nslots), dtype=int)

    for chan in range(0, nchannels):
        nodes = channels[chan]

        if len(nodes) > 0:
            for slot in range (0, nslots):
                sched[chan,slot] = nodes[slot % len(nodes)]

    return sched
