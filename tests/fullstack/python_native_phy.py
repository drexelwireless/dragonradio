# Run with the dragonradio executable: dragonradio tests/fullstack/python_native_phy.py
# Constructs the native PHY through the embedded Python module exactly as
# radio.py does, accepts the production coded tables (rs8, convolutional
# codes) and rejects unsupported entries.
import sys
import _dragonradio

radio, liquid = _dragonradio.radio, _dragonradio.liquid
failures = 0

def check(cond, what):
    global failures
    print(("ok   " if cond else "FAIL ") + what)
    failures += not cond

table = [(liquid.MCS('crc32', 'none', 'none', 'qpsk'), radio.AutoGain()),
         (liquid.MCS('crc32', 'none', 'none', 'qam16'), radio.AutoGain())]
phy = radio.NativeOFDM(table, 48, 6, None, 4096, 4)
check(isinstance(phy, radio.PHY), "NativeOFDM is a PHY")
check((phy.M, phy.cp_len, phy.max_payload, phy.chase_bits) == (48, 6, 4096, 4), "properties")
check(list(phy.bits_per_symbol) == [2, 4], "QPSK and 16-QAM table")
check(len(phy.subcarriers) == 48, "subcarrier mask " + phy.subcarriers)
check(phy.getModulatedSize(0, 1500) > phy.getModulatedSize(1, 1500) > 0, "16-QAM frame shorter than QPSK")
check(phy.min_rx_rate_oversample == 1 and phy.min_tx_rate_oversample == 1, "1x oversampling")
check(radio.NativeOFDM.fec_available, "native FEC built")
check(list(phy.fec) == [('none', 'none'), ('none', 'none')] and phy.soft_payload and phy.header_fec == 'none',
      "uncoded table, soft decoding and uncoded header by default")
hdr = radio.NativeOFDM(table, 48, 6, None, 4096, 4, True, 'v29')
check(hdr.header_fec == 'v29' and hdr.getModulatedSize(0, 100) > phy.getModulatedSize(0, 100), "coded header is longer")
# The production payload code (rs8) and convolutional codes are accepted.
coded = radio.NativeOFDM([(liquid.MCS('crc32', 'rs8', 'none', 'qpsk'), radio.AutoGain()),
                          (liquid.MCS('crc32', 'none', 'v29p78', 'qam16'), radio.AutoGain())], 48, 6, None, 4096, 4, False)
check(list(coded.fec) == [('rs8', 'none'), ('none', 'v29p78')] and not coded.soft_payload, "coded table " + str(list(coded.fec)))
check(coded.getModulatedSize(0, 1500) > phy.getModulatedSize(0, 1500), "rs8 frame longer than uncoded")
for bad in (liquid.MCS('crc32', 'none', 'h74', 'qpsk'), liquid.MCS('crc32', 'v39', 'none', 'qpsk'),
            liquid.MCS('crc32', 'none', 'none', 'psk8'), liquid.MCS('crc16', 'none', 'none', 'qpsk')):
    try:
        radio.NativeOFDM([(bad, radio.AutoGain())], 48, 6)
        check(False, "rejects %r" % (bad,))
    except ValueError as exc:
        check("not support" in str(exc), "rejects %r" % (bad,))

# The configuration parser offers the new PHY and its options.
sys.path.insert(0, 'python/dragonradio')
try:
    from dragonradio.radio.config import Config
    check(Config().native_max_payload == 4096 and Config().native_chase_bits == 4 and Config().native_header_fec == 'v29',
          "configuration defaults")
    config = Config()
    # As scripts/standalone-radio.py does: parse straight into the Config.
    config.parser().parse_args(['--phy', 'native-ofdm', '--native-max-payload', '2048', '--native-chase-bits', '0'],
                               namespace=config)
    check(config.phy == 'native-ofdm' and config.native_max_payload == 2048 and config.native_chase_bits == 0,
          "command-line options")
except ImportError as exc:
    print("skip config parser check: " + str(exc))

print("PASSED" if not failures else "FAILED (%d)" % failures)
sys.exit(1 if failures else 0)
