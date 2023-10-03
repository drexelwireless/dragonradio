# DragonRadio Utilities

DragonRadio comes with a number of utilities for processing log files, generating plots, running a CIL collaboration server, etc. Most utilities are part of the `dragonradio-tools` Python module, located in the `python/dragonradio-tools` directory. They depend on the `dragonradiocpp` module, which is located in the top-level directory, that implements a subset of DragonRadio functionality, like modulation and demodulation, as a Python module.

## Building the Utilities

The DragonRadio tools can either be packaged as a docker container or built stand-alone.

## Docker

The following command builds a docker container with all tools and tag `dragontools:latest`:

```shell
$ docker build -t dragontools:latest -f tools/Dockerfile .
```

The DragonRadio tools are also available on Docker Hub as `mainland/dragontools:latest`.

The container can be run as follows:

```shell
docker run -it --rm \
  -u $(id -u):$(id -g) \
  -v "/etc/passwd:/etc/passwd:ro" \
  -v "/etc/group:/etc/group:ro" \
  -v /colosseum/mirror:/common_logs \
  -v ~/.cache:/cache \
  -e DISPLAY \
  -v "/tmp/.X11-unix:/tmp/.X11-unix:rw" \
  mainland/dragontools:latest \
  <COMMAND>
```

The extra docker arguments have the following purposes:

* The user running the container is "imported" to the container with the `-u` and first two volume mapping (`-v`) arguments.
* Colosseum logs are mounted in the container with the `-v /colosseum/mirror:/common_logs` argument. The container volume `/common_logs` is meant to contain logs. In this example, logs are located at `/colosseum/mirror`, but you should substitute the location of your log files. If you are not processing Colosseum logs, this argument isn't necessary.
* A data cache is mounted with the `-v ~/cache:/cache`. The data cache is used to avoid re-computing traffic and score information. You can mount whatever directory you like as the cache.
* The `-e DISPLAY` and `-v "/tmp/.X11-unix:/tmp/.X11-unix:rw"` arguments enable graphical applications in the container. This is required for plotting scores.

### Alternative build instructions

If you don't want to use docker, you can build the utilities in a Python virtual environment. The following steps will create a virtual environment in the directory `tools/venv`.

1. Build the main DragonRadio binary by running the `build.sh` script. This will build and install the libraries requires by the tools (UHD, `libcorrect`, and `liquid-dsp`).

1. Change to the `tools` directory and run the `install.sh` script. This will create a virtual environment in the `venv` directory.

## Plotting Tools

The primary tools for plotting log and scoring data are:

* `plot-events`: Plot logged DragonRadio events on a timeline.
* `plot-mgen-metric`: Plot a MGEN metric: rate, packet count, latency, interarrival time, loss, and late packet count. This script was built to mimic the style of plot produced by the `trpr` tool.
* `plot-radio-metric`: Plot DragonRadio per-packet metrics, like EVM and transmission latency.
* `plot-score`: Plot SC2 phase 2 and phase 3 scores. This script can also dump various score-related metrics to a CSV file and print all flows in deceasing order of number of possible points lost. The `--plot-score` flag will plot the Phase 3 score.
* `plot-traffic`: Plot packet traffic as a function of time. This can provide a very detailed view of *every* packet sent and received by any node.
* `sc2-batch`: Prepare a Colosseum batch file from a template.
* `sc2-diagnostics`: Check node connectivity, display SC2 scores, and report nodes that lost phase 3 points.
* `sc2-score`: Compute SC2 scores and can produce a variety of reports. See the help (`-h`) for descriptions of the various options. The most useful flag is `--problem-flows`, which shows all traffic flows that lost points ordered by number of points lost.

Use the `-h` or `--help` flag to see available options.

## `drgui`

The `drgui` tool allows logged IQ data to be visualized.
