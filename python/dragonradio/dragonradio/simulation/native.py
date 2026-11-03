"""Optional baseline exercising DragonRadio's actual liquidDSP modem bindings.

Use the compiled embedded interpreter: ./dragonradio scripts/sim-radio.py ...
This deliberately uses sequential single-user frames, not OFDMA. No USRP/TAP is
created. Keeping this separate avoids claiming a reference model tests C++ MAC.
"""

import numpy as np


class NativeLiquidOFDM:
    def __init__(self, nfft, cp_len):
        try:
            from _dragonradio import liquid, radio
        except ImportError as exc:
            raise RuntimeError(
                "liquid-ofdm needs a compiled DragonRadio runtime. On Linux run "
                "./dragonradio scripts/sim-radio.py --phy liquid-ofdm. "
                "The portable OFDMA backend does not need the native runtime."
            ) from exc
        self.radio = radio
        mcs = liquid.MCS(liquid.CRCScheme("crc32"), liquid.FECScheme("none"),
                         liquid.FECScheme("none"), liquid.ModulationScheme("qpsk"))
        # Use the no-mask overload; the upstream custom-mask Python binding has
        # an unrelated conversion bug. This baseline uses default allocation.
        self.mod = liquid.OFDMModulator(mcs, nfft, cp_len, 0)
        self.mod.payload_mcs = mcs
        self.demods = {i: liquid.OFDMDemodulator(mcs, False, False, nfft, cp_len, 0) for i in (1, 2)}

    def modulate(self, packet):
        header = self.radio.Header(packet.source, packet.destination, packet.sequence % 65536)
        # The envelope includes a sequence number for exact verification without
        # depending on the upstream Header.seq binding's unregistered Seq type.
        return np.asarray(self.mod.modulate(header, packet.encode()), dtype=np.complex64)

    def receive(self, user_id, samples, expected):
        demod = self.demods[user_id]
        demod.reset()
        recovered = []
        # Exercise incremental native synchronizer input, including a final
        # flush. These boundaries intentionally differ from OFDM symbol sizes.
        for start in range(0, len(samples), 97):
            recovered.extend(demod.demodulate(samples[start:start + 97]))
        valid = [(h, p) for h, p, _stats in recovered if h is not None and p is not None]
        exact = any(h.curhop == expected.source and h.nexthop == user_id
                    and p == expected.encode() for h, p in valid)
        return exact, len(valid)
