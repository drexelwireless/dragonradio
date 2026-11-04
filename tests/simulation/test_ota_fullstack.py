"""Dry run of the full-stack OTA runner (scripts/ota-fullstack.py) and the
stratified summary (scripts/summarize-fullstack.py). Nothing here opens a
radio, connects to a host or runs a subprocess: Popen is patched to fail."""
import copy
import importlib.util
import io
import json
import shlex
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ota = load('ota_fullstack', 'scripts/ota-fullstack.py')
summ = load('summarize_fullstack', 'scripts/summarize-fullstack.py')


def filled_plan():
    plan = json.loads((ROOT / 'grid/fullstack-plan.example.json').read_text())
    plan.update(trial_id='t001', reservation_id='res-42', rf_approval_reference='approval-2026-11-x',
                workspace_commit='0123456789abcdef0123456789abcdef01234567')
    plan['nodes'][0]['host'] = 'radio-a'
    plan['nodes'][1]['host'] = 'radio-b'
    plan['strata'].update(strength='strong', placement='bench A', power='-30 dBm at RX')
    return plan


class Forbidden(Exception):
    pass


class DryRun(unittest.TestCase):
    def setUp(self):
        patcher = mock.patch.object(subprocess, 'Popen', side_effect=Forbidden('dry run must not execute'))
        patcher.start()
        self.addCleanup(patcher.stop)

    def run_main(self, plan, *extra):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'plan.json'
            p.write_text(json.dumps(plan))
            out, err = io.StringIO(), io.StringIO()
            with redirect_stdout(out), redirect_stderr(err):
                rc = ota.main(['--plan', str(p), *extra])
            return rc, out.getvalue(), err.getvalue()

    def test_example_placeholders_rejected(self):
        plan = json.loads((ROOT / 'grid/fullstack-plan.example.json').read_text())
        rc, _out, err = self.run_main(plan)
        self.assertEqual(rc, 2)
        self.assertIn('not filled in', err)

    def test_dry_run_prints_commands_and_executes_nothing(self):
        rc, out, _err = self.run_main(filled_plan())
        self.assertEqual(rc, 0)
        built = json.loads(out)
        self.assertTrue(built['dry_run'])
        self.assertFalse(built['hardware_opened'])
        self.assertEqual(sorted(built['nodes']), ['1', '2'])
        for nid, n in built['nodes'].items():
            ssh = n['ssh']
            self.assertEqual(ssh[0], 'ssh')
            self.assertIn('StrictHostKeyChecking=yes', ssh)
            self.assertIn('BatchMode=yes', ssh)
            self.assertEqual(ssh[-1], 'bash -s')
            s = n['script']
            self.assertIn('trap cleanup EXIT INT TERM', s)
            self.assertIn('--hardware', s)
            self.assertIn('--addr addr=192.168.', s)
            self.assertIn('timeout --signal=INT', s)
            self.assertIn('"$SHARED/GO"', s)
            self.assertIn('--ready-file', s)
            self.assertIn('--time-master 1', s)
            self.assertIn('workspace commit mismatch', s)
            self.assertIn('ip netns add', s)
            self.assertIn('ip link set eth', s)       # USRP interface moved into the namespace ...
            self.assertIn('netns 1', s)               # ... and back on exit
            self.assertNotIn('--sim-air', s)
            # Every generated radio command line parses as shell.
            radio = next(l for l in s.splitlines() if 'sim-node.py' in l)
            self.assertIsInstance(shlex.split(radio.replace('"$LOCAL', '"/x')), list)

    def test_traffic_maps_to_apps(self):
        plan = filled_plan()
        self.assertIn('--udp-send', ota.app_args(plan, 1))
        self.assertIn('10.10.10.2:5001:40:500', ota.app_args(plan, 1))
        self.assertIn('--tcp-server', ota.app_args(plan, 2))
        self.assertIn('--udp-recv', ota.app_args(plan, 2))

    def test_rejections(self):
        cases = {
            'no reverse-link approval': lambda p: p.update(rf_approval_covers=['forward data']),
            'duplicate hosts': lambda p: p['nodes'][1].update(host='radio-a'),
            'relative results path': lambda p: p.update(results_root='results'),
            'infinite duration': lambda p: p.update(duration_s=100000),
            'unverified rate': lambda p: p.update(bandwidth=10e6),
            'N210 internal time source': lambda p: p['nodes'][0].update(time_source='internal'),
            'N210 TX gain out of range': lambda p: p['nodes'][0].update(tx_gain=40),
            'short commit': lambda p: p.update(workspace_commit='abc123'),
            'no time master': lambda p: p.update(timesync={}),
            'missing stratum': lambda p: p['strata'].pop('placement'),
            'traffic to self': lambda p: p['traffic'].append({'kind': 'udp', 'src': 1, 'dst': 1, 'port': 5009}),
        }
        for name, mutate in cases.items():
            plan = filled_plan()
            mutate(plan)
            rc, _out, err = self.run_main(plan)
            self.assertEqual(rc, 2, name)
            self.assertIn('plan rejected', err, name)

    def test_execute_requires_flag(self):
        # Without --execute nothing runs (Popen would raise Forbidden).
        rc, _out, _err = self.run_main(filled_plan())
        self.assertEqual(rc, 0)
        with tempfile.TemporaryDirectory() as d:
            plan = filled_plan()
            plan['results_root'] = d
            with self.assertRaises(Forbidden):
                self.run_main(plan, '--execute')


class Summary(unittest.TestCase):
    def trial(self, phy, block, delivery, valid=True, **strata):
        base = dict(pair_type='N210<->X310', strength='strong', placement='A', power='p', clock='independent',
                    carrier_map='single', phy=phy, bandwidth=1e6, frequency=915e6, build='c', pair_block=block)
        base.update(strata)
        return {'_path': f'{phy}-{block}', 'passed': True, 'valid_for_rf_conclusions': valid,
                'continuity': {} if valid else {'1': {'tx_late': 3}},
                'flows': [{'flow': '1->2:5001', 'delivery': delivery, 'latency_p50_ms': 80.0}], 'strata': base}

    def test_strata_paired_and_excluded(self):
        trials = [self.trial('native-ofdm', b, 0.99) for b in range(3)] + [self.trial('ofdm', b, 0.97) for b in range(3)]
        trials.append(self.trial('ofdm', 9, 0.5, valid=False))
        trials.append(self.trial('native-ofdm', 5, 0.9, strength='weak'))
        out = summ.summarize(trials)
        self.assertEqual(len(out['excluded']), 1)
        self.assertIn('continuity', out['excluded'][0]['reason'])
        self.assertEqual(len(out['strata']), 3)   # strong native, strong liquid, weak native: never pooled
        paired = out['paired_native_minus_liquid']
        self.assertEqual(len(paired), 1)
        d = paired[0]['differences']['udp 1->2:5001 delivery']
        self.assertAlmostEqual(d['mean'], 0.02)
        self.assertEqual(paired[0]['pair_blocks'], [0, 1, 2])


if __name__ == '__main__':
    unittest.main()
