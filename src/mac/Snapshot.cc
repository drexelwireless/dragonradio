#include <mutex>

#include "mac/Snapshot.hh"

SnapshotCollector::SnapshotCollector()
  : last_local_tx_start_(0.0)
{
}

void SnapshotCollector::start(void)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    snapshot_ = std::make_shared<Snapshot>();
    // Set *provisional* snapshot timestamp. Eventually, we will set this to the
    // timestamp of the first collected slot.
    snapshot_->timestamp = Clock::now();
    snapshot_collect_ = true;
    snapshot_off_ = 0;

    // Log last TX if it is in progress
    float             fs = last_local_tx_fs_rx_;
    Clock::time_point end = last_local_tx_start_ + last_local_tx_.end/fs;

    if (snapshot_->timestamp < end) {
        ssize_t actual_start = (snapshot_->timestamp - last_local_tx_start_).get_real_secs()*fs;

        last_local_tx_.start -= actual_start;
        last_local_tx_.end -= actual_start;

        snapshot_->selftx.emplace_back(last_local_tx_);
    }
}

void SnapshotCollector::stop(void)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    snapshot_collect_ = false;
}

std::shared_ptr<Snapshot> SnapshotCollector::finish(void)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    if (!snapshot_->slots.empty()) {
        float             fs = snapshot_->slots[0]->fs;
        Clock::time_point provisional_timestamp = snapshot_->timestamp;
        Clock::time_point actual_timestamp = snapshot_->slots[0]->timestamp;
        ssize_t           delta = (actual_timestamp - provisional_timestamp).get_real_secs()*fs;

        // Make snapshot timestamp the timestamp of the first collected slot
        snapshot_->timestamp = actual_timestamp;

        // Update all offsets of local self-tranmissions, i.e., transmissions
        // *this* node has made during snapshot collection. Before we fix them
        // up here, they are relative to the *provisional* snapshot timestamp.
        for (auto &selftx : snapshot_->selftx) {
            if (selftx.is_local) {
                selftx.start -= delta;
                selftx.end -= delta;
            }
        }
    }

    std::shared_ptr<Snapshot> result = std::move(snapshot_);

    return result;
}

bool SnapshotCollector::push(std::shared_ptr<IQBuf> &buf)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    if (snapshot_ && snapshot_collect_) {
        buf->in_snapshot = true;
        buf->snapshot_off = snapshot_off_;
        snapshot_->slots.emplace_back(buf);
        return true;
    } else
        return false;
}

void SnapshotCollector::finalizePush(void)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    if (snapshot_) {
        auto it = snapshot_->slots.rbegin();

        snapshot_off_ += (*it)->size();
    }
}

void SnapshotCollector::selfTX(unsigned start, unsigned end, float fc, float fs)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    if (snapshot_)
        snapshot_->selftx.emplace_back(SelfTX{false, start, end, fc, fs});
}

void SnapshotCollector::selfTX(Clock::time_point when,
                               float fs_rx,
                               float fs_tx,
                               unsigned nsamples,
                               float fc)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);
    ssize_t                         scaled_nsamples;

    scaled_nsamples = static_cast<ssize_t>(nsamples*fs_rx/fs_tx);

    if (snapshot_) {
        ssize_t start = (when - snapshot_->timestamp).get_real_secs()*fs_rx;

        snapshot_->selftx.emplace_back(SelfTX{true,
                                              start,
                                              start + scaled_nsamples,
                                              fc,
                                              fs_tx});
    } else {
        last_local_tx_start_ = when;
        last_local_tx_fs_rx_ = fs_rx;

        last_local_tx_.is_local = true;
        last_local_tx_.start = 0;
        last_local_tx_.end = scaled_nsamples;
        last_local_tx_.fc = fc;
        last_local_tx_.fs = fs_tx;
    }
}
