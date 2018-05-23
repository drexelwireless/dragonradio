# `full-radio`

## Cloning the repository

This version of full-radio utilizes submodules, so you will need to perform a recursive checkout as follows:

```
git clone --recursive -b mainland https://github.com/dwsl/full-radio.git full-radio
```

The submodules point to public GitHub repos with SC2-specific changes for uhd, liquid-dsp, and liquid-usrp. There is also a submodule for libfec, although it doesn't currently include any patches.

## Building the radio

If you are using Ubuntu, the `build.sh` script will install all necessary dependencies and build the radio binary, which is named `full-radio`.

## Running the radio

The radio is initialized via Python. The `full-radio` binary will treat its first argument as the name of a Python script to run to set up the radio, ignoring all further arguments. The Python script will receive all but the first argument passed to `full-radio`, which is the name of the binary, `full-radio`, as its list of arguments.

The radio must be run with root privileges in order to properly configure the network.

### Running the radio with `fullradio.py`

The `python/fullradio.py` script is a simple example of how to configure the radio from Python: it parses command-line options and configures the radio appropriately. 

Available CLI options can be displayed by invoking `fullradio.py` with the `-h` or `--help` options, e.g.,

```
./full-radio ./python/fullradio.py -h
```

The options are largely identical to the options for the liquid-usrp examples: if the examples and `fullradio.py` both allow setting the same parameter, then the corresponding option flags are identical.

Notable flags supported by the `fullradio.py` include:

 * `--i NODEID` Set the node's ID. Defaults to 1.
 * `--phy PHY` Choose the physical layer used by the radio. Options are the flexframe-based PHY (`--phy flexframe`), ofdmflexframe-based PHY (`--phy ofdm`), and the multichannel ofdmflexframe-based PHY (`--phy multi-ofdm`). The default is `flexframe`.
 * `--rx-antenna` Set the RX antenna. Defaults to `RX2`. **You must change this to `TX/RX` if you are running on the Grid**.
 * `--auto-soft-tx-gain` Automatically (attempt to) set the soft TX gain to 0 dBFS.
 * `--interactive` Drop into an [IPython](https://ipython.org/) shell once the radio has started up. You can then manipulate the radio components in the shell.

After starting up, the radio will create a `tap` device with IP address `10.10.10.NODEID` and a netmask of `255.255.255.0`, where `NODEID` is the node ID set with `-i`. Packets sent to this subnet will be sent over the radio.

## Logging

The radio provides extensive logging, available using the `-l` flag, which takes one argument that is the name of the log file. Logs are formatted as [HDF5](https://portal.hdfgroup.org/display/HDF5/HDF5).

Radio configuration parameters are all stored as HDF5 attributes on the file itself. Additionally, three groups of data are also stored in the file: received packets, sent packets, and received time slots.

When a packet is received, the IQ data for the time slot it belongs to is logged along with the IQ data for the previous time slot. This results in a substantial amount of data.
  
  
