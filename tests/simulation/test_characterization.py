"""Theory normalization, fixed-noise fading and replay regression checks."""
import unittest
import numpy as np
from dragonradio.simulation.channel import Channel, ChannelConfig
from dragonradio.simulation.characterization import qpsk_ber, oracle_study, asymmetric_study
from dragonradio.simulation.fast import library_path
from dragonradio.simulation.frame import FrameEngine
from dragonradio.simulation.ofdma import Packet, ofdm_config


class NoiseAndTheoryTest(unittest.TestCase):
    def test_qpsk_theory_known_values(self):
        self.assertAlmostEqual(qpsk_ber(0),.078649603525,places=11)
        self.assertAlmostEqual(qpsk_ber(10),.000003872108,places=11)

    def test_attenuation_keeps_noise_fixed_and_lowers_snr(self):
        x = np.ones(50000,np.complex64)
        outputs = [Channel(ChannelConfig(snr_db=None,taps=(gain,),noise_power=.01),seed=51).propagate(x,128,trailing_samples=0)
                   for gain in (1,.1)]
        noise1,noise2 = outputs[0]-1,outputs[1]-.1
        np.testing.assert_allclose(noise1,noise2,atol=2e-7)
        snrs = [10*np.log10(g*g/np.mean(np.abs(n)**2)) for g,n in ((1,noise1),(.1,noise2))]
        self.assertAlmostEqual(float(snrs[0]-snrs[1]),20,places=4)
        with self.assertRaises(ValueError): ChannelConfig(noise_power=.1)
        with self.assertRaises(ValueError): ChannelConfig(snr_db=None,noise_power=-1)

    @unittest.skipUnless(library_path().exists(),'build native PHY')
    def test_oracle_ofdm_matches_qpsk_with_fft_normalization(self):
        rows = oracle_study(10,[183],ebnos=(0,4))
        self.assertTrue(all(r['theory_consistent_6sigma'] for r in rows))
        self.assertTrue(all(r['bits']==10*272*8 for r in rows))

    @unittest.skipUnless(library_path().exists(),'build native PHY')
    def test_asymmetric_robust_link_control_and_composite_accounting(self):
        try:
            rows=asymmetric_study(2,[731],strengths=((30,30),))
        except RuntimeError as exc:
            if 'absent' in str(exc): self.skipTest(str(exc))
            raise
        self.assertEqual(len(rows),3)
        for row in rows:
            self.assertTrue(all(v['delivered']==2 for v in row['users'].values()))
            self.assertEqual(row['jain_fairness'],1)
            self.assertGreater(row['total_traffic_seconds'],0)

    @unittest.skipUnless(library_path().exists(),'build native PHY')
    def test_large_view_replay_matches_small_chunks_and_offsets(self):
        cfg=ofdm_config()
        engine=FrameEngine(cfg,'native')
        expected=[Packet(0,1,i,bytes([i])*256) for i in range(12)]
        chunks=[np.zeros(817,np.complex64)]
        for packet in expected: chunks.extend((engine.build([packet]).samples,np.zeros(47,np.complex64)))
        waveform=np.concatenate(chunks)
        sequences=[]
        for size in (97,len(waveform)):
            rx=engine.stream_receiver(1)
            results=[]
            for start in range(0,len(waveform),size): results.extend(rx.feed(waveform[start:start+size]))
            results.extend(rx.feed([],final=True))
            good=[r for r in results if r.ok]
            self.assertEqual([r.packet for r in good],expected)
            sequences.append([r.start_sample for r in good])
            self.assertLess(len(rx.buffer),cfg.frame_samples)
        self.assertEqual(*sequences)
