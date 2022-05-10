# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""MAC schedule construction"""
import numpy as np
from typing import Dict, Sequence, Tuple

NodeId = int
"""A node ID"""

ChannelIdx = int
"""A channel index"""

NodeAssignments = Dict[NodeId, ChannelIdx]
"""A map from node to assignment channel"""

Schedule = np.ndarray
"""A MAC schedule.

A schedule is an n x m array, where n is the number of channels and m is the
number of time slots. Each entry in the array specifies which node is allowed to
transmit in that slot.
"""

def bestScheduleChannel(sched: Schedule, node_id: NodeId) -> ChannelIdx:
    """Choose the best single channel for the given node to use from the
    schedule.

    If the schedule allows the given node to transmit on multiple channels, pick
    the channel with the most air time.

    Args:
        sched (Schedule): A schedule
        node_id (NodeId): A node

    Raises:
        ValueError: Raised if no channel is available.

    Returns:
        ChannelIdx: The best channel to transmit on
    """
    if not (sched == node_id).any():
        raise ValueError("No slot for node {}".format(node_id))

    return (sched == node_id).sum(axis=1).argmax()

def pureTDMASchedule(nodes: Sequence[NodeId]) -> Schedule:
    """Create a pure TDMA schedule that gives each node a single slot.

    Args:
        nodes (Sequence[NodeId]): The nodes

    Returns:
        Schedule: A schedule consisting of a 1 X nslots array of node IDs.
    """
    nslots = len(nodes)
    sched = np.zeros((1, nslots), dtype=int)

    for i, node in enumerate(nodes):
        sched[0][i] = node

    return sched

def fullChannelMACSchedule(nchannels: int, nslots: int, nodes: Sequence[NodeId], k: int) -> Schedule:
    """Create a greedy schedule that gives each node its own channel.

    Args:
        nchannels (int): The number of channels
        nslots (int): The number of time slots
        nodes (Sequence[NodeId]): The nodes
        k (int): The desired channel separation

    Returns:
        Schedule: A schedule consisting of a nchannels X nslots array of node IDs.
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

def fairMACSchedule(nchannels: int, nslots : int, nodes: Sequence[NodeId], k: int=3, assignments: NodeAssignments={}) -> Tuple[Schedule, NodeAssignments]:
    """Create a schedule that distributes slots evenly amongst nodes.

    Args:
        nchannels (int): The number of channels
        nslots (int): The number of time slots
        nodes (Sequence[NodeId]): The nodes
        k (int, optional): The desired channel separation. Defaults to 3.
        assignments (Dict[int, int], optional): Map from nodes to assigned channels. Defaults to {}.

    Returns:
        Tuple[Schedule, NodeAssignments]: A schedule consisting of a nchannels X nslots array of node IDs and
    """
    # Create list of nodes assigned to each channel.
    channels = [[] for _ in range(0, nchannels)]

    # Add existing assignments
    for node, chan in assignments.items():
        channels[chan].append(node)

    # Assign nodes to channels
    basechan = 0

    for nodeidx, node in enumerate(nodes):
        # Skip nodes we've already assigned
        if node in assignments:
            continue

        for i in range(0, nchannels):
            chan = (basechan + i) % nchannels

            if len(channels[chan]) <= nodeidx // nchannels:
                channels[chan].append(node)
                assignments[node] = chan
                basechan = chan + k
                break

    # Create a schedule where nodes alternate slots in their assigned channel
    sched = np.zeros((nchannels, nslots), dtype=int)

    for chan in range(0, nchannels):
        nodes = channels[chan]

        if len(nodes) > 0:
            for slot in range (0, nslots):
                sched[chan,slot] = nodes[slot % len(nodes)]

    return sched, assignments
