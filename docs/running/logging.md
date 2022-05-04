(sec:logging)=

# Logging

The radio provides extensive logging. The low-level C++ radio will create a log in [HDF5 format](https://portal.hdfgroup.org/display/HDF5/HDF5) named `radio.h5`. If the file `radio.h5` exists, it will create `radio-N.h5` where `N` is the first number such that `radio-N.h5` does not exist; this allows the radio to be restarted if it crashes while guaranteeing it won't overwrite old logs.

Each HDF5 log has the following attributes:

 * `config`: The radio configuration, dumped from the {class}`~dragonradio.config.Config` object used to configure the radio.
 * `node_id`: The numeric radio node identifier.
 * `start`: Start time of logging, in seconds since the epoch.
 * `version`: The version of the radio.

A HDF5 log contains the following tables:

 * `arq_event`: ARQ events.
 * `event`: Logged messages, consisting of a time (offset from `start` of the log) and a string.
 * `recv`: Received packets.
 * `selftx`: Self-transmissions.
 * `send`: Sent packets.
 * `slots`: IQ data received by MAC.
 * `snapshots`: Snapshotted IQ data.
