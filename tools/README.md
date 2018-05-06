# DragonRadio Tools

This directory contains tools for parsing and visualizing DragonRadio log files.

The file `drlog.py` is a module for loading radio log files into a `Log` object.

## Building

The tools here target python 3.5. The `build-dependencies.sh` script will install the necessary Ubuntu packages using `apt` and install necessary Python packages using `pip`. Python packages are installed using a [pip user install](https://pip.pypa.io/en/stable/user_guide/#user-installs).

## Tools and examples

### `drls.py`

A simple script that loads log files and prints out details about all participating nodes.

### `drbadpackets.py`

A simple script that loads log files and prints out all packets for which the header or payload was invalid.

### `drgui.py`

This script visualizes sent and received packets. Invoking it with `--tx ID` shows a visualization of the TX log for node `ID`, and invoking it with `--rx ID` shows a visualization of the RX log for node `ID`. Both options may be specified multiple times, in which case multiple visualization windows will opened. Additional arguments to the command are treated as log files that should be read and merged into a single log.
