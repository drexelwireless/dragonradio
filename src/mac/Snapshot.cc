#include <mutex>

#include "mac/Snapshot.hh"

void SnapshotCollector::start(void)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    snapshot_ = std::make_shared<Snapshot>();
    snapshot_collect_ = true;
    snapshot_timestamp_valid_ = false;
    snapshot_off_ = 0;
}

void SnapshotCollector::stop(void)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    snapshot_collect_ = false;
}

std::shared_ptr<Snapshot> SnapshotCollector::finish(void)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);
    std::shared_ptr<Snapshot>       result = std::move(snapshot_);

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

        if (!snapshot_timestamp_valid_) {
            snapshot_->timestamp = (*it)->timestamp;
            snapshot_timestamp_valid_ = true;
        }

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

    if (snapshot_ && snapshot_timestamp_valid_) {
        ssize_t start = (when - snapshot_->timestamp).get_real_secs()*fs_rx;

        snapshot_->selftx.emplace_back(SelfTX{true,
                                              start,
                                              start + static_cast<ssize_t>(nsamples*fs_rx/fs_tx),
                                              fc,
                                              fs_tx});
    }
}
