Default changes, November 2026 (read this if you run the radio)
================================================================

Status 2026-11-05. This is the ``codex/ofdma-simulation`` page of the same
name, ported to ``codex/native-radiowars2024`` (DragonRadio
``wip/radiowars2024`` as in the GNU Radio 3.10 class image). Seven changes
on this branch alter what the production radio does on hardware with no
change to your command line or ``radio.conf``:

* two are new defaults, chosen on 2026-11-04 on the old branch;
* five are behavior fixes.

Each default has a way to get the old behavior back. Nothing listed here
has been run on hardware from this branch yet (simulation only, see
:doc:`radiowars2024-port`).

.. list-table::
   :header-rows: 1

   * - What
     - Old behavior (``5cf3e7d``, the image's DragonRadio)
     - New behavior
     - Get the old behavior with
   * - ARQ retransmission limit (``5308ec26``)
     - Unlimited: a packet is retransmitted until it is ACKed or its
       deadline passes
     - At most 8 retransmissions (9 transmissions), then the packet is
       dropped and the send window moves on
     - ``--max-retransmissions -1`` or ``arq_max_retransmissions = -1``
   * - Tail-drop queue when full, ``--taildrop`` (``dfaed327``)
     - The arriving packet is always dropped
     - If the arriving packet can be sent now, the oldest queued packets
       whose destination's send window is closed are dropped instead
     - ``--no-taildrop-push-out`` or ``tail_drop_push_out = False``
   * - HDF5 log chunk cache, ``-l`` logging (``ccdbfb9f``)
     - 64 MB cache per dataset; 131,072-row chunks
     - 8 MB cache per dataset; 16,384-row chunks
     - Edit ``kRDCCNumBytes`` (``src/Logger.cc``) and
       ``kChunkGranularity`` (``src/ExtensibleDataSet.cc``)
   * - TDMA channel synthesizer (fix, ``543707c6``)
     - A packet too large for the rest of a slot was re-modulated in a
       tight loop until the slot was popped (10,020 times in a 200 ms slot
       in ``test_synth_slot``)
     - The packet waits for the next slot
     - Not configurable (it was a bug)
   * - RED gentle region, ``--red`` (fix, ``ccdbfb9f``)
     - Between ``max_thresh`` and twice it, almost nothing was dropped
     - The drop probability ramps from ``max_p`` to 1, as specified
     - Not configurable (it was a bug)
   * - ARQ SYN retransmission (fix, ``61094286``)
     - A retransmitted SYN reset the receive window and redelivered data
     - A SYN matching the one that opened the window is ACKed again and
       dropped as a duplicate
     - Not configurable (it was a bug)
   * - Closed send window (fix, ``43677d24``)
     - ACK/NAK/control packets queued behind a closed window were held,
       which deadlocked a simultaneous open until the SYN timer fired
     - A closed window holds back only sequenced data
     - Not configurable (it was a bug)

Two crash and undefined-behavior fixes also affect every hardware run:

* **SIGUSR1 at start-up** (``456a46d3``). ``TimerQueue::start`` and
  ``TunTap::start`` now install the SIGUSR1 wake handler before creating
  their worker thread. Before, a timer scheduled right after start-up could
  signal a worker that had not installed it yet, and the signal's default
  action killed the radio. ``tests/fullstack/test_timer_wake.cc``: 194 of
  200 one-CPU start-ups were killed before the fix, 0 after.
* **Misaligned ``Seq``** (``f9fa0d5d``). ``Seq`` now has alignment 1, so
  packed control-message fields are valid objects; UBSan had reported a
  misaligned call. The wire layout is unchanged.

Nothing else about the defaults changed:

* Liquid OFDM is still the default PHY.
* The default queue is still FIFO (unbounded), so the tail-drop change
  applies only with ``--taildrop``.
* The native PHY and downlink OFDMA are opt-in (``--phy native-ofdm``,
  ``--phy native-ofdma``).
* ``--ofdma-burst-wait`` matters only with ``native-ofdma``: no other PHY
  waits for a burst partner, and nothing else kicks the network queue
  (``642470c4``).

Why each default changed
------------------------

**Retransmission limit 8.** With no limit, a neighbor that is heard (so its
send window stays open) but never decodes our packets takes airtime
forever. In the downlink OFDMA scenarios on the old branch, a UE that never
decoded starved the other UE until ``--max-retransmissions`` was set. A
finite limit bounds the cost of a dead link.

Eight retransmissions lose a packet only if nine transmissions all fail:
about 5e-7 at 20% frame loss, about 0.2% at 50%. Losses that survive that
are left to the application (TCP retransmits; UDP was never reliable).

Unchanged:

* SYN packets are still retransmitted without limit, because a connection
  cannot start without them.
* A packet's deadline (mandate queue) still drops it earlier.
* The window still moves along by dropping the oldest packet when it is
  full and ``move_along`` is set.

To reproduce historical runs exactly, set ``arq_max_retransmissions`` to
``-1`` in the configuration.

**Tail-drop push-out.** Packets to a node whose send window is closed
(unreachable, or slow to ACK) cannot leave the queue. With plain tail drop
they filled the queue, and every packet to every other destination was
dropped on arrival: in the old branch's ``ofdma-one-disconnected``
scenario a healthy UE got 21 of 1501 packets. Now, when the queue is full
and the arrival can be sent, the oldest blocked packets are pushed out to
make room. With one destination, or every window open, nothing changes.
``tests/fullstack/test_net_queue.cc`` is the regression test.

**Logger cache.** HDF5 gives each open dataset its own raw-data chunk
cache. At 64 MB per dataset, heap use grew in 16 MB steps during long
logged runs. It was bounded, but on a radio host with several logged
datasets that is hundreds of MB. Logged datasets are append-only, so only
the chunk being written needs the cache. Readers see no difference.
``tests/fullstack/test_logger_chunks.cc`` is the regression test.

New options (no change unless used)
-----------------------------------

* ``--use-pps`` (``clock_use_pps``, this branch's existing option, now
  ``USRP::syncTime(use_pps)``, ``cab14e2e``). It plays the role of the old
  branch's ``--sync-time-to-pps``. It waits for a PPS edge and, with an
  external clock source, checks that the 10 MHz reference is locked. It
  then sets the device time at the next edge to the host's whole second and
  verifies that the time took. Radios sharing a PPS then count the same
  seconds. The radio exits if no edge arrives within 3 s or the reference
  is not locked. It has no hardware-free test.
* ``--no-taildrop-push-out``, and ``--max-retransmissions`` now accepts a
  negative value meaning unlimited.
* ``--sim-air`` and the other ``--sim-*`` options select the simulated
  device. They never open a USRP (:doc:`radiowars2024-port`).
