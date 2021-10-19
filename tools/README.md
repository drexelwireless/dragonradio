# DragonRadio Tools

This directory contains tools for parsing and visualizing DragonRadio log files.

The file `drlog.py` is a module for loading radio log files into a `Log` object.

## Building

The tools here target python 3.5. The `install.sh` script will perform all steps necessary to create a Python virtualenv at `tools/venv` and install the `dragonradio` Python module there. The `dragonradio` module allows portions of the `liquid-dsp` library and other DSP functionality present in the full radio to be used in python.

A full installation requires compiling and installing the `libcorrect` and `liquid-dsp` libraries, which the `install.sh` script does. If you don't like this, if you don't want the liquid module, or if you don't want to use a virtualenv, look at the `install.sh` script and see how it operates.

## Tools and examples

### `drtool.py`

Summarizes log files. Can be used to print out bad packets, print out events, display the header of log files, etc.

### `drgui.py`

This script visualizes sent and received packets. Invoking it with `--tx ID` shows a visualization of the TX log for node `ID`, and invoking it with `--rx ID` shows a visualization of the RX log for node `ID`. Both options may be specified multiple times, in which case multiple visualization windows will opened. Additional arguments to the command are treated as log files that should be read and merged into a single log.

### `driperf-summary.py`

Produces a summary of a `iperf-radio.py` run when given the run's server and client logs.

### MGEN Wireshark dissector

The `wireshark` directory contains a Wireshark dissector for MGEN packets. The [README](./wireshark/README.md) contains installation instructions.

### EVM threshold optimization

The [EVM tool documentation](./doc/evm.md) contains instructions.

## Running the `iperf-radio.py` radio driver

The `iperf-radio.py` radio driver allows automated search over radio configuration parameters using iperf-like tests. Parameters are configured using a Python configuration file; see the examples in the `iperf-config` directory.

The radio configuration script takes the same set of command-line arguments as `standalone-radio.py`, but it is expected that most parameters will be left to the configuration file. The one exception is the PHY, which can *only* be specified on the command line since replacing the PHY at run time is complicated and would require coordination between the client and server.

The server can be invoked as follows:

```
sudo ./dragonradio python/iperf-radio.py -n 2 -i 1 --server -o server.log
```

And the client like this:

```
sudo ./dragonradio python/iperf-radio.py -n 2 -i 2 --client 10.10.10.1 -o client.log --test-config tools/iperf-config/amc.py --bw 1m
```

Note that the bandwidth used to test UDP is specified on the command-line as well. By default, each test runs for 10 seconds, but this can be changed suing the `--duration` flag.

The results of the tests can be displayed using the following command:

```
./tools/driperf-summary.py server.log client.log
```

## Testing collaboration locally

### Running the collaboration server and stand-alone collaboration client

The collaboration server is run by specifying a single argument, which is the server's IP address:

```
./collab_server.py --server-ip $IP
```

The collaboration client can be run in stand-alone mode by specifying a node ID and the IP address of the collaboration server.

```
./collab_client.py -i $CLIENTID --server-ip $IP
```

### Providing fake GPS data to the radio

If `gpsd` is running, the collaboration client will use it to set its GPS location. You can run a fake `gpsd` data source as follows:

```
gpsfake -P 6000 -c 0.1 gps/data/logfile_20140914_365495765_can.log.chk
```

The file `gps/data/logfile_20140914_365495765_can.log.chk` is a copy of that found in the `test/nmea2000` directory of the `gpsd` source distribution. You can also obtain this file directly from the `git` repository [here](http://git.savannah.nongnu.org/cgit/gpsd.git/plain/test/nmea2000/logfile_20140914_365495765_can.log.chk).

### Testing collaboration and mandates

This section gives an example of how to test collaboration and mandated outcomes with two radios. Example files are in `tools/collab`. We make the following assumptions:

  1. The shell variable `IP` contains the IP address of the host that will be use to run the collaboration server.
  1. The shell variable `CLIENTID` contains an arbitrary node ID used to run the stub collaboration client. Any ID can be used, just don't choose 1 or 2.
  1. Your working directory is the `tools` subdirectory of your `dragonradio` checkout.
  1. Radio ID 1 is the gateway, and radio ID 2 is the non-gateway.

Test collaboration and mandated outcomes as follows:

1. Start the collaboration server:

```
./collab_server.py --server-ip $IP
```

2. Start the collaboration client. This will be used to observe the collaboration messages that come from the actual radio

```
./collab_client.py -i $CLIENTID --server-ip $IP
```

3. Start the copy of the SC2 radio that will serve as the gateway. Give it the extra argument `--collab-server-ip $IP`.

4. Start another copy of the SC2 radio.

5. On both hosts running the SC2 radios, load the mandates file.

```
../python/radio-client.py update-outcomes collab/scenario1_mandated_outcomes.json
```

6. On the gateway, start a `mgen` listener:

```
mgen check port 5000-5005 output listen.drc
```

6. On the non-gateway node, run `mgen`:

```
mgen check input tools/collab/scenario1.mgen
```

Note that this `.mgen` file assume it should send data to the IP address `10.10.10.1`, which corresponds to the gateway in this setup.

7. Watch the collaboration client started in step 2. It should dump updates from the gateway node, including voxel declarations and detailed performance metrics.

## Jupyter notebooks

The `notebooks` directory contains [Jupyter](https://jupyter.org/) notebooks. You will need to install the `liquid` module to use most of them.

To start the jupyter server, execute the following command **in the virtualenv created by `install.sh`**:

```
jupyter notebook --no-browser
```

This will give you a URL to a local jupyter server that you must open with a browser of your choice.

It is possible that `jupyter` does not find the correct python interpreter. If you are sure you installed everything properly and you still can't import the `liquid` python module, try executing the following command:

```
python3 -m ipykernel install --user
```
