DragonRadio Utilities
=====================

DragonRadio comes with a number of utilities for processing log files, generating plots, running a CIL collaboration server, etc. Most utilities are part of the ``dragonradio-tools`` Python module, located in the ``python/dragonradio-tools`` directory. They depend on the ``dragonradio-internal`` module, which is located in the top-level directory, that implements a subset of DragonRadio functionality, like modulation and demodulation, as a Python module.

Building the Utilities
----------------------

The utilities must be built in a separate Python virtual environment from DragonRadio. The following steps will create a virtual environment in the directory ``tools/venv`` that contains the tools. Like the radio, the tools assume Ubuntu 20.04.

#. Build the main DragonRadio binary by running the ``build.sh`` script. This will build and install the libraries requires by the tools (UHD, ``libcorrect``, and ``liquid-dsp``).

#. Change to the ``tools`` directory and run the ``install.sh`` script.

Plotting Tools
--------------

The primary tools for plotting log and scoring data are:

* ``plot-events``: Plot logged DragonRadio events on a timeline.
* ``plot-mgen-metric``: Plot a MGEN metric: rate, packet count, latency, interarrival time, loss, and late packet count. This script was built to mimic the style of plot produced by the ``trpr`` tool.
* ``plot-radio-metric``: Plot DragonRadio per-packet metrics, like EVM and transmission latency.
* ``plot-score``: Plot SC2 phase 2 and phase 3 scores. This script can also dump various score-related metrics to a CSV file and print all flows in deceasing order of number of possible points lost.
* ``plot-traffic``: Plot packet traffic as a function of time. This can provide a very detailed view of *every* packet sent and received by any node.

Use the ``-h`` or ``--help`` flag to see available options.

``drgui``
---------

The ``drgui`` tool allows logged IQ data to be visualized.
