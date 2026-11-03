"""Sample transports. UHD is imported only for an explicit hardware operation."""
from dataclasses import dataclass
import time

import numpy as np

from .channel import Channel
from .frame import normalize_iq


class SimulatedTransport:
    def __init__(self, config, channel, seed=2026):
        self.config = config
        self.channel = Channel(channel, seed)

    def exchange(self, frame):
        if frame.sample_rate != self.config.sample_rate:
            raise ValueError("frame/sample transport rates differ")
        return self.channel.propagate(frame.samples, self.config.nfft)


@dataclass(frozen=True)
class SDRConfig:
    device_args: str
    center_frequency: float
    sample_rate: float = 1e6
    gain_db: float = 0.0
    antenna: str = ""
    channel: int = 0
    clock_source: str = "internal"
    time_source: str = ""
    peak_amplitude: float = 0.5

    def __post_init__(self):
        if not self.device_args or self.center_frequency is None or not np.isfinite(self.center_frequency) or self.center_frequency <= 0:
            raise ValueError("explicit USRP device args and positive center frequency required")
        if not np.isfinite(self.sample_rate) or self.sample_rate <= 0 or not np.isfinite(self.gain_db):
            raise ValueError("invalid rate/gain")
        if not 0 < self.peak_amplitude < 1 or self.channel < 0:
            raise ValueError("invalid amplitude/channel")


class UHDTransport:
    """Finite timed TX or streaming RX; never instantiated by simulator defaults.

    Supply the UHD Python API from the target grid image. No flashing, node
    reservations, or device discovery is performed by these helpers.
    """
    def __init__(self, config, role, uhd_module=None):
        if role not in ("tx", "rx"):
            raise ValueError("role must be tx or rx")
        if uhd_module is None:
            import uhd as uhd_module
        self.uhd, self.config, self.role = uhd_module, config, role
        self.usrp = uhd_module.usrp.MultiUSRP(config.device_args)
        # N210 supports none/external/mimo for time source, not internal.
        # Empty strings preserve device defaults and do not wait for PPS.
        if config.clock_source:
            self.usrp.set_clock_source(config.clock_source)
        if config.time_source:
            self.usrp.set_time_source(config.time_source)
        for name, value in (("rate", config.sample_rate), ("freq", config.center_frequency), ("gain", config.gain_db)):
            getattr(self.usrp, "set_" + role + "_" + name)(value, config.channel)
        if config.antenna:
            getattr(self.usrp, "set_" + role + "_antenna")(config.antenna, config.channel)
        actual = getattr(self.usrp, "get_" + role + "_rate")(config.channel)
        if abs(actual - config.sample_rate) > 1:
            raise RuntimeError("USRP sample rate differs from PHY configuration")
        arguments = uhd_module.usrp.StreamArgs("fc32", "sc16")
        arguments.channels = [config.channel]
        self.stream = getattr(self.usrp, "get_" + role + "_stream")(arguments)

    def transmit(self, frame, lead_seconds=0.2, preserve_scale=False):
        if self.role != "tx" or frame.sample_rate != self.config.sample_rate or lead_seconds <= 0:
            raise ValueError("invalid TX role/rate/lead time")
        if preserve_scale:
            samples = np.ascontiguousarray(frame.samples, dtype=np.complex64)
            if (samples.ndim != 1 or not samples.size or not np.all(np.isfinite(samples)) or
                    np.max(np.abs(samples)) > self.config.peak_amplitude + 1e-7):
                raise ValueError("prepared waveform exceeds configured peak")
            scale = 1.0
        else:
            samples, scale = normalize_iq(frame.samples, self.config.peak_amplitude)
        samples = samples.reshape(1, -1)
        md = self.uhd.types.TXMetadata()
        md.start_of_burst, md.end_of_burst, md.has_time_spec = True, False, True
        md.time_spec = self.usrp.get_time_now() + self.uhd.types.TimeSpec(lead_seconds)
        completion_deadline = time.monotonic() + lead_seconds + samples.shape[1] / self.config.sample_rate + 2.0
        offset = 0
        try:
            while offset < samples.shape[1]:
                sent = self.stream.send(samples[:, offset:], md, 1.0)
                if sent <= 0 or sent > samples.shape[1] - offset:
                    raise RuntimeError("USRP TX made no valid progress")
                offset += sent
                md.start_of_burst, md.has_time_spec = False, False
        finally:
            md.start_of_burst, md.has_time_spec, md.end_of_burst = False, False, True
            self.stream.send(np.empty((1, 0), dtype=np.complex64), md, 1.0)
        # Report asynchronous underrun/late errors rather than silently accepting
        # sample enqueue as evidence of a successful RF transmission.
        event = self.uhd.types.TXAsyncMetadata()
        events = []
        acknowledged = False
        while time.monotonic() < completion_deadline and not acknowledged:
            if self.stream.recv_async_msg(event, 0.1):
                if event.event_code == self.uhd.types.TXMetadataEventCode.burst_ack:
                    acknowledged = True
                else:
                    events.append(str(event.event_code))
                    break
        if events:
            raise RuntimeError("USRP TX asynchronous errors: " + ", ".join(events))
        if not acknowledged:
            raise RuntimeError("USRP did not acknowledge burst completion")
        return dict(samples=offset, linear_scale=scale, peak=float(np.max(np.abs(samples))))

    def receive(self, decoder, seconds=5.0, on_ready=None):
        if self.role != "rx" or not np.isfinite(seconds) or seconds <= 0:
            raise ValueError("invalid RX role/duration")
        md = self.uhd.types.RXMetadata()
        buffer = np.empty((1, self.stream.get_max_num_samps()), dtype=np.complex64)
        start = self.uhd.types.StreamCMD(self.uhd.types.StreamMode.start_cont)
        start.stream_now = True
        self.stream.issue_stream_cmd(start)
        results = []
        deadline = time.monotonic() + seconds
        try:
            while time.monotonic() < deadline:
                count = self.stream.recv(buffer, md, min(0.5, max(0.01, deadline - time.monotonic())))
                if md.error_code == self.uhd.types.RXMetadataErrorCode.timeout:
                    continue
                if md.error_code != self.uhd.types.RXMetadataErrorCode.none:
                    decoder.reset()
                    raise RuntimeError("USRP RX discontinuity: " + str(md.error_code))
                if count:
                    if on_ready is not None:
                        on_ready(md)
                        on_ready = None
                    results.extend(decoder.feed(buffer[0, :count]))
        finally:
            stop = self.uhd.types.StreamCMD(self.uhd.types.StreamMode.stop_cont)
            self.stream.issue_stream_cmd(stop)
        results.extend(decoder.feed(np.empty(0, dtype=np.complex64), final=True))
        return results
