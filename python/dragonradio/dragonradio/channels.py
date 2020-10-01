# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Channel planning"""
try:
  from _dragonradio.radio import Channel
except:
  pass

def defaultChannelPlan(bandwidth, cbw,
                       cgbw=0,
                       egbw=0,
                       maximize_channel_guard_bandwidth=True):
    """Generate a default channel plan.

    Arguments:
        bandwidth: total bandwidth
        cbw: channel bandwidth
        cgbw: channel guard bandwidth, i.e., minimum space between channels
        egbw: edge guard bandwidth, i.e., minimum space from edges of spectrum

    Return:
        A list of Channels
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
