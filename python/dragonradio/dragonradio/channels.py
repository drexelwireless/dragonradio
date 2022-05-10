# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Channel planning"""
from typing import List

from _dragonradio.radio import Channel

def defaultChannelPlan(bandwidth: float, cbw: float,
                       cgbw: float=0,
                       egbw: float=0,
                       maximize_channel_guard_bandwidth: bool=True) -> List[Channel]:
    """Generate a default channel plan. Channels have equal bandwidth and are
    optionally separated by a channel guard bandwidth. The entire block of
    spectrum is also optionally surrounded on the two edges by a guard.

    Args:
        bandwidth (float): total bandwidth
        cbw (float): channel bandwidth
        cgbw (float, optional): channel guard bandwidth, i.e., minimum space between channels. Defaults to 0.
        egbw (float, optional): edge guard bandwidth, i.e., minimum space from edges of spectrum. Defaults to 0.
        maximize_channel_guard_bandwidth (bool, optional): Use extra spectrum to maximize space between channels. Defaults to True.

    Raises:
        ValueError: raised if there is not enough bandwidth for a channel.

    Returns:
        List[Channel]: A list of spaced channels.
    """
    # We space channels so that there is egbw on each end and at least cgbw
    # between channels. For n channels, we therefore have n+1 guards, so:
    #    n*cbw + 2*egbw + (n-1)*cgbw <= bandwidth
    # => n <= 1 + (bandwidth - cbw - 2*egbw) / (cbw + cgbw)
    n = 1 + int((bandwidth-cbw-2*egbw)/(cbw+cgbw))

    if n < 1:
        raise ValueError(("No channels "
                          "(bandwidth={:g}; "
                          "channel bandwidth={:g}; "
                          "channel guard={:g}; "
                          "edge guard={:g})").\
            format(bandwidth, cbw, cgbw, egbw))

    # We use the leftover space to space channels as far apart as possible.
    #    (n-1)*cgbw + 2*egbw + n*cbw = bandwidth
    # => cgbw = (bandwidth - 2*egbw - n*cbw)/(n-1)
    if maximize_channel_guard_bandwidth and n > 1:
        cgbw = (bandwidth-2*egbw-n*cbw)/(n-1)

    return [Channel(egbw + i*(cbw + cgbw) + cbw/2. - bandwidth/2., cbw) for i in range(0,n)]
