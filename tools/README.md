# DragonRadio Tools

This directory contains tools for parsing and visualizing DragonRadio log files.

The file `drlog.py` is a module for loading radio log files into a `Log` object.

## Building

The tools here target python 3.5. The `install-dependencies.sh` script will install the necessary Ubuntu packages using `apt` and install necessary Python packages using `pip`. Python packages are installed using a [pip user install](https://pip.pypa.io/en/stable/user_guide/#user-installs).

## Tools and examples

### `drtool.py`

Summarizes log files. Can be used to print out bad packets, print out events, display the header of log files, etc.

### `drgui.py`

This script visualizes sent and received packets. Invoking it with `--tx ID` shows a visualization of the TX log for node `ID`, and invoking it with `--rx ID` shows a visualization of the RX log for node `ID`. Both options may be specified multiple times, in which case multiple visualization windows will opened. Additional arguments to the command are treated as log files that should be read and merged into a single log.

### `driperf-summary.py`

Produces a summary of a `iperf-radio.py` run when given the run's server and client logs.

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

## Running the collaboration server and stand-alone collaboration client 

The collaboration server is run by specifying a single argument, which is the server's IP address:

```
./collab_server.py --server-ip IP
```

The collaboration client can be run in stand-alone mode by specifying a node ID and the IP address of the collaboration server.

```
./collab_client.py -i NODEID --server-ip IP
```

If `gpsd` is running, the collaboration client will use it to set its GPS location. You can run a fake `gpsd` data source as follows:

```
gpsfake -P 6000 -c 0.1 logfile_20140914_365495765_can.log.chk
```

The file `logfile_20140914_365495765_can.log.chk` is in the `test/nmea2000` of the `gpsd` source distribution. You can also obtain this file directly from the `git` repository [here](http://git.savannah.nongnu.org/cgit/gpsd.git/plain/test/nmea2000/logfile_20140914_365495765_can.log.chk).
