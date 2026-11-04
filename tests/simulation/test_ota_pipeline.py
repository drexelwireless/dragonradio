"""grid/ota-pipeline.sh and grid/ota-campaign.py against a fake ssh/gridcli.
Nothing here reaches the grid or a radio: ssh is a local script that answers
gridcli and runs node commands locally, with the radio probe and the
simulation gate stubbed. Needs bash and GNU timeout (Linux)."""
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMMIT = '0123456789abcdef0123456789abcdef01234567'

FAKE_SSH = r'''#!/bin/bash
# Fake ssh: skip options, take the host, then the command.
state=__STATE__
while [ $# -gt 0 ]; do
  case $1 in
    -o|-i|-F|-p) shift 2 ;;
    -*) shift ;;
    *) break ;;
  esac
done
host=$1; shift
cmd="$*"
echo "$host: $cmd" >> "$state/ssh.log"
if [ "$host" = head.example ]; then
  case $cmd in
    "gridcli -q status") echo "{'disabled': False, 'status_message': ''}" ;;
    "gridcli -q nodes") if [ -e "$state/nodes" ]; then cat "$state/nodes"; else echo "grid91 READY"; echo "grid92 READY"; fi ;;
    getent*) echo "10.0.0.1 x" ;;
    "gridcli -q reservation schedule"*) echo "$cmd" | sed -n 's/.*--name \([^ ]*\).*/\1/p' > "$state/name" ;;
    "gridcli -q reservations") echo "777 \"$(cat "$state/name")\" (me)" ;;
    "gridcli -q reservation info"*) echo "$(cat "$state/name") (777) RUNNING" ;;
    "gridcli -q reservation stop"*) echo stopped >> "$state/stopped" ;;
    *) echo "unexpected head command: $cmd" >&2; exit 1 ;;
  esac
  exit 0
fi
case $cmd in
  *ota-node-health.sh*)
    [ -e "$state/health-fail" ] && { echo "FAIL radio does not answer ping"; exit 1; }
    echo "host=$host"; echo "  Mboard: X310"; exit 0 ;;
  *run-sim-fullstack.sh*) echo "== clean-arq"; echo "   PASSED"; exit 0 ;;
  *) bash -c "$cmd" ;;
esac
'''


@unittest.skipUnless(shutil.which('timeout') and sys.platform.startswith('linux'), 'needs GNU timeout (Linux)')
class Pipeline(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        state = self.tmp / 'state'
        state.mkdir()
        bindir = self.tmp / 'bin'
        bindir.mkdir()
        (bindir / 'ssh').write_text(FAKE_SSH.replace('__STATE__', str(state)))
        (bindir / 'ssh').chmod(0o755)
        self.state = state
        # A workspace tarball holding drfs-ws/dragonradio/COMMIT.
        src = self.tmp / 'src' / 'drfs-ws' / 'dragonradio'
        src.mkdir(parents=True)
        (src / 'COMMIT').write_text(COMMIT + '\n')
        self.tar = self.tmp / 'ws.tar.gz'
        with tarfile.open(self.tar, 'w:gz') as t:
            t.add(self.tmp / 'src' / 'drfs-ws', arcname='drfs-ws')
        campaign = json.loads((ROOT / 'grid/ota-campaign.shared-pps.example.json').read_text())
        campaign.update(rf_approval_reference='approval-test', workspace_commit=COMMIT,
                        workspace=str(self.tmp / 'node' / 'drfs-ws'), workspace_tar=str(self.tar),
                        results_root=str(self.tmp / 'results'), ssh={'head': 'head.example', 'key': '/dev/null'})
        campaign['nodes'][0]['host'] = 'grid91'
        campaign['nodes'][1]['host'] = 'grid92'
        campaign['strata'].update(strength='strong', placement='rack', power='10 dB')
        self.campaign = self.tmp / 'campaign.json'
        self.campaign.write_text(json.dumps(campaign))
        self.env = dict(os.environ, PATH=f'{bindir}:{os.environ["PATH"]}', PY=sys.executable)

    def pipeline(self, run, **env):
        return subprocess.run(['bash', str(ROOT / 'grid/ota-pipeline.sh'), str(run), str(self.campaign)],
                              env=dict(self.env, **env), capture_output=True, text=True, timeout=120)

    def test_dry_run_stops_before_launch(self):
        run = self.tmp / 'run-dry'
        p = self.pipeline(run, DRY_RUN='1')
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertTrue((run / 'dry-run.ok').exists())
        self.assertFalse((run / 'reservation.id').exists())
        self.assertNotIn('reservation schedule', (self.state / 'ssh.log').read_text())
        self.assertEqual(len(list((run / 'plans').glob('*.json'))), 6)   # 3 blocks x 2 PHYs

    def test_up_to_rf_then_stop(self):
        run = self.tmp / 'run-norf'
        p = self.pipeline(run, STOP_BEFORE='5')
        log = (run / 'pipeline.log').read_text()
        self.assertEqual(p.returncode, 0, log + p.stderr)
        for n in range(5):
            self.assertTrue((run / f'stage-{n}.done').exists(), f'stage {n}')
        self.assertFalse((run / 'stage-5.done').exists())
        self.assertIn('skipping stage 5', log)
        self.assertTrue((run / 'stage-6.done').exists())
        self.assertEqual((self.state / 'stopped').read_text().count('stopped'), 1)
        ssh = (self.state / 'ssh.log').read_text()
        self.assertIn('--no-flash --no-dragonradio-service --no-traffic', ssh)
        self.assertIn('dragonradio/ubuntu2404-20260225-20064ad9 grid91 grid92', ssh)
        self.assertNotIn('ota-fullstack.py', ssh)              # no trial ran
        self.assertEqual((self.tmp / 'node' / 'drfs-ws' / 'dragonradio' / 'COMMIT').read_text().strip(), COMMIT)

    def test_cells_rehearsal(self):
        # The N210/X310 cells campaign: one reservation for the union of the
        # cells' nodes, the external reference checked where a cell uses it,
        # the simulation gate on the first node of each family, and no RF.
        campaign = json.loads((ROOT / 'grid/ota-campaign.n210-x310.json').read_text())
        campaign.update(workspace_commit=COMMIT, workspace=str(self.tmp / 'node' / 'drfs-ws'),
                        workspace_tar=str(self.tar), results_root=str(self.tmp / 'results'),
                        ssh={'head': 'head.example', 'key': '/dev/null'})
        self.campaign.write_text(json.dumps(campaign))
        hosts = ['grid04', 'grid08', 'grid01', 'grid15', 'grid60', 'grid61', 'grid39', 'grid58', 'grid59']
        (self.state / 'nodes').write_text(''.join(f'{h} READY\n' for h in hosts))
        run = self.tmp / 'run-cells'
        p = self.pipeline(run, STOP_BEFORE='5')
        log = (run / 'pipeline.log').read_text()
        self.assertEqual(p.returncode, 0, log + p.stderr)
        self.assertEqual(len(list((run / 'plans').glob('*.json'))), 36)   # 3 blocks x 6 cells x 2 PHYs
        ssh = (self.state / 'ssh.log').read_text()
        self.assertIn('dragonradio/ubuntu2404-20260225-20064ad9 ' + ' '.join(hosts) + ' --name', ssh)
        health = {line.split(':')[0]: line for line in ssh.splitlines() if 'ota-node-health.sh' in line}
        self.assertEqual(sorted(health), sorted(hosts))
        for h in hosts:
            want = "'external'" if h in ('grid04', 'grid08', 'grid01', 'grid15') else "'internal'"
            self.assertTrue(health[h].endswith(want), health[h])
        gates = sorted({line.split(':')[0] for line in ssh.splitlines() if 'run-sim-fullstack.sh' in line})
        self.assertEqual(gates, ['grid04', 'grid60'])                     # first N210, first X310
        self.assertNotIn('ota-fullstack.py', ssh)
        self.assertTrue((run / 'stage-6.done').exists())

    def test_image_only_rehearsal(self):
        # No workspace: nothing installed, health on the image's own tools,
        # no simulation gate, and stage 5 refused.
        campaign = json.loads((ROOT / 'grid/ota-campaign.n210-x310.json').read_text())
        campaign.update(workspace_commit='image-only', workspace_tar='',
                        workspace=str(self.tmp / 'node' / 'drfs-ws'), results_root=str(self.tmp / 'results'),
                        ssh={'head': 'head.example', 'key': '/dev/null'})
        self.campaign.write_text(json.dumps(campaign))
        hosts = ['grid04', 'grid08', 'grid01', 'grid15', 'grid60', 'grid61', 'grid39', 'grid58', 'grid59']
        (self.state / 'nodes').write_text(''.join(f'{h} READY\n' for h in hosts))
        run = self.tmp / 'run-image'
        p = self.pipeline(run, STOP_BEFORE='5')
        log = (run / 'pipeline.log').read_text()
        self.assertEqual(p.returncode, 0, log + p.stderr)
        ssh = (self.state / 'ssh.log').read_text()
        self.assertIn('krd26/dragonradio-class-gr310-20261027-v2 grid04', ssh)
        self.assertNotIn('tar -xzf', ssh)
        self.assertIn('IMAGE_ONLY=1 bash', ssh)
        self.assertNotIn('run-sim-fullstack.sh', ssh)
        self.assertIn('sim gate: skipped', log)
        self.assertFalse((self.tmp / 'node' / 'drfs-ws').exists())
        run2 = self.tmp / 'run-image-rf'
        p = self.pipeline(run2)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn('trials refused: image-only campaign', (run2 / 'pipeline.log').read_text())

    def test_not_approved_campaign_refuses_rf(self):
        campaign = json.loads(self.campaign.read_text())
        campaign['rf_approval_reference'] = 'NOT-APPROVED-rehearsal-only'
        self.campaign.write_text(json.dumps(campaign))
        run = self.tmp / 'run-na'
        p = self.pipeline(run)
        log = (run / 'pipeline.log').read_text()
        self.assertNotEqual(p.returncode, 0)
        self.assertIn('stage 5 (trials) failed', log)
        self.assertNotIn('ota-fullstack.py', (self.state / 'ssh.log').read_text())
        self.assertTrue((self.state / 'stopped').exists())

    def test_health_failure_stops_reservation(self):
        (self.state / 'health-fail').write_text('')
        run = self.tmp / 'run-fail'
        p = self.pipeline(run)
        log = (run / 'pipeline.log').read_text()
        self.assertNotEqual(p.returncode, 0)
        self.assertIn('health: grid91 FAILED', log)
        self.assertIn('cleanup: stopping reservation 777', log)
        self.assertTrue((self.state / 'stopped').exists())
        self.assertFalse((run / 'stage-3.done').exists())
        self.assertFalse((run / 'stage-2.done').exists())         # a rerun relaunches and reinstalls


class Campaign(unittest.TestCase):
    def setUp(self):
        sys.path.insert(0, str(ROOT / 'grid'))
        import importlib.util
        spec = importlib.util.spec_from_file_location('ota_campaign', ROOT / 'grid/ota-campaign.py')
        self.mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.mod)
        self.c = json.loads((ROOT / 'grid/ota-campaign.shared-pps.example.json').read_text())
        self.c.update(rf_approval_reference='a', workspace_commit=COMMIT)
        for i, n in enumerate(self.c['nodes']):
            n['host'] = f'grid9{i}'
        self.c['strata'].update(strength='strong', placement='rack', power='p')

    def test_paired_blocks(self):
        plans = self.mod.plans(self.c, '/tmp/run', '777')
        self.assertEqual(len(plans), 6)
        for b in (1, 2, 3):
            self.assertEqual(sorted(p['phy'] for p in plans if p['strata']['pair_block'] == b), ['native-ofdm', 'ofdm'])
        p = plans[0]
        self.assertEqual(p['reservation_id'], '777')
        self.assertEqual(p['known_hosts'], '/tmp/run/known_hosts.777')
        self.assertTrue(any('ProxyCommand=' in o and 'dwslgrid' in o for o in p['ssh_options']))
        self.assertEqual(p['nodes'][0]['workspace'], '/tmp/drfs-ws')
        self.assertEqual(p['duration_s'], 60)

    def test_gain_cap(self):
        self.c['nodes'][0]['tx_gain'] = 25
        with self.assertRaises(self.mod.CampaignError):
            self.mod.plans(self.c, '/tmp/run', '777')

    def cells_campaign(self):
        c = json.loads((ROOT / 'grid/ota-campaign.n210-x310.json').read_text())
        c.update(workspace_commit=COMMIT)
        return c

    def test_cells(self):
        c = self.cells_campaign()
        plans = self.mod.plans(c, '/tmp/run', '777')
        self.assertEqual(len(plans), 36)
        ids = [p['trial_id'] for p in plans]
        self.assertEqual(len(set(ids)), 36)
        self.assertIn('pps-mixed01-b01-XS-ofdm', ids)
        for b in (1, 2, 3):
            runs = sorted((p['strata']['cell'], p['phy']) for p in plans if p['strata']['pair_block'] == b)
            self.assertEqual(runs, sorted((cell, phy) for cell in ('S1', 'W1', 'XS', 'XW', 'NXS', 'NXW')
                                          for phy in ('native-ofdm', 'ofdm')))
        xs = next(p for p in plans if p['strata']['cell'] == 'XS')
        self.assertEqual([n['host'] for n in xs['nodes']], ['grid60', 'grid61'])
        self.assertEqual(xs['strata']['pair_type'], 'X310<->X310')
        self.assertEqual(xs['nodes'][0]['addr'], 'addr=192.168.40.2')
        self.assertEqual(xs['strata']['carrier_map'], 'single channel')
        for p in plans:
            self.mod.ota.build(p)                    # every plan validates
        hosts = self.mod.hosts(c)
        self.assertEqual([h[0] for h in hosts],
                         ['grid04', 'grid08', 'grid01', 'grid15', 'grid60', 'grid61', 'grid39', 'grid58', 'grid59'])
        self.assertEqual(dict((h, clk) for h, _, clk, _ in hosts)['grid01'], 'external')   # external in W1
        self.assertEqual(dict((h, clk) for h, _, clk, _ in hosts)['grid61'], 'internal')

    def test_cells_reject_conflicts(self):
        c = self.cells_campaign()
        c['cells'][4]['nodes'][0]['addr'] = 'addr=192.168.10.3'           # grid01 elsewhere 192.168.10.2
        with self.assertRaises(self.mod.CampaignError):
            self.mod.plans(c, '/tmp/run', '777')
        c = self.cells_campaign()
        c['cells'][1]['name'] = 'S1'
        with self.assertRaises(self.mod.CampaignError):
            self.mod.plans(c, '/tmp/run', '777')
        c = self.cells_campaign()
        c['cells'][2]['nodes'][0]['tx_gain'] = 21
        with self.assertRaises(self.mod.CampaignError):
            self.mod.plans(c, '/tmp/run', '777')

    def test_not_approved_run_refused(self):
        c = self.cells_campaign()
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'c.json'
            path.write_text(json.dumps(c))
            self.assertEqual(self.mod.main(['run', str(path), '--run-dir', d, '--reservation', '777', '--execute']), 2)
            self.assertFalse((Path(d) / 'plans').exists())

    def test_run_requires_execute(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'c.json'
            path.write_text(json.dumps(self.c))
            self.assertEqual(self.mod.main(['run', str(path), '--run-dir', d, '--reservation', '777']), 2)


if __name__ == '__main__':
    unittest.main()
