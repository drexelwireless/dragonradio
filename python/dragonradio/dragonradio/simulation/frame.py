"""Frame-level packet/IQ boundary shared by simulation and SDR transports."""
from dataclasses import dataclass

import numpy as np

from .fast import NativeStream, make_phy
from .ofdma import Packet


@dataclass(frozen=True)
class PacketRecord:
    packet: Packet
    sample_offset: int
    sample_count: int


@dataclass
class ModulatedFrame:
    samples: np.ndarray
    records: tuple
    sample_rate: float
    implementation: str


class FrameEngine:
    """Batch several network packets into a shared waveform, retaining records.

    OFDM uses one allocation/packet; OFDMA uses two. Packet sequence numbers are
    independent. Engines are stateful and must not be shared between threads.
    """
    def __init__(self, config, implementation="auto", library=None, chase_bits=4, stream="auto"):
        if stream not in ("auto", "native", "python"):
            raise ValueError("stream must be auto, native or python")
        self.config = config
        self.stream = stream
        self.transmitter, self.receivers, self.implementation = make_phy(config, implementation, library, chase_bits)

    def build(self, packets):
        packets = tuple(packets)
        samples = self.transmitter.modulate(packets)
        records = tuple(PacketRecord(p, 0, len(samples)) for p in packets)
        return ModulatedFrame(samples, records, self.config.sample_rate, self.implementation)

    def receive(self, user_id, samples):
        return self.receivers[user_id].demodulate(samples)

    def stream_receiver(self, user_id):
        receiver = self.receivers[user_id]
        native = getattr(receiver, "supports_stream", False)
        if self.stream == "native" and not native:
            raise ValueError("persistent native stream requires the v2 native engine")
        if native and self.stream != "python":
            return NativeStream(receiver)
        return StreamReceiver(receiver, self.config)


class StreamReceiver:
    """Bounded-window acquisition/reassembly across arbitrary transport chunks.

    A successful decode consumes exactly through the end of its frame. Failed
    acquisition advances a search hop while retaining enough overlap. CRC-failed
    candidates retain overlap, allowing the next valid frame to decode.
    """
    def __init__(self, receiver, config, hop_samples=256):
        if hop_samples < 1:
            raise ValueError("hop must be positive")
        self.receiver, self.config, self.hop = receiver, config, hop_samples
        self.buffer = np.empty(0, dtype=np.complex64)
        self.sample_offset = 0

    def feed(self, samples, final=False):
        samples = np.asarray(samples, dtype=np.complex64)
        if samples.ndim != 1 or not np.all(np.isfinite(samples)):
            raise ValueError("stream must contain finite one-dimensional IQ")
        self.buffer = np.concatenate((self.buffer, samples))
        results = []
        frame = self.config.frame_samples
        cursor = 0
        # Consume views while processing this chunk. Copy only the final tail;
        # copying the entire remainder for every candidate becomes quadratic for
        # large capture/replay chunks, especially weak-link CRC failures.
        while len(self.buffer) - cursor >= frame + (0 if final else self.hop):
            limit = min(len(self.buffer) - cursor, frame + self.hop)
            result = self.receiver.demodulate(self.buffer[cursor:cursor + limit])
            acquired = result.sync_metric is not None and result.sync_metric >= 0.20
            if acquired:
                # A partial preamble at the right edge may produce a sidelobe
                # before enough of the real frame has arrived. Only a verified
                # packet justifies consuming a full frame. Preserve overlap on
                # failed candidates so noisy idle time cannot hide the first one.
                consumed = result.start_sample + (frame if result.ok else 1)
                result.start_sample += self.sample_offset + cursor
                results.append(result)
            else:
                consumed = min(self.hop, len(self.buffer) - cursor - frame + 1)
            cursor += consumed
        if cursor:
            self.buffer = self.buffer[cursor:].copy()
            self.sample_offset += cursor
        return results

    @property
    def pending_samples(self):
        return len(self.buffer)

    def reset(self):
        self.buffer = np.empty(0, dtype=np.complex64)
        self.sample_offset = 0


def normalize_iq(samples, peak=0.5):
    """Apply one linear scale, preserving OFDM orthogonality and avoiding clipping."""
    samples = np.asarray(samples, dtype=np.complex64)
    if samples.ndim != 1 or not samples.size or not np.all(np.isfinite(samples)) or not 0 < peak < 1:
        raise ValueError("invalid waveform or target peak")
    maximum = float(np.max(np.abs(samples)))
    scale = peak / maximum if maximum else 1.0
    return np.ascontiguousarray(samples * scale), scale


class NativePacketAdapter:
    """Adapter for the existing embedded _dragonradio packet classes.

    This is a frame boundary rather than a PacketModulator subclass: one native
    packet per allocation is passed together. C++ MAC selection/wiring remains a
    distinct integration task. The packet body retains native extended headers.
    """
    def __init__(self, engine):
        self.engine = engine

    def build(self, packets):
        converted = []
        for packet in packets:
            h = packet.hdr
            flags = sum(int(getattr(h.flags, key)) << bit for bit, key in enumerate(
                        ("syn", "ack", "has_seq", "has_control", "compressed"))) | (int(h.flags.team) << 5)
            converted.append(Packet(int(h.curhop), int(h.nexthop), int(h.seq), bytes([flags]) + packet.payload))
        return self.engine.build(converted)

    def receive(self, user_id, samples, radio_module=None, timestamp=None):
        if radio_module is None:
            from _dragonradio import radio as radio_module
        result = self.engine.receive(user_id, samples)
        if not result.ok or not result.packet.payload:
            return result, None
        p = result.packet
        h = radio_module.Header(p.source, p.destination, p.sequence % 65536)
        flags = p.payload[0]
        for bit, key in enumerate(("syn", "ack", "has_seq", "has_control", "compressed")):
            setattr(h.flags, key, (flags >> bit) & 1)
        h.flags.team = flags >> 5
        native = radio_module.RadioPacket(h, p.payload[1:])
        if timestamp is not None:
            native.timestamp = timestamp + result.start_sample / self.engine.config.sample_rate
        if result.evm_db is not None:
            native.evm = result.evm_db
        if result.cfo_subcarriers is not None:
            native.cfo = 2 * np.pi * result.cfo_subcarriers / self.engine.config.nfft
        return result, native
