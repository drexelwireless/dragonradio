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
    me_timestamps = relativizeTimestamps(me.timestamps.values())
    if len(me_timestamps) == 0:
        return

    master_timestamps = relativizeTimestamps(master.timestamps.values())
    if len(master_timestamps) == 0:
        return

    # If we have a GPSDO, then assume skew is zero
    if config.clock_noskew or \
        (radio.usrp.clock_source == 'external' and radio.usrp.time_source == 'external'):
        (sigma, delta, tau) = timestampRegressionNoSkew(me_timestamps, master_timestamps)
    else:
        (sigma, delta, tau) = timestampRegression(me_timestamps, master_timestamps)

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

def timestampRegression(echoed, master):
    """Perform a linear regression on timestamps to determine clock skew and delta"""
    # pylint: disable=too-many-locals

    avec = [a for (a, _) in echoed]
    bvec = [b for (_, b) in echoed]

    cvec = [c for (c, _) in master]
    dvec = [d for (_, d) in master]

    abar = np.mean(avec)
    bbar = np.mean(bvec)

    cbar = np.mean(cvec)
    dbar = np.mean(dvec)

    covab = sum([(a - abar)*(b - bbar) for (a, b) in echoed])
    vara = sum([(a - abar)**2.0 for a in avec])

    covcd = sum([(c - cbar)*(d - dbar) for (c, d) in master])
    vard = sum([(d - dbar)**2.0 for d in dvec])

    sigma = (covab + covcd)/(vara + vard)

    delta_plus_tau = bbar - sigma*abar
    delta_minus_tau = cbar - sigma*dbar

    delta = (delta_plus_tau + delta_minus_tau) / 2.0
    tau = (delta_plus_tau - delta_minus_tau) / 2.0

    return (sigma, delta, tau)

def timestampRegressionNoSkew(echoed, master):
    """Perform a linear regression on timestamps to determine clock delta (assuming no skew)"""
    avec = [a for (a, _) in echoed]
    bvec = [b for (_, b) in echoed]

    cvec = [c for (c, _) in master]
    dvec = [d for (_, d) in master]

    abar = np.mean(avec)
    bbar = np.mean(bvec)

    cbar = np.mean(cvec)
    dbar = np.mean(dvec)

    delta_plus_tau = bbar - abar
    delta_minus_tau = cbar - dbar

    delta = (delta_plus_tau + delta_minus_tau) / 2.0
    tau = (delta_plus_tau - delta_minus_tau) / 2.0

    return (1.0, delta, tau)
