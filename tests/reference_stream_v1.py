"""Frozen pre-cursor-copy streaming receiver for software regression measurement."""
import numpy as np

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
        while len(self.buffer) >= frame + (0 if final else self.hop):
            limit = min(len(self.buffer), frame + self.hop)
            result = self.receiver.demodulate(self.buffer[:limit])
            acquired = result.sync_metric is not None and result.sync_metric >= 0.20
            if acquired:
                # A partial preamble at the right edge may produce a sidelobe
                # before enough of the real frame has arrived. Only a verified
                # packet justifies consuming a full frame. Preserve overlap on
                # failed candidates so noisy idle time cannot hide the first one.
                consumed = result.start_sample + (frame if result.ok else 1)
                result.start_sample += self.sample_offset
                results.append(result)
            else:
                consumed = min(self.hop, len(self.buffer) - frame + 1)
            self.buffer = self.buffer[consumed:].copy()
            self.sample_offset += consumed
        return results

    def reset(self):
        self.buffer = np.empty(0, dtype=np.complex64)
        self.sample_offset = 0

