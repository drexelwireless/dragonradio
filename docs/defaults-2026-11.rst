Default changes, November 2026 (read this if you run the radio)
================================================================

Status 2026-11-04. Five changes in the 2026-11 work alter what the
production radio does on hardware with no change to your command line or
``radio.conf``. Three are new defaults chosen on 2026-11-04. Two are
behavior fixes that were already merged and are listed so nothing is a
surprise. Each has a way to get the old behavior back.

.. list-table::
   :header-rows: 1

   * - What
     - Old behavior
     - New behavior
     - Get the old behavior with
   * - ARQ retransmission limit
     - Unlimited: a packet is retransmitted until it is ACKed or its
       deadline passes
     - At most 8 retransmissions (9 transmissions), then the packet is
       dropped and the send window moves on
     - ``--max-retransmissions -1`` or ``arq_max_retransmissions = -1;``
   * - Tail-drop queue when full (``--taildrop``)
     - The arriving packet is always dropped
     - If the arriving packet can be sent now, the oldest queued packets
       whose destination's send window is closed are dropped instead
     - ``--no-taildrop-push-out`` or ``tail_drop_push_out = false;``
   * - HDF5 log chunk cache (``-l`` logging)
     - 64 MB cache per dataset; 131,072-row chunks
     - 8 MB cache per dataset; 16,384-row chunks
     - Edit ``kRDCCNumBytes`` (``src/Logger.cc``) and
       ``kChunkGranularity`` (``src/ExtensibleDataSet.cc``)
   * - TDMA slot synthesizer (fix, ``f0f0f3b8``)
     - A packet too large for the rest of a slot was re-modulated in a
       tight loop
     - The slot is closed and the packet waits for the next slot
     - Not configurable (it was a bug)
   * - RED gentle region (fix, ``--red``)
     - Between ``max_thresh`` and twice it, almost nothing was dropped
     - The drop probability ramps from ``max_p`` to 1, as specified
     - Not configurable (it was a bug)

One crash fix also affects every hardware run: ``TimerQueue::start`` (and
``TunTap::start``) now install the SIGUSR1 wake handler before creating
their worker thread. Before, a timer scheduled right after start-up could
signal a worker that had not yet installed it, and the signal's default
action killed the radio. It is more likely on a loaded host
(``tests/fullstack/test_timer_wake.cc``: 194 of 200 one-CPU start-ups
killed before the fix, 0 after).

Nothing else about the defaults changed: liquid OFDM is still the default
PHY, the default queue is still FIFO (unbounded, so the tail-drop change
applies only with ``--taildrop``), and the native PHY and OFDMA are opt-in
(``--phy native-ofdm``, ``--phy native-ofdma``).

Why each default changed
------------------------

**Retransmission limit 8.** With no limit, a neighbor that is heard (so its
send window stays open) but never decodes our packets takes airtime
forever: in the downlink OFDMA scenarios a UE that never decoded starved
the other UE until ``--max-retransmissions`` was set. A finite limit bounds
the cost of a dead link. Eight retransmissions lose a packet only if nine
transmissions all fail: at 20% frame loss that is about 5e-7, and at 50%
about 0.2%. Losses that survive that are left to the application (TCP
retransmits; UDP was never reliable). Unchanged:

* SYN packets are still retransmitted without limit, because a connection
  cannot start without them.
* A packet's deadline (mandate queue) still drops it earlier.
* The window still moves along by dropping the oldest packet when it is
  full and ``move_along`` is set, exactly as before.

``config/*/radio.conf`` files never set ``arq_max_retransmissions``, so the
historical configurations pick up the new limit. Set it to ``-1`` in a
configuration to reproduce historical runs exactly.

**Tail-drop push-out.** Packets to a node whose send window is closed
(unreachable, or slow to ACK) cannot leave the queue. With plain tail drop
they filled the queue and every packet to every other destination was
dropped on arrival: in the ``ofdma-one-disconnected`` scenario a healthy UE
got 21 of 1501 packets. Now, when the queue is full and the arrival can be
sent, the oldest blocked packets are pushed out to make room. With one
destination, or every window open, nothing changes. The regression test is
``tests/fullstack/test_net_queue.cc``; it also checks that
``setPushOut(false)`` restores plain tail drop.

**Logger cache.** HDF5 gives each open dataset its own raw-data chunk
cache. At 64 MB per dataset, heap use grew in 16 MB steps (one chunk of
about 120-byte rows) during long logged runs, up to 64 MB per large
dataset. It was bounded, not a leak, but on a radio host with several
logged datasets that is hundreds of MB. Logged datasets are append-only, so
only the chunk being written needs the cache. Chunks are now 16,384 rows
(about 2 MB for 120-byte rows) and the cache is 8 MB per dataset, enough
for two chunks of rows up to 256 bytes. Readers see no difference: chunking
is internal to the file. ``tests/fullstack/test_logger_chunks.cc`` writes
400,000 rows to two datasets in uneven batches, checks every row back, and
measured 16.5 MB of heap growth for the two datasets.

New options (no change unless used)
-----------------------------------

* ``--sync-time-to-pps`` (``sync_time_to_pps``): after selecting the time
  source, wait for a PPS edge and set the device time at the next edge to
  the host's whole second (``USRP.syncTimeToPPS``). For radios sharing a
  PPS with ``--time-source external``: every radio then counts the same
  seconds and TDMA slots line up from the start. Before this, an external
  time source had no effect on device time, which was always set from the
  host clock. The radio exits before transmitting if no PPS edge arrives
  within 3 s. It warns when the edge falls within 0.1 s of the host's half
  second, where two radios could pick different seconds.
* ``--no-taildrop-push-out``, and ``--max-retransmissions`` now accepts a
  negative value meaning unlimited.

Where the evidence is
---------------------

* Build and tests: every ``tests/fullstack`` program, including the new
  ``test_logger_chunks``, and the Python simulation tests pass on gridgpu01
  (2026-11-04).
* The full-stack scenario suite was rerun with the new defaults: 19/31
  passed on a heavily loaded host. Every failure but one had counted late
  or partially late bursts on the simulated device. The other,
  ``clock-offset-sync``, lost one frame before the first clock fit, as the
  previous build also did. The retransmission limit dropped no packet in
  any scenario. Details are in the plan's section 18.
* ``syncTimeToPPS`` has no hardware-free test: it only runs against a real
  device. It is first exercised by the shared-PPS OTA campaign
  (:doc:`ota-fullstack-grid`).
