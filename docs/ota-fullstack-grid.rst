Full-stack OTA on the grid: first campaign with a shared PPS
=============================================================

Status 2026-11-04. This is the procedure and tooling for the first
over-the-air runs of the production radio (MAC, ARQ, queues, tap interface,
kernel UDP/TCP) with native OFDM and liquid OFDM, on grid nodes that share a
10 MHz reference and PPS. It is the step before cabled and wider OTA
matrices: a shared reference removes oscillator offset and clock drift
between the radios, so the first results are about the stack and the PHYs,
not synchronization. Nothing in this section has transmitted yet. The
tooling has been tested against a fake ``ssh``/``gridcli`` only.

What runs where
---------------

::

   laptop --ssh (once)--> gridgpu01 (grid controller, reservation 3005)
                          grid-job: grid/ota-pipeline.sh RUN CAMPAIGN
                            | gridcli via ssh dwslgrid.ece.drexel.edu
                            | one ssh per node per stage, through dwslgrid
                            v
          radio containers (dragonradio/ubuntu2404 image): prebuilt workspace in /tmp/drfs-ws,
          scripts/sim-node.py --hardware per node, results to ~/dragonradio-ota (NFS)

The pipeline runs in a grid-job on the controller, so the laptop can sleep
once it is started.

* ``grid/make-ota-workspace.sh DEV_WS OUT`` (controller, no RF): packages a
  commit (``scripts/package-fullstack.py``), sets it up offline at
  ``/tmp/drfs-ws`` from the bundle alone (which builds the radio and runs
  every ``tests/fullstack`` program), runs ``clean-arq`` in simulation with
  both PHYs, and writes ``OUT/ws-COMMIT.tar.gz``. The controller uses the
  same ``/tmp/drfs-ws``.
* ``grid/ota-campaign.py`` expands a campaign file into one
  ``scripts/ota-fullstack.py`` plan per trial: ``blocks`` blocks, each running
  every PHY once in a seeded random order at identical RF settings, so
  native and liquid are paired by block. ``plan`` writes and validates the
  plans; ``run --execute`` runs them.
* ``grid/ota-pipeline.sh RUN CAMPAIGN`` does the grid part in stages:

  .. list-table::
     :header-rows: 1

     * - Stage
       - RF
       - What
     * - 0 preflight
       - no
       - campaign and every trial plan validate; Grid enabled; nodes READY;
         login-server DNS works; the tarball's commit matches the campaign.
         ``DRY_RUN=1`` stops here.
     * - 1 launch
       - no
       - one reservation of the campaign's nodes with its image,
         ``--no-flash --no-dragonradio-service --no-traffic``, finite
         duration (default 4 h); waits for RUNNING; records the ID
     * - 2 install
       - no
       - extracts the workspace tarball into node-local ``/tmp/drfs-ws``,
         checks its commit; container host keys are pinned per reservation
         here
     * - 3 health
       - no
       - ``grid/ota-node-health.sh``: NTP synchronized, root or passwordless
         sudo, ``/dev/net/tun``, radio answers ping, read-only
         ``uhd_usrp_probe`` with the workspace's UHD 4.6 (no FPGA/firmware
         compatibility complaint), 2 GB free, no other radio running
     * - 4 sim-gate
       - no
       - ``clean-arq`` in simulation on the first radio node, native and
         liquid production, the exact build on the node's CPU (one retry)
     * - 5 trials
       - **yes**
       - ``ota-campaign.py run --execute``
     * - 6 stop
       - no
       - stops the reservation

  Each stage leaves ``RUN/stage-N.done``, and rerunning the same command
  resumes. On any failure the exit trap stops the reservation this run
  created and forgets stages 1–4, so a rerun relaunches. ``STOP_BEFORE=5``
  runs everything up to the first transmission and then stops the
  reservation: a full rehearsal without RF.

Each trial (``scripts/ota-fullstack.py --execute``) starts one node script
per node over ssh. The node script:

* checks the workspace commit, that it runs as root or has passwordless
  sudo, that no other radio, ``tap0`` or 10.10.10.0/24 address exists, and
  disk space;
* starts the production radio on its USRP under ``timeout``
  (``--hardware --addr``, ``--arq``, timesync HELLOs) with
  ``--clock-source external --time-source external --sync-time-to-pps``;
* writes a readiness file and waits for the controller's GO, which carries
  a wall-clock start time 20 s ahead (NTP), so NFS delays cannot skew the
  start;
* runs ``sim-netapp.py`` (kernel UDP and TCP over the radio's tap) for the
  finite duration;
* counts continuity errors (late TX, underflow, RX errors, missed slot
  deadlines) and copies its results to the shared results directory from an
  exit trap.

The controller then scores the trial with the simulation's
``evaluate_tap``, marks trials with continuity errors invalid for RF
conclusions, and after the last trial runs ``scripts/summarize-fullstack.py``
(stratified, paired native − liquid by block).

Privilege and networking on the grid
------------------------------------

Every grid node is its own LXC container, in which users have full admin
rights. No sudo setup or approval is needed:

* Privileged commands (the radio's tap interface) run directly when the
  node script runs as root, and through ``sudo -n`` when it runs as an admin
  user (``"privilege": "auto"``, the default).
* The tap interface and applications use the container's own network stack
  (``"netns": false``, the default). Each node is a separate container, so
  nothing collides, and the USRP's Ethernet interface is never touched. The
  radio adds no OS routes; it only filters packets internally. A per-trial
  namespace (``"netns": true``) remains available for hosts that run
  several radios.

Shared PPS and 10 MHz
---------------------

With ``clock_source`` and ``time_source`` both ``external``, the radio's
sample clocks share the 10 MHz reference, so there is no carrier or
sample-clock offset between radios beyond each daughterboard's LO behavior.
``--sync-time-to-pps`` (new, :doc:`defaults-2026-11`) waits for a PPS edge
and sets device time at the next edge to the host's whole second. Every
radio on the same PPS whose host clock is NTP-synchronized (checked in
stage 3) then has the same device time, and TDMA slots line up from the
first slot. Without it, external time had no effect: device time was set
from each host clock. The timesync protocol still runs. With no skew to fit
it only refines a residual offset. If a node gets no PPS, its radio exits
before transmitting.

The radio warns if the PPS edge falls near the host's half second. There,
two radios can pick different seconds, which would show as a one-second
offset that the timesync protocol then removes.

What the campaign needs before anything transmits
-------------------------------------------------

Copy ``grid/ota-campaign.shared-pps.example.json`` and fill in:

* **Nodes.** Names of two grid nodes whose radios share a 10 MHz and PPS
  distribution, their families and addresses. Grid convention: X310 at
  ``addr=192.168.40.2``, N210 at ``addr=192.168.10.2``. N210s need
  ``time_source`` ``external`` here too, never ``internal``.
* **RF approval.** This experiment's approval reference, covering forward
  data, reverse data with ACKs, and timesync HELLO broadcasts: TDMA with ARQ
  transmits from every node. A previous experiment's frequency approval
  (OpenISAC, 1.3 GHz, 2026-10-23) does not carry over. The example's
  1.3 GHz is a placeholder.
* **Frequency, TX gain (cap 20 dB, lower if approved lower), RX gain.**
* **Strata.** Strength, placement and power, as they will be reported.
* **Workspace.** ``workspace_tar`` and ``workspace_commit`` from
  ``make-ota-workspace.sh``.

Then on gridgpu01::

    cd /tmp/drfs-ws/dragonradio
    PY=/tmp/drfs-ws/venv/bin/python
    $PY grid/ota-campaign.py plan CAMPAIGN.json --run-dir /tmp/plan-check     # validate, print
    DRY_RUN=1 bash grid/ota-pipeline.sh ~/dragonradio-ota/run-dry CAMPAIGN.json
    STOP_BEFORE=5 bash grid/ota-pipeline.sh ~/dragonradio-ota/rehearsal CAMPAIGN.json   # no RF
    grid-job start --cwd /tmp/drfs-ws/dragonradio --name dr-ota -- \
        bash grid/ota-pipeline.sh ~/dragonradio-ota/run-YYYYMMDD CAMPAIGN.json        # RF

The rehearsal reserves the nodes, installs, health-checks and runs the
simulation gate on the real containers, then stops the reservation. It is
the right first step on an unfamiliar node pair.

Default first campaign
----------------------

Two nodes, 1 MS/s, M = 48, ``rs8`` QPSK on both PHYs, 3 blocks × 2 PHYs of
60 s each: bidirectional UDP at 40 × 500 B/s each way and 100 TCP
request/response exchanges. Each trial is about 3–4 minutes, including
radio start-up, the readiness barrier and the start lead. The campaign is
about 25 minutes of trials, of which 6 minutes carry traffic. The radios
also transmit HELLOs and ACKs during warm-up, about 1 minute per trial.
The whole pipeline, with reservation launch, installation and the gate, is
roughly an hour.

Known limits
------------

* Never run against hardware. The first real reservation will show what the
  fake ``ssh`` could not:

  * container details (whether the user is root, ``/dev/net/tun``, NTP);
  * whether UHD 4.6 accepts the X310 FPGA images loaded for UHD 4.2 on
    2026-10-23 (the probe in stage 3 checks; nothing is flashed);
  * whether the node CPUs keep up at 1 MS/s.

  ``STOP_BEFORE=5`` answers all three without transmitting.
* No real-time priority unless the radio runs as root with
  ``CAP_SYS_NICE``. On the grid it does run as root or through sudo, so the
  P4 late-burst question gets its first hardware answer here.
* Native OFDMA needs three nodes and is not part of this campaign.
