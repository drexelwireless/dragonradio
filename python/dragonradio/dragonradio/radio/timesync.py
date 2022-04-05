# Copyright 2018-2021 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Support for time synchronization."""
import logging
import math

import numpy as np

try:
    from _dragonradio.radio import MonoTimePoint, clock
except:
    pass

logger = logging.getLogger('timesync')

def synchronize(config, radio, master, me):
    """Use timestamps to synchronize our clock with the time master (the gateway)"""
    # Perform linear regression on all timestamps
    me_timestamps = radio.me_timestamps
    if len(radio.me_timestamps) == 0:
        return

    master_timestamps = radio.master_timestamps
    if len(master_timestamps) == 0:
        return

    # If we have a GPSDO, then assume skew is zero
    no_skew = config.clock_noskew or \
        (radio.usrp.clock_source == 'external' and radio.usrp.time_source == 'external')

    (sigma, delta, tau) = timestampRegression(me_timestamps, master_timestamps, no_skew=no_skew)

    old_sigma = clock.skew
    old_delta = clock.offset.secs

    logger.debug(("TIMESYNC: regression parameters: "
                  "old_sigma=%g; "
                  "old_delta=%g; "
                  "sigma=%g; "
                  "delta=%g; "
                  "tau=%g"),
        old_sigma, old_delta, sigma, delta, tau)

    if math.isfinite(delta) and math.isfinite(sigma):
        clock.offset = MonoTimePoint(delta)
        clock.skew = sigma
        radio.logger.logEvent(("TIMESYNC: set skew and offset: "
                               "sigma={:g}; "
                               "delta={:g}").format(sigma, delta))

def relativizeTimestamps(ts):
    """Make (t_send, t_recv) timestamps relative to Clock t0"""
    t0 = clock.t0

    return sorted([((t_send-t0).secs, (t_recv-t0).secs) for (t_send, t_recv) in ts], key=lambda ts: ts[0])

def timestampRegression(echoed, master, no_skew=True):
    """Perform a linear regression on timestamps to determine clock skew and delta"""
    # pylint: disable=too-many-locals
    avec = np.array([a for (a, _) in echoed])
    bvec = np.array([b for (_, b) in echoed])

    cvec = np.array([c for (c, _) in master])
    dvec = np.array([d for (_, d) in master])

    abar = np.mean(avec)
    bbar = np.mean(bvec)

    cbar = np.mean(cvec)
    dbar = np.mean(dvec)

    if no_skew:
        sigma = 1.0
    else:
        covab = np.sum((avec - abar)*(bvec - bbar))
        vara = np.sum(np.square(avec - abar))

        covcd = np.sum((cvec - cbar)*(dvec - dbar))
        vard = np.sum(np.square(dvec - dbar))

        sigma = (covab + covcd)/(vara + vard)

    delta_plus_tau = bbar - sigma*abar
    delta_minus_tau = cbar - sigma*dbar

    delta = (delta_plus_tau + delta_minus_tau) / 2.0
    tau = (delta_plus_tau - delta_minus_tau) / 2.0

    return (sigma, delta, tau)
