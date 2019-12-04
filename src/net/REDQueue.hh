#ifndef REDQUEUE_HH_
#define REDQUEUE_HH_

#include <list>
#include <random>

#include "logging.hh"
#include "Clock.hh"
#include "net/Queue.hh"
#include "net/SizedQueue.hh"

/** @brief An Adaptive RED queue. */
/** See the paper Random Early Detection Gateways for Congestion Avoidance */
template <class T>
class REDQueue : public SizedQueue<T> {
public:
    using const_iterator = typename std::list<T>::const_iterator;

    using Queue<T>::canPop;
    using SizedQueue<T>::stop;
    using SizedQueue<T>::drop;
    using SizedQueue<T>::done_;
    using SizedQueue<T>::size_;
    using SizedQueue<T>::hi_priority_flows_;
    using SizedQueue<T>::m_;
    using SizedQueue<T>::cond_;
    using SizedQueue<T>::hiq_;
    using SizedQueue<T>::q_;

    REDQueue(bool gentle,
             size_t min_thresh,
             size_t max_thresh,
             double max_p,
             double w_q)
      : SizedQueue<T>()
      , gentle_(gentle)
      , min_thresh_(min_thresh)
      , max_thresh_(max_thresh)
      , max_p_(max_p)
      , w_q_(w_q)
      , count_(-1)
      , avg_(0)
      , gen_(std::random_device()())
      , dist_(0, 1.0)
    {
    }

    REDQueue() = delete;

    virtual ~REDQueue()
    {
        stop();
    }

    /** @brief Get flag indicating whether or not to be gentle */
    /** See:
     * https://www.icir.org/floyd/notes/test-suite-red.txt
     */
    bool getGentle(void) const
    {
        return gentle_;
    }

    /** @brief Set flag indicating whether or not to be gentle */
    void setGentle(bool gentle)
    {
        gentle_ = gentle;
    }

    /** @brief Get minimum threshold */
    size_t getMinThresh(void) const
    {
        return min_thresh_;
    }

    /** @brief Set minimum threshold */
    void setMinThresh(size_t min_thresh)
    {
        min_thresh_ = min_thresh;
    }

    /** @brief Get maximum threshold */
    size_t getMaxThresh(void) const
    {
        return max_thresh_;
    }

    /** @brief Set maximum threshold */
    void setMaxThresh(size_t max_thresh)
    {
        max_thresh_ = max_thresh;
    }

    /** @brief Get maximum drop probability */
    double getMaxP(void) const
    {
        return max_p_;
    }

    /** @brief Set maximum drop probability */
    void setMaxP(double max_p)
    {
        max_p_ = max_p;
    }

    /** @brief Get queue qeight */
    double getQueueWeight(void) const
    {
        return max_p_;
    }

    /** @brief Set queue qeight */
    void setQueueWeight(double w_q)
    {
        w_q_ = w_q;
    }

    virtual void reset(void) override
    {
        std::lock_guard<std::mutex> lock(m_);

        done_ = false;
        size_ = 0;
        count_ = 0;
        hiq_.clear();
        q_.clear();
    }

    virtual void push(T&& item) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            if (item->flow_uid && hi_priority_flows_.find(*item->flow_uid) != hi_priority_flows_.end()) {
                hiq_.emplace_back(std::move(item));
                return;
            }

            bool mark = false;

            // Calculate new average queue size
            if (size_ == 0)
                avg_ = 0;
            else
                avg_ = (1 - w_q_)*avg_ + w_q_*size_;

            // Determine whether or not to mark packet
            if (avg_ < min_thresh_) {
                count_ = -1;
            } else if (min_thresh_ <= avg_ && avg_ < max_thresh_) {
                count_++;

                double p_b = max_p_*(avg_ - min_thresh_)/(max_thresh_ - min_thresh_);
                double p_a = p_b/(1.0 - count_*p_b);

                if (dist_(gen_) < p_a) {
                    mark = true;
                    count_ = 0;
                }
            } else if (gentle_ && avg_ < 2*max_thresh_) {
                count_++;

                double p_a = max_p_*(avg_ - max_thresh_)/max_thresh_;

                if (dist_(gen_) < p_a) {
                    mark = true;
                    count_ = 0;
                }
            } else {
                mark = true;
                count_ = 0;
            }

            if (mark)
                drop(*item);

            if (!mark) {
                size_ += item->payload_size;

                q_.emplace_back(std::move(item));
            }
        }

        cond_.notify_one();
    }

protected:
    /** @brief Gentle flag. */
    bool gentle_;

    /** @brief Minimum threshold. */
    size_t min_thresh_;

    /** @brief Maximum threshold. */
    size_t max_thresh_;

    /** @brief Maximum drop probability. */
    double max_p_;

    /** @brief Queue weight. */
    double w_q_;

    /** @brief Packets since last marked packet. */
    int count_;

    /** @brief Average size of queue (bytes). */
    double avg_;

    /** @brief Random number generator */
    std::mt19937 gen_;

    /** @brief Uniform 0-1 real distribution */
    std::uniform_real_distribution<double> dist_;
};

using REDNetQueue = REDQueue<std::shared_ptr<NetPacket>>;

#endif /* REDQUEUE_HH_ */
