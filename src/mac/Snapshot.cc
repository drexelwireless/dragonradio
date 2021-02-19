// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <mutex>

#include "mac/Snapshot.hh"

std::optional<std::shared_ptr<IQBuf>> Snapshot::getCombinedSlots(void) const
{
    if (slots.empty())
        return std::nullopt;

    size_t size = 0;
    float  fc = slots[0]->fc;
    float  fs = slots[0]->fs;

    for (auto it = slots.begin(); it != slots.end() && (*it)->fc == fc && (*it)->fs == fs; ++it) {
        assert((*it)->complete);
        size += (*it)->size();
    }

    std::shared_ptr<IQBuf> buf = std::make_shared<IQBuf>(size);
    size_t                 off = 0;

    buf->timestamp = timestamp;
    buf->fc = fc;
    buf->fs = fs;

    using C = std::complex<float>;

    for (auto it = slots.begin(); it != slots.end() && (*it)->fc == fc && (*it)->fs == fs; ++it) {
        assert(off + (*it)->size() <= buf->size());
        memcpy(buf->data() + off, (*it)->data(), (*it)->size()*sizeof(C));
        off += (*it)->size();
    }

    return buf;
}

SnapshotCollector::SnapshotCollector()
  : last_local_tx_start_(0.0)
{
}

void SnapshotCollector::start(void)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    newSnapshot();
}

void SnapshotCollector::stop(void)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    snapshot_collect_ = false;
}

std::shared_ptr<Snapshot> SnapshotCollector::finalize(void)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    fixSnapshotTimestamps();

    return std::move(snapshot_);
}

std::shared_ptr<Snapshot> SnapshotCollector::next(void)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    fixSnapshotTimestamps();

    auto snapshot = std::move(snapshot_);

    newSnapshot();

    return snapshot;
}

bool SnapshotCollector::push(const std::shared_ptr<IQBuf> &buf)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    if (snapshot_ && snapshot_collect_) {
        buf->snapshot_off = snapshot_off_;
        curbuf_ = buf;
        return true;
    } else
        return false;
}

void SnapshotCollector::finalizePush(void)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    if (snapshot_) {
        snapshot_off_ += curbuf_->size();
        snapshot_->slots.emplace_back(std::move(curbuf_));
    } else
        curbuf_.reset();
}

void SnapshotCollector::selfTX(ssize_t start, ssize_t end, float fc, float fs)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    if (snapshot_)
        snapshot_->selftx.emplace_back(SelfTX{false, start, end, fc, fs});
}

void SnapshotCollector::selfTX(MonoClock::time_point when,
                               float fs_rx,
                               float fs_tx,
                               float fs_chan,
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
                                              fs_chan});
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

void SnapshotCollector::newSnapshot(void)
{
    snapshot_ = std::make_unique<Snapshot>();
    // Set *provisional* snapshot timestamp. Eventually, we will set this to the
    // timestamp of the first collected slot.
    snapshot_->timestamp = MonoClock::now();
    snapshot_collect_ = true;
    snapshot_off_ = 0;

    // Log last TX if it is in progress
    float                 fs = last_local_tx_fs_rx_;
    MonoClock::time_point end = last_local_tx_start_ + last_local_tx_.end/fs;

    if (snapshot_->timestamp < end) {
        ssize_t actual_start = (snapshot_->timestamp - last_local_tx_start_).get_real_secs()*fs;

        last_local_tx_.start -= actual_start;
        last_local_tx_.end -= actual_start;

        snapshot_->selftx.emplace_back(last_local_tx_);
    }
}

void SnapshotCollector::fixSnapshotTimestamps(void)
{
    if (!snapshot_->slots.empty()) {
        float                 fs = snapshot_->slots[0]->fs;
        MonoClock::time_point provisional_timestamp = snapshot_->timestamp;
        MonoClock::time_point actual_timestamp = *snapshot_->slots[0]->timestamp;
        ssize_t               delta = (actual_timestamp - provisional_timestamp).get_real_secs()*fs;

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
}