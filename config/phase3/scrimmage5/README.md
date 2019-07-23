We validated our submission using version 3.5.2.3 of the spectrum validation tool as follows:

```
RES=96263

./spec-val \
    --res-dir RESERVATION-${RES} \
    --scenario-len 630 \
    --noise-std-dev 1e-6 \
    --rf-threshold-base -155.0 \
    --prediction-latency 2.0 \
    --prediction-len 5.0
```

Note that we added the `--rf-threshold-base -155.0` flag in addition to those specified in [CIL-Validation-Procedure.md](https://gitlab.com/darpa-sc2-phase3/CIL/blob/master/doc/CIL-Validation-Procedure.md).

Specifically:

1. spec-val docker tag: 3.5.2.3

2. Scenario length: 630

3. List of additional/optional arg/flags and values:

  ```
  --rf-threshold-base -155.0
  --prediction-latency 2.0
  --prediction-len 5.0
  ```
