#!/usr/bin/env dragonradio
"""Read-only check of a USRP's external 10 MHz reference and PPS input.

Run with the radio executable (embedded Python):

  dragonradio scripts/usrp-ref-check.py --addr addr=192.168.10.2 [--seconds 4]

Opens the device without tuning, setting gains or creating streams (nothing
is transmitted), selects the external clock and time sources, and reports
whether the reference is locked and how many PPS edges arrived. The previous
sources are restored. Prints key=value lines; exits 1 unless the reference
is locked (or the board has no lock sensor) and at least two PPS edges
arrived, the rule of the OpenISAC grid sync check.
"""
import argparse

from _dragonradio.radio import checkReference  # pylint: disable=no-name-in-module


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--addr', required=True)
    ap.add_argument('--clock-source', default='external')
    ap.add_argument('--time-source', default='external')
    ap.add_argument('--seconds', type=float, default=4.0)
    args = ap.parse_args()
    r = checkReference(args.addr, args.clock_source, args.time_source, args.seconds)
    for k in sorted(r):
        print(f'{k}={r[k]}')
    ok = r['ref_locked'] in ('yes', 'unknown') and int(r['pps_edges']) >= 2
    print(f'reference_ok={"yes" if ok else "no"}')
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
