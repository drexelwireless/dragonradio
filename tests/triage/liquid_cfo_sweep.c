// Standalone pinned-liquidDSP CFO sweep (P0 triage). Links libliquid directly:
// no DragonRadio wrapper, no Python channel, no UHD. Noiseless CFO-only channel.
// Usage: liquid_cfo_sweep TRIALS STEP MAXCFO   (CFO in subcarrier spacings)
// Output CSV: cfo_subcarriers,experiment_mask,mirrored_mask,liquid_default_mask
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "liquid/liquid.h"
static int ok_count;
static unsigned char expect[256];
static int callback(unsigned char *h, int header_valid, int header_test, unsigned char *p,
                    unsigned int n, int payload_valid, framesyncstats_s s, void *u) {
    (void)h; (void)s; (void)u;
    if (header_test) return 1;  // demodulate every payload
    if (header_valid && payload_valid && n == 256 && !memcmp(p, expect, 256)) ok_count++;
    return 0;
}
int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: %s TRIALS STEP MAXCFO\n", argv[0]); return 2; }
    const int trials = atoi(argv[1]);
    const double step = atof(argv[2]), maxcfo = atof(argv[3]);
    enum { M = 128, CP = 16 };
    unsigned char experiment[M], mirrored[M], standard[M];
    // ofdm_config(): data -52..52 with |k|>=5, pilots at the listed bins, others null.
    const int pilots[] = {-52, -40, -28, -16, 5, 17, 29, 41};
    for (int k = 0; k < M; k++) experiment[k] = OFDMFRAME_SCTYPE_NULL;
    for (int k = -52; k <= 52; k++) if (abs(k) >= 5) experiment[(k + M) % M] = OFDMFRAME_SCTYPE_DATA;
    for (int j = 0; j < 8; j++) experiment[(pilots[j] + M) % M] = OFDMFRAME_SCTYPE_PILOT;
    for (int k = 0; k < M; k++) mirrored[(M - k) % M] = experiment[k];
    ofdmframe_init_default_sctype(M, standard);
    unsigned char *masks[3] = {experiment, mirrored, standard};
    printf("cfo_subcarriers,experiment_mask,mirrored_mask,liquid_default_mask\n");
    const int points = (int)floor(maxcfo / step + 0.5);
    for (int ci = -points; ci <= points; ci++) {
        const double cfo = ci * step, dphi = 2 * M_PI * cfo / M;
        printf("%+.3f", cfo);
        for (int m = 0; m < 3; m++) {
            ofdmflexframegenprops_s props;
            ofdmflexframegenprops_init_default(&props);
            props.check = LIQUID_CRC_32; props.fec0 = LIQUID_FEC_NONE;
            props.fec1 = LIQUID_FEC_NONE; props.mod_scheme = LIQUID_MODEM_QPSK;
            ofdmflexframegen gen = ofdmflexframegen_create(M, CP, 0, masks[m], &props);
            ofdmflexframesync sync = ofdmflexframesync_create(M, CP, 0, masks[m], callback, NULL);
            ok_count = 0;
            for (int t = 0; t < trials; t++) {
                srand(1000 + t);
                for (int i = 0; i < 256; i++) expect[i] = rand() & 255;
                unsigned char header[8] = {0};
                float complex block[M + CP], zeros[512] = {0};
                ofdmflexframegen_assemble(gen, header, expect, 256);
                ofdmflexframesync_reset(sync);
                ofdmflexframesync_execute(sync, zeros, 23);
                unsigned long sample = 23;
                for (int done = 0; !done;) {
                    done = ofdmflexframegen_write(gen, block, M + CP);
                    for (int i = 0; i < M + CP; i++, sample++) block[i] *= cexp(I * dphi * (double)sample);
                    ofdmflexframesync_execute(sync, block, M + CP);
                }
                ofdmflexframesync_execute(sync, zeros, 512);
            }
            printf(",%.4f", ok_count / (double)trials);
            ofdmflexframegen_destroy(gen); ofdmflexframesync_destroy(sync);
        }
        printf("\n");
    }
    return 0;
}
