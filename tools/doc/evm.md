# EVM tools

## EVM simulation

The `evmsim.py` uses the DragonRadio python module to:

 1. Simulate packet error rate across different SNRs and modulation and coding schemes.
 2. Use simulation results to calculate EVM thresholds for a given packet error rate, i.e., what EVM is necessary to maintain a specified PER.
 3. Plot a CDF of probability of reception vs. EVM.

Simulation results can be generated as follows:

```
./evmsim.py --simulate --ntrials=10 -o evm.csv
```

Computing thresholds using simulation results is performed as follows:

```
./evmsim.py --threshold evm.csv
```

Finally, the CDF can be plotted as follows:

```
./evmsim.py --plot evm.csv
```

## EVM threshold computation

The `evmthresh.py` tool uses a radio log to determine what EVM value ensures a threshold fraction p of packets will be successfully received.
