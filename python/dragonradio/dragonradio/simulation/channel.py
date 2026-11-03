"""A discrete-time SISO channel, independently instantiated for each receiver."""

from dataclasses import dataclass
from typing import Optional, Tuple

import numpy as np


@dataclass(frozen=True)
class ChannelConfig:
    snr_db: Optional[float] = 30.0
    taps: Tuple[complex, ...] = (1.0 + 0j,)
    delay_samples: int = 0
    cfo_subcarriers: float = 0.0
    phase_radians: float = 0.0
    noise_power: Optional[float] = None

    def __post_init__(self):
        if self.delay_samples < 0 or not isinstance(self.delay_samples, int):
            raise ValueError("delay_samples must be a nonnegative integer")
        if not self.taps or not np.all(np.isfinite(self.taps)) or not np.any(self.taps):
            raise ValueError("taps must contain a finite, nonzero channel")
        if self.snr_db is not None and not np.isfinite(self.snr_db):
            raise ValueError("snr_db must be finite, or None for a noiseless channel")
        if self.noise_power is not None:
            if self.snr_db is not None or not np.isfinite(self.noise_power) or self.noise_power < 0:
                raise ValueError("fixed noise_power must be finite/nonnegative and requires snr_db=None")
        if not np.isfinite(self.cfo_subcarriers) or not np.isfinite(self.phase_radians):
            raise ValueError("CFO and phase must be finite")


class Channel:
    def __init__(self, config: ChannelConfig, seed: int = 0):
        self.config = config
        self.rng = np.random.default_rng(seed)

    def propagate(self, samples, nfft: int, trailing_samples: int = 512):
        """Apply FIR, delay, CFO/phase, and complex AWGN.

        SNR refers to mean post-FIR signal power during the active burst, before
        padding. With noise_power, complex noise variance stays fixed independently
        of FIR gain and snr_db must be None. CFO units are FFT subcarrier
        spacings (Fs / nfft). This is a
        burst transport, not a real-time USRP emulator.
        """
        x = np.asarray(samples, dtype=np.complex128)
        if x.ndim != 1 or not x.size or not np.all(np.isfinite(x)):
            raise ValueError("samples must be a nonempty finite complex vector")
        if nfft <= 0 or trailing_samples < 0:
            raise ValueError("invalid FFT size or trailing padding")
        cfg = self.config
        filtered = np.convolve(x, np.asarray(cfg.taps))
        power = float(np.mean(np.abs(filtered) ** 2))
        y = np.pad(filtered, (cfg.delay_samples, trailing_samples))
        phase = cfg.phase_radians + 2 * np.pi * cfg.cfo_subcarriers * np.arange(y.size) / nfft
        y = y * np.exp(1j * phase)
        if cfg.snr_db is not None or cfg.noise_power is not None:
            variance = cfg.noise_power if cfg.noise_power is not None else power * 10 ** (-cfg.snr_db / 10)
            sigma = np.sqrt(variance / 2)
            y += sigma * (self.rng.standard_normal(y.size) + 1j * self.rng.standard_normal(y.size))
        return y.astype(np.complex64)
