# Dragon Radio

## Cloning the repository

This repository uses submodules, so you will need to perform a recursive checkout as follows:

```
git clone --recursive -b mainland https://github.com/mainland/dragonradio.git dragonradio
```

The submodules point to public GitHub repos with SC2-specific changes for `uhd`, `liquid-dsp`, and `liquid-usrp`. There is also a submodule for `libfec`, although it doesn't currently include any patches.

## Building the radio

If you are using Ubuntu, the `build.sh` script will install all necessary dependencies and build the radio binary, which is named `dragonradio`.

## Running the radio

The radio is initialized via Python. The `dragonradio` binary will treat its first argument as the name of a Python script to run to set up the radio, ignoring all further arguments. The Python script will receive all but the first argument passed to `dragonradio`, which is the name of the binary, `dragonradio`, as its list of arguments.

The radio must be run with root privileges in order to properly configure the network.

## `standalone-radio.py` vs. `sc2-radio.py`

There are two scripts in the `python` directory that will set up and run the radio, `standalone-radio.py` and `sc2-radio.py`.

The first, `standalone-radio.py`, is meant for command-line use and experimentation. It does not enable any higher-layer cognitive features, and it allows you to drop into an [IPython](https://ipython.org/) shell once the radio has started up where you can modify the radio in real-time.

The second, `sc2-radio.py`, is the SC2 competition radio. It is meant to be run as a background daemon, but it can also be forced to run in the foreground (with the `--foreground` flag). This version of the radio starts several background components that enable cognitive control.

Both radios can be configured either via the command line or via a configuration file (or both); they take the same command-line arguments for radio configuration and the same configuration file format and parameters. The configuration file is in [libconfig](http://www.hyperrealm.com/libconfig/libconfig_manual.html#Configuration-Files) format. They key configuration differences are as follows:

 * `standalone-radio.py` expects to be configured primarily on the command line, so it applies options from a configuration file after applying all command-line options, including command-line defaults. If you want to see the command-line defaults, pass the `-h` command-line flag to `standalone-radio.py`.
 * `sc2-radio.py` expects to be configured primarily via a configuration file, so it applies options specified on the command-line after applying options from the command-line—the opposite order from `standalone-radio.py`! This means you **must** use a complete configuration file because defaults from the command-line arguments **will not** be used.

Why the ordering difference? Because when running the competition radio, we often want to use a configuration file and tweak one or two parameters. When running `standalone-radio.py`, we usually only want to use a configuration file to specify one or two things, like the AMC table, that are a pain to configure on the command line.

### Running the radio with `standalone-radio.py`

Available CLI options can be displayed by invoking `standalone-radio.py` with the `-h` or `--help` options, e.g.,

```
./dragonradio ./python/standalone-radio.py -h
```

The options are largely identical to the options for the liquid-usrp examples: if the liquid-usrp examples and `standalone-radio.py` both allow setting the same parameter, then the corresponding option flags have the same name and argument format.

Here is an example of a standard way to run the base radio without any bells or whistles:

```
sudo ./dragonradio python/standalone-radio.py -g -4.1 -v
```

This starts a radio with the following features:

 * QPSK modulation and both inner and outer forward error correction (FEC) (in particular, `qpsk` modulation scheme, `v29` inner FEC, and `rs8` outer FEC).
 * No automatic repeat request support (ARQ).
 * No automatic modulation control (AMC).
 * A soft-TX gain of -4.1 dB applied to all modulated data.
 * Verbose messages from the low-level radio (sent to stderr).

Notable flags supported by the `standalone-radio.py` include:

 * `-i NODEID` Set the node's ID. By default, this is determined from the hostname.
 * `--phy PHY` Choose the physical layer used by the radio. Options are the flexframe-based PHY (`--phy flexframe`), ofdmflexframe-based PHY (`--phy ofdm`), and the multichannel ofdmflexframe-based PHY (`--phy multi-ofdm`). The default is `flexframe`.
 * `--rx-antenna` Set the RX antenna. Defaults to `RX2`. **You must change this to `TX/RX` if you are running on the Grid**.
 * `--interactive` Drop into an [IPython](https://ipython.org/) shell once the radio has started up. You can then manipulate the radio components in the shell.
 * `--arq` Enables selective-repeat ARQ.
 * `--amc` Enables automatic modulation control.

Here is an example of how to run the radio with lots of bells and whistles:

```
sudo ./dragonradio python/standalone-radio.py --config config/amc.conf --amc --arq --auto-soft-tx-gain 100 -v
```

 * Automatic repeat request support (ARQ).
 * Automatic modulation control (AMC).
 * A modulation table taken from `config/amc.conf`.
 * Automatic soft-TX gain calculation for each modulation configuration. This attempts to set the gain to 0 dBFS based on 100 samples of modulated packets.
 * Verbose messages from the low-level radio (sent to stderr).

### Running SC2 competition radio with `sc2-radio.py`

Here is a simple method for running the competition radio in the foreground:

```
sudo ./dragonradio python/sc2-radio.py --config config/radio.conf --colosseum-ini config/colosseum_config.ini start --bootstrap -d --foreground
```

This invocation does the following:

 * Reads radio configuration settings from `config/radio.conf`
 * Read Colosseum configuration settings from `onfig/colosseum_config.ini`
 * Start the competition radio daemon, running it in the foreground.
 * Starts the radio itself. Note that the `start` argument initializes the radio, but the ALOHA-based bootstrap process is initiated by the `--bootstrap` argument.

## Networking

After starting up, the radio will create a `tap0` device with IP address `10.10.10.NODEID` and a netmask of `255.255.255.0`, where `NODEID` is the node ID. Packets sent to this subnet will in turn be sent over the radio.

## Logging

The radio provides extensive logging, available using the `-l` flag, which takes one argument that is the directory where logs files should be placed. The low-level C++ radio will create a log in  [HDF5 format](https://portal.hdfgroup.org/display/HDF5/HDF5) named `radio.h5`. If the file `radio.h5` exists, it will create `radio-N.h5` where `N` is the first number such that `radio-N.h5` doesn't exist; this allows the radio to be restarted if it crashes while guaranteeing it won't overwrite old logs.

Radio configuration parameters are all stored as HDF5 attributes on the file itself. Additionally, three groups of data are also stored in the file: received packets, sent packets, and received time slots. When a packet is received, the IQ data for the time slot it belongs to is logged along with the IQ data for the previous time slot. This results in a substantial amount of data.
  
  
