#ifndef SNAPSHOT_H_
#define SNAPSHOT_H_

#include <atomic>
#include <memory>

#include "spinlock_mutex.hh"
#include "IQBuffer.hh"

/** @brief A self transmission event within a snapshot. */
struct SelfTX {
    /** @brief Is this TX local, i.e., produced by this node? */
    bool is_local;

    /* @brief Snapshot sample offset of start of packet */
    ssize_t start;

    /* @brief Snapshot sample offset of end of packet */
    ssize_t end;

    /* @brief Center frequency of packet */
    float fc;

    /* @brief Sample frequency of packet */
    float fs;
};

/** @brief A snapshot of received spectrum. */
struct Snapshot {
    /** @brief Timestamp of start of snapshot */
    MonoClock::time_point timestamp;

    /** @brief IQ buffers holding samples in snapshot */
    std::vector<std::shared_ptr<IQBuf>> slots;

    /** @brief Demodulated packets */
    std::vector<SelfTX> selftx;

    /** @brief Return an IQBuf containing IQ data from all slots */
    std::optional<std::shared_ptr<IQBuf>> getCombinedSlots(void) const;
};

/** @brief A snapshot collector. */
class SnapshotCollector {
public:
    SnapshotCollector();
    virtual ~SnapshotCollector() = default;

    /** @brief Start snapshot collection */
    void start(void);

    /** @brief Stop snapshot collection */
    void stop(void);

    /** @brief Finish snapshot collection */
    std::shared_ptr<Snapshot> finish(void);

    /** @brief Add IQ buffer to the snapshot
     * @return true if snapshots are being collected
     */
    /** The IQ buffer should not yet have been filled with received data. This
     * will initialize the snapshot_off field of the IQ buffer.
     */
    bool push(const std::shared_ptr<IQBuf> &buf);

    /** @brief Finalize a snapshotted IQ buffer */
    /** Call this after the IQ buffer has been filled. This will update the
     * snapshot offset counter.
     */
    void finalizePush(void);

    /** @brief Add a self-transmission event based on a received packet
     * @param start Sample offset of start of self-transmission
     * @param end Sample offset of end of self-transmission
     * @param fc Center frequency of self-transmission
     * @param fs Bandwidth of self-transmission
     */
    void selfTX(ssize_t start, ssize_t end, float fc, float fs);

    /** @brief Add a local self-transmission event (we transmitted something)
     * @param when Timestamp of start of self-transmission
     * @param fs_rx RX sampling rate
     * @param fs_tx TX sampling rate
     * @param fs_chan TX channel sampling rate
     * @param nsamples Number of samples of self-transmission
     * @param fc Center frequency of self-transmission
     */
    void selfTX(MonoClock::time_point when,
                float fs_rx,
                float fs_tx,
                float fs_chan,
                unsigned nsamples,
                float fc);

    /** @brief Return true if a snapshot is being collected */
    bool active(void)
    {
        return snapshot_ != nullptr;
    }

protected:
    /** @brief Mutex protecting access to the snapshot */
    spinlock_mutex mutex_;

    /** @brief The current snapshot */
    std::unique_ptr<Snapshot> snapshot_;

    /** @brief The current IQ buffer */
    std::shared_ptr<IQBuf> curbuf_;

    /** @brief Should we collect new slots? */
    bool snapshot_collect_;

    /** @brief Current offset from the beginning of the first collected slot */
    size_t snapshot_off_;

    /** @brief Timestamp of ast local TX */
    MonoClock::time_point last_local_tx_start_;

    /** @brief RX sampling frequency during last local TX  */
    float last_local_tx_fs_rx_;

    /** @brief Last local TX */
    SelfTX last_local_tx_;
};

#endif /* SNAPSHOT_H_ */
