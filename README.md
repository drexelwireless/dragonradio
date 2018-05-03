# `full-radio`

## Cloning the repository

This version of full-radio utilizes submodules, so you will need to perform a recursive checkout as follows:

```
git clone --recursive -b mainland https://github.com/dwsl/full-radio.git full-radio
```

The submodules point to public GitHub repos with SC2-specific changes for uhd, liquid-dsp, and liquid-usrp. There is also a submodule for libfec, although it doesn't currently include any patches.

## Building the radio

If you are using Ubuntu, the `build-dependencies.sh` script will build and install all dependencies, including any necessary packages and all submodules in the `dependencies` directory.

After installing the dependencies, typing `make` should suffice to build the radio binary, which is `full-radio`.

## Running the radio

Available CLI options can be displayed by invoking `full-radio` with either the `-?` or `-h` options.

The options are largely identical to the options for the liquid-usrp examples: if the examples and `full-radio` both allow setting the same parameter, then the corresponding option flags are identical.

This radio also allows a choice of PHY: flexframe-based PHY (the default), ofdmflexframe-based PHY (`-o`), and the multichannel ofdmflexframe-based PHY (`-u`).

The `-i` option sets the node's ID. This is all you need to run a pair of radios. It defaults to 1.

After starting up, the radio will create a `tap` device with IP address `10.10.10.NODEID` and a netmask of `255.255.255.0`, where `NODEID` is the node ID set with `-i`. Packets sent to this subnet will be sent over the radio.

## Logging

The radio provides extensive logging, available using the `-l` flag, which takes one argument that is the name of the log file. Logs are formatted as [HDF5](https://portal.hdfgroup.org/display/HDF5/HDF5).

Radio configuration parameters are all stored as HDF5 attributes on the file itself. Additionally, three groups of data are also stored in the file: received packets, sent packets, and received time slots.

When a packet is received, the IQ data for the time slot it belongs to is logged along with the IQ data for the previous time slot. This results in a substantial amount of data.
  
  
