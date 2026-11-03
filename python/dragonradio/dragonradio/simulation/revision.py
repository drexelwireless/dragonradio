"""Benchmark studies for the native PHY revision (v1 versus v2 versus legacy).

Every study is simulation only. Decoders receive IQ and static configuration,
never expected payloads; verification happens after decoding. Packets and
channel draws are matched across modes for the same (profile, SNR, seed,
sequence, user) so mode differences are paired.
"""
import hashlib
import math
import os
from pathlib import Path
import platform
import resource
import time

import numpy as np

from .channel import Channel, ChannelConfig
from .fast import FastReceiver, FastTransmitter, library_path
from .frame import FrameEngine
from .liquid import LiquidOFDM
from .ofdma import OFDMAConfig, Packet, _hard_bits, ofdm_config

# name -> (engine family, OFDMA?, modulation, chase bits)
MODES = {
    "legacy-qpsk": ("legacy", False, "qpsk", None),
    "legacy-16qam": ("legacy", False, "16qam", None),
    "v1-ofdm-qpsk": ("v1", False, "qpsk", 4),
    "v1-ofdma-qpsk": ("v1", True, "qpsk", 4),
    "v2-ofdm-qpsk": ("v2", False, "qpsk", 4),
    "v2-ofdma-qpsk": ("v2", True, "qpsk", 4),
    "v2-ofdm-16qam": ("v2", False, "16qam", 4),
    "v2-ofdma-16qam": ("v2", True, "16qam", 4),
    "v2-ofdm-qpsk-nochase": ("v2", False, "qpsk", 0),
}
PROFILES = ("awgn", "multipath-cp", "beyond-cp", "rayleigh", "sfo-50ppm",
            "cfo+0.18", "cfo-0.18", "cfo+0.6", "cfo-0.6")
PAYLOAD = 256
ACTIVE_TONES = 96   # data + pilot tones; training uses the same 96 bins


def wilson(k, n, z=1.959963984540054):
    if n == 0:
        return [0.0, 1.0]
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return [max(0.0, c - h), min(1.0, c + h)]


def qpsk_ber(ebno_db):
    return 0.5 * math.erfc(math.sqrt(10 ** (ebno_db / 10)))


def qam16_ber(ebno_db):
    """Exact Gray square 16-QAM bit error probability in AWGN."""
    x = math.sqrt(0.4 * 10 ** (ebno_db / 10))
    return 0.375 * math.erfc(x) + 0.25 * math.erfc(3 * x) - 0.125 * math.erfc(5 * x)


def theory_ber(modulation, ebno_db):
    return qpsk_ber(ebno_db) if modulation == "qpsk" else qam16_ber(ebno_db)


def ebno_from_sample_snr(snr_db, bits_per_symbol, nfft=128):
    """Native frames: unit-power active tones, unitary FFT, SNR on TX frame power."""
    return snr_db + 10 * math.log10(nfft / ACTIVE_TONES) - 10 * math.log10(bits_per_symbol)


def iid_per(modulation, ebno_db, payload=PAYLOAD):
    bits = (16 + payload) * 8
    pb = theory_ber(modulation, ebno_db)
    return -math.expm1(bits * math.log1p(-pb)) if pb < 1 else 1.0


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def provenance(root):
    libs = {}
    for name in ("phy", "phy-v1", "modem"):
        for lib in (Path(root) / "build" / name).glob("libdragonradio_*"):
            if lib.suffix in (".so", ".dylib"):
                libs[str(lib.relative_to(root))] = digest(lib)
    sources = {}
    for base in ("src/simulation", "tests/reference_native_v1", "python/dragonradio/dragonradio/simulation"):
        for f in sorted((Path(root) / base).rglob("*")):
            if f.is_file() and f.suffix in (".py", ".cc", ".hh"):
                sources[str(f.relative_to(root))] = digest(f)
    return dict(platform=platform.platform(), machine=platform.machine(), python=platform.python_version(),
                numpy=np.__version__, logical_cpus=os.cpu_count(), libraries=libs, sources=sources,
                thread_env={k: os.environ.get(k) for k in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS")})


def config_for(mode, user_id=1, payload=PAYLOAD):
    family, ofdma, modulation, _ = MODES[mode]
    if ofdma:
        return OFDMAConfig(max_payload=payload, modulation=modulation)
    return ofdm_config(user_id=user_id, max_payload=payload, modulation=modulation)


class Modem:
    """One mode's transmitter and per-user receivers (cached per process)."""
    def __init__(self, mode, payload=PAYLOAD):
        family, ofdma, modulation, chase = MODES[mode]
        self.mode, self.family, self.ofdma, self.payload = mode, family, ofdma, payload
        self.cfg = config_for(mode, payload=payload)
        self.users = (1, 2) if ofdma else (1,)
        if family == "legacy":
            self.liquid = LiquidOFDM(self.cfg)
        else:
            lib = str(library_path("reference-v1" if family == "v1" else "current"))
            self.tx = FastTransmitter(self.cfg, library=lib)
            self.rx = {u: FastReceiver(u, self.cfg, library=lib, chase_bits=chase) for u in self.users}

    def modulate(self, packets):
        if self.family == "legacy":
            return self.liquid.modulate(packets[0])
        return self.tx.modulate(packets if self.ofdma else packets[:1])

    def receive(self, user, iq, expected):
        """Returns (delivered, false_accept, acquired, chase_trials)."""
        if self.family == "legacy":
            got = self.liquid.demodulate(iq)
            if got is None:
                return False, False, None, None
            return got == expected.encode(), got != expected.encode(), None, None
        d = self.rx[user].demodulate(iq)
        acquired = d.error != "preamble acquisition failed"
        if d.ok:
            return d.packet == expected, d.packet != expected, acquired, d.correction_trials
        return False, False, acquired, d.correction_trials


_MODEMS = {}


def modem(mode, payload=PAYLOAD):
    key = (mode, payload)
    if key not in _MODEMS:
        _MODEMS[key] = Modem(mode, payload)
    return _MODEMS[key]


def impair(profile, snr, waveform, seed, nfft=128):
    """Channel with fixed receiver noise set from TX active-frame power."""
    rng = np.random.default_rng(seed)
    power = float(np.mean(np.abs(waveform) ** 2))
    taps, cfo = (1 + 0j,), 0.0
    if profile.startswith("cfo"):
        cfo = float(profile[3:])
    elif profile == "multipath-cp":
        taps = (1 + 0j, .3j, 0j, .15 + 0j)
    elif profile == "beyond-cp":
        taps = (1 + 0j,) + (0j,) * 16 + (.6j,)
    elif profile == "rayleigh":
        taps = (complex(rng.standard_normal(), rng.standard_normal()) / math.sqrt(2),)
    elif profile not in ("awgn", "sfo-50ppm"):
        raise ValueError(profile)
    if profile in ("multipath-cp", "beyond-cp"):
        norm = math.sqrt(sum(abs(t) ** 2 for t in taps))
        taps = tuple(t / norm for t in taps)
    noise = None if snr is None else power * 10 ** (-snr / 10)
    iq = Channel(ChannelConfig(snr_db=None, taps=taps, delay_samples=23, cfo_subcarriers=cfo,
                               noise_power=noise), seed).propagate(waveform, nfft)
    if profile == "sfo-50ppm":
        t = np.arange(len(iq)) * (1 + 50e-6)
        iq = (np.interp(t, np.arange(len(iq)), iq.real) + 1j * np.interp(t, np.arange(len(iq)), iq.imag)).astype(np.complex64)
    return iq


def packet_pair(seed, sequence, payload=PAYLOAD):
    rng = np.random.default_rng(seed * 1_000_003 + sequence)
    return [Packet(0, u, sequence, rng.bytes(payload)) for u in (1, 2)]


def channel_seed(profile, snr, seed, sequence, user):
    tag = PROFILES.index(profile) if profile in PROFILES else 97
    return (((seed * 131 + tag) * 1009 + int(round((snr if snr is not None else -99) * 10)) + 5000) * 100003
            + sequence) * 7 + user


def practical_cell(task):
    """One (mode, profile, snr, seed) block of `frames` frames."""
    mode, profile, snr, seed, frames, payload = task
    m = modem(mode, payload)
    users = {u: dict(delivered=0, false_accepts=0, acquired=0, chase_trials=0, chase_frames=0) for u in m.users}
    airtime = 0
    for sequence in range(frames):
        pair = packet_pair(seed, sequence, payload)
        waveform = m.modulate(pair)
        airtime += len(waveform)
        for u in m.users:
            iq = impair(profile, snr, waveform, channel_seed(profile, snr, seed, sequence, u))
            ok, bad, acquired, trials = m.receive(u, iq, pair[u - 1])
            r = users[u]
            r["delivered"] += ok
            r["false_accepts"] += bad
            if acquired is not None:
                r["acquired"] += acquired
            if trials is not None:
                r["chase_trials"] += trials
                r["chase_frames"] += 1
    return dict(mode=mode, profile=profile, snr_db=snr, seed=seed, frames=frames, payload=payload,
                airtime_samples=airtime, users=users)


def aggregate_practical(cells, sample_rate=1e6):
    groups = {}
    for c in cells:
        groups.setdefault((c["mode"], c["profile"], c["snr_db"], c["payload"]), []).append(c)
    rows = []
    for (mode, profile, snr, payload), block in sorted(groups.items(), key=lambda kv: (kv[0][0], kv[0][1], kv[0][3], kv[0][2] if kv[0][2] is not None else 1e9)):
        _, ofdma, modulation, _ = MODES[mode]
        frames = sum(c["frames"] for c in block)
        airtime = sum(c["airtime_samples"] for c in block) / sample_rate
        users = {}
        delivered_bits = 0
        for u in block[0]["users"]:
            k = sum(c["users"][u]["delivered"] for c in block)
            per_seed = [c["users"][u]["delivered"] / c["frames"] for c in block]
            delivered_bits += 8 * payload * k
            trials = sum(c["users"][u]["chase_trials"] for c in block)
            tframes = sum(c["users"][u]["chase_frames"] for c in block)
            users[str(u)] = dict(delivered=k, packets=frames, pdr=k / frames, pdr_wilson95=wilson(k, frames),
                                 pdr_seed_std=float(np.std(per_seed)),
                                 false_accepts=sum(c["users"][u]["false_accepts"] for c in block),
                                 acquired=sum(c["users"][u]["acquired"] for c in block) if MODES[mode][0] != "legacy" else None,
                                 mean_chase_trials=trials / tframes if tframes else None)
        bps = 2 if modulation == "qpsk" else 4
        row = dict(mode=mode, profile=profile, snr_db=snr, payload=payload, frames=frames, seeds=len(block),
                   users=users, airtime_seconds=airtime, aggregate_goodput_bps=delivered_bits / airtime,
                   frame_samples=block[0]["airtime_samples"] / block[0]["frames"])
        if snr is not None and MODES[mode][0] != "legacy":
            ebno = ebno_from_sample_snr(snr, bps)
            row["theory"] = dict(ebno_db=ebno, ber=theory_ber(modulation, ebno), iid_per=iid_per(modulation, ebno, payload),
                                 note="independent hard-bit packet reference; excludes acquisition, channel estimation and Chase")
        rows.append(row)
    return rows


def oracle_cell(task):
    """Known timing/channel BER for one (modulation, Eb/N0, seed)."""
    modulation, ebno, seed, frames = task
    cfg = ofdm_config(modulation=modulation)
    tx = FastTransmitter(cfg)
    bps = cfg.bits_per_symbol
    bins = np.array(cfg.allocations[0].data) % cfg.nfft
    rng = np.random.default_rng(seed * 7919 + int(ebno * 10) + (0 if modulation == "qpsk" else 500000))
    errors = total = packet_errors = 0
    for sequence in range(frames):
        packet = Packet(0, 1, sequence, rng.bytes(PAYLOAD))
        waveform = tx.modulate([packet])
        grid = np.fft.fft(waveform.reshape(-1, cfg.symbol_samples)[:, cfg.cp_len:], axis=1, norm="ortho")
        symbols = grid[2:, bins].reshape(-1)
        es = float(np.mean(np.abs(symbols) ** 2))
        n0 = es / (bps * 10 ** (ebno / 10))
        received = waveform + math.sqrt(n0 / 2) * (rng.standard_normal(len(waveform)) + 1j * rng.standard_normal(len(waveform)))
        detected = np.fft.fft(received.reshape(-1, cfg.symbol_samples)[:, cfg.cp_len:], axis=1, norm="ortho")[2:, bins].reshape(-1)
        length = len(packet.encode()) * 8
        count = int(np.count_nonzero(_hard_bits(detected, bps)[:length] != _hard_bits(symbols, bps)[:length]))
        errors += count
        total += length
        packet_errors += count > 0
    return dict(modulation=modulation, ebno_db=ebno, seed=seed, bits=total, bit_errors=errors,
                packets=frames, packet_errors=packet_errors)


def aggregate_oracle(cells):
    groups = {}
    for c in cells:
        groups.setdefault((c["modulation"], c["ebno_db"]), []).append(c)
    rows = []
    for (modulation, ebno), block in sorted(groups.items()):
        bits = sum(c["bits"] for c in block)
        errors = sum(c["bit_errors"] for c in block)
        theory = theory_ber(modulation, ebno)
        tolerance = max(6 * math.sqrt(theory * (1 - theory) / bits), 6 / bits)
        rows.append(dict(modulation=modulation, ebno_db=ebno, bits=bits, bit_errors=errors, ber=errors / bits,
                         ber_wilson95=wilson(errors, bits), theory_ber=theory,
                         theory_consistent_6sigma=abs(errors / bits - theory) <= tolerance,
                         packets=sum(c["packets"] for c in block),
                         packet_error_rate=sum(c["packet_errors"] for c in block) / sum(c["packets"] for c in block),
                         iid_per=iid_per(modulation, ebno)))
    return rows


def cfo_cell(task):
    mode, cfo, snr, trials = task
    m = modem(mode)
    ok = {u: 0 for u in m.users}
    for i in range(trials):
        pair = packet_pair(77, i)
        waveform = m.modulate(pair)
        for u in m.users:
            noise = None if snr is None else float(np.mean(np.abs(waveform) ** 2)) * 10 ** (-snr / 10)
            iq = Channel(ChannelConfig(snr_db=None, delay_samples=23 + i % 17, cfo_subcarriers=cfo, noise_power=noise),
                         channel_seed("awgn", snr, 77, i, u)).propagate(waveform, 128)
            ok[u] += m.receive(u, iq, pair[u - 1])[0]
    return dict(mode=mode, cfo_subcarriers=cfo, snr_db=snr, trials=trials,
                pdr={str(u): ok[u] / trials for u in m.users})


# ---------------------------------------------------------------- software
def _stats(samples_us):
    a = np.asarray(samples_us)
    return dict(n=int(a.size), median_us=float(np.median(a)), p90_us=float(np.percentile(a, 90)),
                p99_us=float(np.percentile(a, 99)), mean_us=float(a.mean()))


def kernel_timing(mode, iterations=3000, snr=20.0):
    """Warm per-frame TX and burst RX time (one frame plus idle margins)."""
    m = modem(mode)
    pair = packet_pair(5, 0)
    waveform = m.modulate(pair)
    iq = impair("awgn", snr, waveform, 12345)
    tx, rx = [], {u: [] for u in m.users}
    for _ in range(50):
        m.modulate(pair)
        for u in m.users:
            m.receive(u, iq, pair[u - 1])
    for _ in range(iterations):
        t = time.perf_counter_ns(); m.modulate(pair); tx.append((time.perf_counter_ns() - t) / 1000)
        for u in m.users:
            t = time.perf_counter_ns()
            ok = m.receive(u, iq, pair[u - 1])[0]
            rx[u].append((time.perf_counter_ns() - t) / 1000)
            if not ok:
                raise RuntimeError("kernel timing input failed to decode: " + mode)
    return dict(mode=mode, frame_samples=len(waveform), tx=_stats(tx), rx={str(u): _stats(v) for u, v in rx.items()},
                scope="warm, one frame per call, includes Python/ctypes call overhead; excludes channel simulation")


STREAMS = {  # name -> (mode, stream implementation)
    "legacy-qpsk": ("legacy-qpsk", None),
    "v1-ofdm-qpsk": ("v1-ofdm-qpsk", "python"),
    "v2-ofdm-qpsk": ("v2-ofdm-qpsk", "native"),
    "v2-ofdm-qpsk-pystream": ("v2-ofdm-qpsk", "python"),
    "v1-ofdma-qpsk": ("v1-ofdma-qpsk", "python"),
    "v2-ofdma-qpsk": ("v2-ofdma-qpsk", "native"),
    "legacy-16qam": ("legacy-16qam", None),
    "v2-ofdm-16qam": ("v2-ofdm-16qam", "native"),
    "v2-ofdma-16qam": ("v2-ofdma-16qam", "native"),
}


def stream_corpus(mode, kind, seconds, snr=25.0, seed=8101):
    """Idle: receiver noise only. Loaded: saturated frames, 64-sample gaps."""
    rng = np.random.default_rng(seed)
    n = int(seconds * 1e6)
    noise = lambda count: ((rng.standard_normal(count) + 1j * rng.standard_normal(count)) * 1e-3).astype(np.complex64)
    expected = {1: [], 2: []}
    if kind == "idle":
        return noise(n), expected
    m = modem(mode)
    family, ofdma, _, _ = MODES[mode]
    pieces, total, i = [], 0, 0
    while total < n:
        pair = packet_pair(seed, i)
        if ofdma:
            frames = [(m.modulate(pair), (1, 2))]
        elif family == "legacy":
            frames = [(m.liquid.modulate(pair[0]), (1,)), (m.liquid.modulate(pair[1]), (2,))]
        else:
            frames = [(m.tx.modulate(pair[:1]), (1,)), (_tdma_tx(mode).modulate(pair[1:]), (2,))]
        for samples, users in frames:
            pieces += [samples, np.zeros(64, np.complex64)]
            total += len(samples) + 64
            for u in users:
                expected[u].append(pair[u - 1])
        i += 1
    signal = np.concatenate(pieces)
    power = float(np.mean(np.abs(signal) ** 2))
    sigma = math.sqrt(power * 10 ** (-snr / 10) / 2)
    signal = signal + (sigma * (rng.standard_normal(signal.size) + 1j * rng.standard_normal(signal.size))).astype(np.complex64)
    return signal.astype(np.complex64), expected


_TDMA = {}


def _tdma_tx(mode):
    """Second-destination transmitter for alternating full-band OFDM traffic."""
    if mode not in _TDMA:
        family, _, modulation, _ = MODES[mode]
        lib = str(library_path("reference-v1" if family == "v1" else "current"))
        _TDMA[mode] = FastTransmitter(ofdm_config(user_id=2, modulation=modulation), library=lib)
    return _TDMA[mode]


def stream_timing(name, kind, chunk, seconds, repetitions):
    """CPU per second of represented airtime, summed over every receiver."""
    mode, implementation = STREAMS[name]
    family, ofdma, modulation, chase = MODES[mode]
    corpus, expected = stream_corpus(mode, kind, seconds)
    users = (1, 2)
    lib = None if family == "legacy" else str(library_path("reference-v1" if family == "v1" else "current"))
    cpu, wall = [], []
    for _ in range(repetitions):
        receivers = {}
        for u in users:
            if family == "legacy":
                receivers[u] = LiquidOFDM(ofdm_config(modulation=modulation))
            else:
                cfg = OFDMAConfig(modulation=modulation) if ofdma else ofdm_config(user_id=u, modulation=modulation)
                receivers[u] = FrameEngine(cfg, "native", library=lib, chase_bits=chase, stream=implementation).stream_receiver(u)
        c0, w0 = time.process_time(), time.perf_counter()
        delivered = {}
        for u, r in receivers.items():
            got = []
            for i in range(0, len(corpus), chunk):
                if family == "legacy":
                    got.extend(p for p in r.receive_many(corpus[i:i + chunk]) if p.destination == u)
                else:
                    got.extend(x.packet for x in r.feed(corpus[i:i + chunk]) if x.ok)
            if family != "legacy":
                got.extend(x.packet for x in r.feed([], final=True) if x.ok)
            delivered[u] = got
        cpu.append(time.process_time() - c0)
        wall.append(time.perf_counter() - w0)
        for u in users:
            if delivered[u] != expected[u]:
                raise RuntimeError("stream verification failed: %s %s chunk %d user %d (%d of %d)" %
                                   (name, kind, chunk, u, len(delivered[u]), len(expected[u])))
    represented = len(corpus) / 1e6
    return dict(stream=name, mode=mode, implementation=implementation or "liquid", kind=kind, chunk_samples=chunk,
                receivers=len(users), represented_seconds=represented, packets_per_user=len(expected[1]),
                cpu_seconds=cpu, wall_seconds=wall,
                cpu_ms_per_airtime_second=1000 * float(np.median(cpu)) / represented,
                cpu_ms_per_airtime_second_per_receiver=1000 * float(np.median(cpu)) / represented / len(users),
                peak_rss_bytes=int(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * (1 if platform.system() == "Darwin" else 1024)),
                all_payloads_verified=True,
                scope="two receivers (one per destination) replay the same capture sequentially; CPU includes receiver construction")
