"""Campaign correctness and regression checks; never opens a radio."""
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
from dragonradio.simulation.ota import prepare, load_wave, decode, evaluate, wilson
from dragonradio.simulation.channel import Channel, ChannelConfig
from dragonradio.simulation.fast import library_path
from dragonradio.simulation.liquid import LiquidOFDM
from dragonradio.simulation.ofdma import ofdm_config, Packet
import importlib.util
import subprocess
import sys
from types import SimpleNamespace
from dragonradio.simulation.transport import SDRConfig, UHDTransport


@unittest.skipUnless(library_path().exists(), 'build native PHY first')
class CampaignTest(unittest.TestCase):
    def test_two_users_tdma_and_simultaneous_with_noisy_idle_prefix(self):
        with tempfile.TemporaryDirectory() as parent:
            directory = Path(parent)/'campaign'
            manifest = prepare(directory, frames=6, modes=('native-ofdm', 'native-ofdma'))
            for mode in manifest['waveforms']:
                _, wave = load_wave(directory, mode)
                reports = {}
                for user in (1, 2):
                    samples = Channel(ChannelConfig(30, (1, .15j), 210, .1), user).propagate(wave, 128)
                    reports[user] = decode(samples, mode, user, manifest['sample_rate'])
                result = evaluate(manifest, mode, reports)
                self.assertTrue(result['all_payloads_verified'], result)
                self.assertEqual(result['jain_fairness'], 1)
                records = manifest['waveforms'][mode]['records']
                self.assertEqual(records[0]['start'] == records[1]['start'], mode == 'native-ofdma')
                self.assertLessEqual(np.max(np.abs(wave)), .5)
                active = np.concatenate([wave[r['start']:r['start']+r['count']] for r in records])
                self.assertAlmostEqual(float(np.sqrt(np.mean(np.abs(active)**2))), .08, places=6)
            path = directory/manifest['waveforms']['native-ofdma']['file']
            with path.open('ab') as stream:
                stream.write(b'bad')
            with self.assertRaisesRegex(ValueError, 'hash/size'):
                load_wave(directory, 'native-ofdma')

    def test_swapped_subbands_and_wrong_map_rejection(self):
        with tempfile.TemporaryDirectory() as parent:
            directory = Path(parent)/'swapped'
            m = prepare(directory, frames=4, modes=('native-ofdma',), allocation_swap=True)
            _, wave = load_wave(directory, 'native-ofdma')
            for user in (1, 2):
                correct = decode(wave, 'native-ofdma', user, 1e6, True)
                wrong = decode(wave, 'native-ofdma', user, 1e6, False)
                self.assertEqual(len(correct['packets']), 4)
                self.assertEqual(len(wrong['packets']), 0)

    def test_grid_plan_and_hardware_cli_default_to_no_hardware(self):
        root = Path(__file__).resolve().parents[2]
        result = subprocess.run([sys.executable, str(root/'grid/run-trial.py'), '--plan',
            str(root/'grid/trial-plan.example.json'), '--mode', 'native-ofdma'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(json.loads(result.stdout)['hardware_opened'])
        with tempfile.TemporaryDirectory() as parent:
            directory = Path(parent)/'run'
            prepare(directory, frames=1, modes=('native-ofdma',))
            cfg = Path(parent)/'sdr.json'
            cfg.write_text(json.dumps(dict(device_args='addr=test', center_frequency=1e9)))
            result = subprocess.run([sys.executable, str(root/'scripts/ota-phy.py'), 'tx',
                '--campaign', str(directory), '--sdr-config', str(cfg)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(json.loads(result.stdout)['hardware_opened'])

    def test_rx_readiness_and_stop_on_discontinuity(self):
        events = []
        class Stream:
            count = 0
            def get_max_num_samps(self): return 20
            def issue_stream_cmd(self, cmd): events.append(cmd.mode)
            def recv(self, buf, md, timeout):
                self.count += 1
                md.error_code = 'none' if self.count == 1 else 'overflow'
                return 20
        class Device:
            def __init__(self, args): pass
            def __getattr__(self, name):
                if name.startswith('set_'): return lambda *args: None
                raise AttributeError(name)
            def get_rx_rate(self, ch): return 1e6
            def get_rx_stream(self, args): return Stream()
        module = SimpleNamespace(usrp=SimpleNamespace(MultiUSRP=Device, StreamArgs=lambda *a: SimpleNamespace()),
            types=SimpleNamespace(RXMetadata=SimpleNamespace, StreamCMD=lambda mode: SimpleNamespace(mode=mode),
                StreamMode=SimpleNamespace(start_cont='start', stop_cont='stop'),
                RXMetadataErrorCode=SimpleNamespace(none='none', timeout='timeout')))
        decoder = SimpleNamespace(reset=lambda: events.append('reset'), feed=lambda block, **kw: [])
        transport = UHDTransport(SDRConfig('addr=test', 1e9), 'rx', module)
        with self.assertRaisesRegex(RuntimeError, 'overflow'):
            transport.receive(decoder, .1, on_ready=lambda md: events.append('ready'))
        self.assertEqual(events, ['start', 'ready', 'reset', 'stop'])

    def test_paired_statistics_and_duplicate_trial_rejection(self):
        root = Path(__file__).resolve().parents[2]
        spec = importlib.util.spec_from_file_location('summary', root/'scripts/summarize-ota.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as parent:
            directories = []
            for i in range(3):
                directory = Path(parent)/str(i)
                m = prepare(directory, frames=1, modes=('native-ofdma',))
                for mode, rate, pdr in (('legacy-ofdm',100, .95),('native-ofdm',120, .96),('native-ofdma',140, .96)):
                    result = dict(run_id=m['run_id'], mode=mode, aggregate_goodput_bps=rate,
                        jain_fairness=1, users={str(u):dict(pdr=pdr) for u in (1,2)},
                        tx=dict(hardware_opened=True, complete=True, sdr=dict(gain_db=0)),
                        rx={str(u):dict(sdr=dict(gain_db=0)) for u in (1,2)})
                    (directory/f'{mode}-result.json').write_text(json.dumps(result))
                directories.append(directory)
            result = module.summarize(directories)
            self.assertTrue(result['native_ofdm_meets_pdr_margin'])
            self.assertTrue(result['ofdma_goodput_gain_supported'])
            with self.assertRaisesRegex(ValueError, 'duplicate'):
                module.summarize([directories[0]]*3)

    def test_duplicate_wrong_payload_and_confidence_interval(self):
        with tempfile.TemporaryDirectory() as parent:
            manifest = prepare(Path(parent)/'run', frames=2, modes=('native-ofdma',))
            p = manifest['expected'][0]
            valid = dict(source=0, **p)
            invalid = dict(valid, sha256='wrong')
            reports = {1: dict(packets=[valid, valid, invalid], decode_cpu_seconds=1),
                       2: dict(packets=[], decode_cpu_seconds=1)}
            result = evaluate(manifest, 'native-ofdma', reports)
            self.assertEqual(result['users'][1]['delivered'], 1)
            self.assertEqual(result['users'][1]['duplicates'], 1)
            self.assertEqual(result['users'][1]['invalid'], 1)
            self.assertFalse(result['all_payloads_verified'])
            self.assertLess(wilson(2, 2)[0], 1)
            self.assertGreater(wilson(0, 2)[1], 0)

    def test_real_legacy_stream_returns_every_packet_across_chunks(self):
        try:
            modem = LiquidOFDM(ofdm_config())
        except RuntimeError as exc:
            if 'absent' in str(exc):
                self.skipTest(str(exc))
            raise
        expected = [Packet(0, u, i, bytes([i])*256) for i in range(4) for u in (1, 2)]
        wave = np.concatenate([np.concatenate((modem.modulate(p), np.zeros(64))) for p in expected])
        for chunk in (97, len(wave)):
            received = []
            modem.demodulate([], reset=True)
            for offset in range(0, len(wave), chunk):
                received.extend(modem.receive_many(wave[offset:offset+chunk]))
            self.assertEqual(received, expected)


if __name__ == '__main__':
    unittest.main()
