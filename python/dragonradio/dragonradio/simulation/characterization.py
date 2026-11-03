"""Theory-calibrated channel and process-isolated software characterization.

Oracle BER is a diagnostic with known timing/channel, never a production decoder.
The practical PHY decoders receive IQ/config only, not expected payloads.
"""
import hashlib
import math
from pathlib import Path
import platform
import resource
import time

import numpy as np
from .channel import Channel, ChannelConfig
from .fast import FastTransmitter, FastReceiver
from .frame import FrameEngine
from .liquid import LiquidOFDM
from .ofdma import OFDMAConfig, OFDMATransmitter, Packet, ofdm_config, _hard_bits
from .ota import MODES, wilson

PROFILES = ('awgn', 'cfo-positive', 'cfo-negative', 'multipath-cp', 'beyond-cp', 'rayleigh', 'sfo-50ppm')


def percentiles(values):
    return dict(zip(('p50_us', 'p95_us', 'p99_us'), map(float, np.percentile(values, (50, 95, 99)))))


def qpsk_ber(ebno_db):
    return .5 * math.erfc(math.sqrt(10**(ebno_db/10)))


def oracle_study(frames, seeds, ebnos=(0, 2, 4, 6, 8, 10)):
    rows = []
    for mode in ('native-ofdm', 'native-ofdma'):
        cfg = ofdm_config() if mode == 'native-ofdm' else OFDMAConfig()
        tx = FastTransmitter(cfg)
        allocation = cfg.allocations[0]
        bins = np.array(allocation.data) % cfg.nfft
        for ebno in ebnos:
            errors, total, packet_errors, packets_count, sample_snr = 0, 0, 0, 0, []
            for seed in seeds:
                rng = np.random.default_rng(seed)
                for sequence in range(frames):
                    packets = [Packet(0, a.user_id, sequence, rng.bytes(256)) for a in cfg.allocations]
                    waveform = tx.modulate(packets)
                    grid = np.fft.fft(waveform.reshape(-1, cfg.symbol_samples)[:, cfg.cp_len:], axis=1, norm='ortho')
                    symbols = grid[2:, bins].reshape(-1)
                    es = float(np.mean(np.abs(symbols)**2))
                    n0 = es/(2*10**(ebno/10))
                    noise = math.sqrt(n0/2)*(rng.standard_normal(len(waveform))+1j*rng.standard_normal(len(waveform)))
                    received = waveform+noise
                    detected = np.fft.fft(received.reshape(-1, cfg.symbol_samples)[:, cfg.cp_len:],axis=1,norm='ortho')[2:,bins].reshape(-1)
                    length = len(packets[0].encode())*8
                    # Compare scrambled transmitted bits directly. Scrambling is
                    # bijective and does not change bit-error counts.
                    count = int(np.count_nonzero(_hard_bits(detected)[:length] != _hard_bits(symbols)[:length]))
                    errors += count
                    total += length
                    packet_errors += count > 0
                    packets_count += 1
                    sample_snr.append(10*math.log10(float(np.mean(np.abs(waveform)**2))/n0))
            theory = qpsk_ber(ebno)
            tolerance = max(6*math.sqrt(theory*(1-theory)/total), 6/total)
            bits_per_packet = length
            iid_per = -math.expm1(bits_per_packet*math.log1p(-theory))
            rows.append(dict(mode=mode, ebno_db=ebno, mean_time_sample_snr_db=float(np.mean(sample_snr)),
                bits=total, bit_errors=errors, ber=errors/total, ber_wilson95=wilson(errors,total),
                theoretical_ber=theory, theory_consistent_6sigma=abs(errors/total-theory)<=tolerance,
                oracle_packet_error_rate=packet_errors/packets_count, iid_hard_decision_per=iid_per,
                nominal_payload_bps=256*8*len(cfg.allocations)*cfg.sample_rate/cfg.frame_samples,
                iid_reference_goodput_bps=256*8*len(cfg.allocations)*cfg.sample_rate/cfg.frame_samples*(1-iid_per),
                interpretation='known timing/channel; QPSK bit Eb excludes CP/pilots/training/header; not practical decoder BER'))
    return rows


def channel_config(profile, snr, waveform, rng):
    # Fix thermal noise from TRANSMITTED reference power, not faded receive
    # power, so a Rayleigh fade or attenuation does not magically lower noise.
    power = float(np.mean(np.abs(waveform)**2))
    taps, cfo = (1+0j,), 0.
    if profile == 'cfo-positive': cfo = .18
    elif profile == 'cfo-negative': cfo = -.18
    elif profile == 'multipath-cp': taps = (1+0j, .3j, 0j, .15+0j)
    elif profile == 'beyond-cp': taps = (1+0j,)+(0j,)*16+(.6j,)
    elif profile == 'rayleigh': taps = (complex(rng.standard_normal(),rng.standard_normal())/math.sqrt(2),)
    elif profile not in ('awgn', 'sfo-50ppm'): raise ValueError(profile)
    if profile in ('multipath-cp','beyond-cp'):
        norm = math.sqrt(sum(abs(t)**2 for t in taps))
        taps = tuple(t/norm for t in taps)
    return ChannelConfig(snr_db=None, taps=taps, delay_samples=23,
                         cfo_subcarriers=cfo, noise_power=power*10**(-snr/10))


def practical_study(frames, seeds, snrs, profiles, payloads=(64, 256)):
    rows = []
    for size in payloads:
        configs = {'native-ofdm':ofdm_config(max_payload=size), 'native-ofdma':OFDMAConfig(max_payload=size)}
        engines = {m:(FastTransmitter(c), {a.user_id:FastReceiver(a.user_id,c) for a in c.allocations}) for m,c in configs.items()}
        liquid = LiquidOFDM(ofdm_config(max_payload=size))
        for profile in profiles:
            for snr in snrs:
                stats = {m:{u:dict(delivered=0, false_accept=0, acquisition_failed=0, correction_trials=0,
                        tx_us=[], rx_us=[], samples=0, evm_db=[]) for u in ((1,2) if m=='native-ofdma' else (1,))} for m in MODES}
                blocks = []
                for seed in seeds:
                    block = {m:{u:0 for u in stats[m]} for m in MODES}
                    for sequence in range(frames):
                        rng = np.random.default_rng(seed*100000+sequence)
                        pair = [Packet(0,u,sequence,rng.bytes(size)) for u in (1,2)]
                        # Rotate mode order for measurement fairness.
                        order = list(MODES)
                        shift = sequence%3
                        for mode in order[shift:]+order[:shift]:
                            start = time.perf_counter_ns()
                            waveform = liquid.modulate(pair[0]) if mode=='legacy-ofdm' else engines[mode][0].modulate(pair if mode=='native-ofdma' else pair[:1])
                            tx_us = (time.perf_counter_ns()-start)/1000
                            for user in stats[mode]:
                                r = stats[mode][user]
                                # Same channel draw for matched OFDM candidates;
                                # second OFDMA receiver is independent.
                                crng = np.random.default_rng(seed*100000+sequence+user*71)
                                cc = channel_config(profile,snr,waveform,crng)
                                iq = Channel(cc,seed*100000+sequence+user*71).propagate(waveform,128)
                                if profile == 'sfo-50ppm':
                                    t = np.arange(len(iq))*(1+50e-6)
                                    iq = (np.interp(t,np.arange(len(iq)),iq.real)+1j*np.interp(t,np.arange(len(iq)),iq.imag)).astype(np.complex64)
                                start = time.perf_counter_ns()
                                if mode == 'legacy-ofdm':
                                    recovered = liquid.demodulate(iq)
                                    ok = recovered == pair[user-1].encode()
                                    accepted = recovered is not None
                                else:
                                    result = engines[mode][1][user].demodulate(iq)
                                    ok, accepted = result.packet == pair[user-1], result.ok
                                    r['acquisition_failed'] += result.sync_metric is None or result.sync_metric < .2
                                    r['correction_trials'] += result.correction_trials
                                    if result.evm_db is not None: r['evm_db'].append(result.evm_db)
                                r['rx_us'].append((time.perf_counter_ns()-start)/1000)
                                r['tx_us'].append(tx_us)
                                r['samples'] += len(waveform)
                                r['delivered'] += ok
                                r['false_accept'] += accepted and not ok
                                block[mode][user] += ok
                    blocks.append(dict(seed=seed, delivered=block))
                for mode in MODES:
                    users = {}
                    for user,r in stats[mode].items():
                        count = frames*len(seeds)
                        users[user] = dict(delivered=r['delivered'],packets=count,per=1-r['delivered']/count,
                            pdr_wilson95=wilson(r['delivered'],count),false_accepts=r['false_accept'],
                            acquisition_failures=r['acquisition_failed'] if mode!='legacy-ofdm' else None,
                            mean_correction_trials=r['correction_trials']/count if mode!='legacy-ofdm' else None,
                            median_evm_db=float(np.median(r['evm_db'])) if r['evm_db'] else None,
                            tx=percentiles(r['tx_us']),rx=percentiles(r['rx_us']),
                            payload_goodput_bps=r['delivered']*size*8/(r['samples']/1e6),
                            # Composite TX is paid once by the transmitter, not once per receiver.
                            rx_cpu_us_per_verified_packet=sum(r['rx_us'])/r['delivered'] if r['delivered'] else None)
                    rows.append(dict(mode=mode,payload_bytes=size,profile=profile,reference_snr_db=snr,
                        noise_definition='fixed complex variance from transmitted active frame power; fading retained',
                        users=users,aggregate_payload_goodput_bps=sum(u['payload_goodput_bps'] for u in users.values()),
                        seed_blocks=blocks,raw_ber=None,
                        raw_ber_note='CRC-valid-only BER is biased; practical pre-CRC bit export not implemented'))
    return rows


def software_worker(mode, frames=100, repetitions=3, reference_stream=False):
    cfg = OFDMAConfig() if mode=='native-ofdma' else ofdm_config()
    start = time.perf_counter_ns()
    if mode=='legacy-ofdm':
        engine = LiquidOFDM(cfg)
    else:
        engine = FrameEngine(cfg,'native')
    init_us = (time.perf_counter_ns()-start)/1000
    rng = np.random.default_rng(8101)
    chunks, expected = [], {1:[],2:[]}
    for i in range(frames):
        packets = [Packet(0,u,i,rng.bytes(256)) for u in (1,2)]
        if mode=='native-ofdma':
            pieces = [engine.build(packets).samples]
        elif mode=='legacy-ofdm':
            pieces = [engine.modulate(p) for p in packets]
        else:
            # TDMA destinations use their own scrambler/pilot identity.
            if i == 0: other = FrameEngine(ofdm_config(user_id=2),'native')
            pieces = [engine.build([packets[0]]).samples,other.build([packets[1]]).samples]
        for p in packets: expected[p.destination].append(p)
        for piece in pieces: chunks.extend((piece,np.zeros(64,np.complex64)))
    signal = np.concatenate(chunks)
    # Real RF idle time contains receiver noise. Equal duration per backend.
    noise = (rng.standard_normal(100000)+1j*rng.standard_normal(100000))*.001
    corpus = np.concatenate((noise,signal,noise)).astype(np.complex64)
    measurements = []
    reference_class = None
    reference_hash = None
    if reference_stream:
        import importlib.util
        path = Path(__file__).resolve().parents[4]/'tests/reference_stream_v1.py'
        reference_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        spec = importlib.util.spec_from_file_location('reference_stream_v1',path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        reference_class = module.StreamReceiver
    for size in (97,512,4096,65536,len(corpus)):
        walls, cpus, attempts = [], [], []
        for rep in range(repetitions):
            delivered = {}
            cpu, wall = time.process_time(),time.perf_counter()
            for user in (1,2):
                if mode=='legacy-ofdm':
                    receiver = LiquidOFDM(cfg)
                    packets = []
                    for offset in range(0,len(corpus),size):
                        packets.extend(p for p in receiver.receive_many(corpus[offset:offset+size]) if p.destination==user)
                else:
                    user_cfg = cfg if mode=='native-ofdma' else ofdm_config(user_id=user)
                    user_engine = FrameEngine(user_cfg,'native')
                    receiver = reference_class(user_engine.receivers[user],user_cfg) if reference_class else user_engine.stream_receiver(user)
                    results = []
                    for offset in range(0,len(corpus),size): results.extend(receiver.feed(corpus[offset:offset+size]))
                    results.extend(receiver.feed([],final=True))
                    packets = [r.packet for r in results if r.ok]
                delivered[user] = packets == expected[user]
            cpus.append(time.process_time()-cpu)
            walls.append((time.perf_counter()-wall)*1e6)
            if not all(delivered.values()): raise RuntimeError('stream verification failed: '+str((mode,size,delivered)))
        measurements.append(dict(chunk_samples=size,includes_two_receiver_setup=True,
            wall=percentiles(walls),mean_process_cpu_seconds=float(np.mean(cpus)),
            represented_two_receiver_seconds=2*len(corpus)/1e6,
            cpu_to_represented_airtime=float(np.mean(cpus))/(2*len(corpus)/1e6),
            all_payloads_verified=True))
    rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return dict(mode=mode,reference_stream=reference_stream,reference_stream_sha256=reference_hash,initial_engine_setup_us=init_us,software=measurements,
        process_peak_rss_bytes=int(rss if platform.system()=='Darwin' else rss*1024),
        memory_scope='isolated worker process high-water RSS including imports/native libraries; not allocation attribution',
        software_scope='two sequential receiver replays including receiver setup; warm filesystem, no UHD, not a real-time guarantee',
        input_sha256=hashlib.sha256(corpus.tobytes()).hexdigest(),sample_count=len(corpus))


def asymmetric_study(frames, seeds, strengths=((30,30),(30,10),(10,30),(10,10))):
    """Equal total TX RMS, fixed receiver noise, different path attenuation.

    Full-band OFDM alternates destinations; OFDMA serves both concurrently.
    Same payloads/attenuation per seed and all airtime charged to both users.
    """
    cfg=OFDMAConfig()
    engines={u:FrameEngine(ofdm_config(user_id=u),'native') for u in (1,2)}
    ofdma=FrameEngine(cfg,'native')
    liquid=LiquidOFDM(ofdm_config())
    rows=[]
    for snrs in strengths:
        for mode in MODES:
            correct={u:0 for u in (1,2)}
            invalid={u:0 for u in (1,2)}
            airtime=0
            for seed in seeds:
                rng=np.random.default_rng(seed)
                for sequence in range(frames):
                    pair=[Packet(0,u,sequence,rng.bytes(256)) for u in (1,2)]
                    waves=([ofdma.build(pair).samples] if mode=='native-ofdma' else
                        [liquid.modulate(p) if mode=='legacy-ofdm' else engines[p.destination].build([p]).samples for p in pair])
                    # Identical composite active RMS = .08 in all modes. The
                    # receiver noise stays fixed; gains implement each link SNR.
                    waves=[wave*(.08/math.sqrt(float(np.mean(np.abs(wave)**2)))) for wave in waves]
                    airtime+=sum(len(w)+64 for w in waves)/cfg.sample_rate
                    for user,snr in zip((1,2),snrs):
                        wave=waves[0] if mode=='native-ofdma' else waves[user-1]
                        gain=10**((snr-30)/20)
                        channel=Channel(ChannelConfig(snr_db=None,taps=(gain,),noise_power=.08**2/1000,delay_samples=29),seed*100000+sequence+user*71)
                        received=channel.propagate(wave,128)
                        if mode=='legacy-ofdm':
                            recovered=liquid.demodulate(received)
                            ok=recovered==pair[user-1].encode()
                            accepted=recovered is not None
                        else:
                            result=(ofdma if mode=='native-ofdma' else engines[user]).receive(user,received)
                            ok,accepted=result.packet==pair[user-1],result.ok
                        correct[user]+=ok
                        invalid[user]+=accepted and not ok
            count=frames*len(seeds)
            users={u:dict(reference_snr_db=snrs[u-1],delivered=correct[u],packets=count,
                    pdr=correct[u]/count,pdr_wilson95=wilson(correct[u],count),false_accepts=invalid[u],
                    payload_goodput_bps=correct[u]*256*8/airtime) for u in (1,2)}
            rates=[r['payload_goodput_bps'] for r in users.values()]
            rows.append(dict(mode=mode,link_snr_db=list(snrs),users=users,total_traffic_seconds=airtime,
                aggregate_payload_goodput_bps=sum(rates),jain_fairness=sum(rates)**2/(2*sum(r*r for r in rates)) if any(rates) else 0,
                noise_definition='fixed noise .08^2/1000; per-link gain from 30dB reference, equal total TX active RMS .08',
                limitation='uncoded AWGN static allocations, no MAC/ARQ; weak10dB is a diagnostic, not OTA calibration'))
    return rows
