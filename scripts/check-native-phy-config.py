#!/usr/bin/env dragonradio
"""No-radio check: parse a radio command line and build its PHY (native or liquid).

  dragonradio scripts/check-native-phy-config.py --phy native-ofdm -c rs8 -m qpsk

Uses the same Config parser and PHY constructor arguments as the radio
(dragonradio.controller.radio.Radio.mkPHY) without opening a USRP.
"""
import sys

from dragonradio.config import Config
from dragonradio.liquid import MCS
from dragonradiocpp.radio import AutoGain, NativeOFDM, NativeOFDMA


def main():
    config = Config()
    parser = config.parser()
    parser.parse_args(namespace=config)
    mcs = MCS(config.check, config.fec0, config.fec1, config.ms)
    table = [(mcs, AutoGain())]
    cls = config.phy_class
    if issubclass(cls, NativeOFDMA):
        phy = cls(table, config.M, config.cp_len, config.subcarriers, config.native_max_payload,
                  config.soft_payload, config.ofdma_allocation)
        print(f'NativeOFDMA M={phy.M} allocation={phy.allocation} max_burst_users={phy.max_burst_users}')
    elif issubclass(cls, NativeOFDM):
        phy = cls(table, config.M, config.cp_len, config.subcarriers, config.native_max_payload,
                  config.native_chase_bits, config.soft_payload, config.native_header_fec)
        print(f'NativeOFDM M={phy.M} cp={phy.cp_len} fec={phy.fec} header_fec={phy.header_fec} '
              f'bits_per_symbol={phy.bits_per_symbol} fec_available={NativeOFDM.fec_available}')
    else:
        print(f'{cls.__name__}: liquid PHY (not constructed here)')
    print(f'arq_max_retransmissions={config.arq_max_retransmissions}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
