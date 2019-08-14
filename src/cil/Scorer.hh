#ifndef SCORING_HH_
#define SCORING_HH_

/** @brief Scoring a single measurement period */
struct Score {
    Score()
      : npackets_sent(0)
      , nbytes_sent(0)
      , update_timestamp_sent(0)
      , npackets_recv(0)
      , nbytes_recv(0)
      , update_timestamp_recv(0)
      , goal(false)
      , goal_stable(false)
      , achieved_duration(0)
      , mp_score(0)
    {
    }

    /** @brief Number of packets sent */
    size_t npackets_sent;

    /** @brief Number of bytes sent */
    size_t nbytes_sent;

    /** @brief Timestamp of last update for sent statistics */
    double update_timestamp_sent;

    /** @brief Number of packets received */
    size_t npackets_recv;

    /** @brief Number of bytes received */
    size_t nbytes_recv;

    /** @brief Timestamp of last update for receive statistics */
    double update_timestamp_recv;

    /** @brief True if goal met in MP */
    bool goal;

    /** @brief True if goal stable in MP */
    bool goal_stable;

    /** @brief Number of consecutive MP's in which goal has been met */
    unsigned achieved_duration;

    /** @brief Score for this MP */
    unsigned mp_score;
};

struct Scores : public std::vector<Score> {
    Scores() : invalid_mp(0)
    {
    }

    /** @brief First invalid MP */
    /** MP's from this MP on have been invalidated and need to be scored */
    unsigned invalid_mp;
};

using ScoreMap = std::unordered_map<FlowUID, Scores>;

class Scorer
{
public:
    Scorer();
    virtual ~Scorer();

    /** @brief Get mandates */
    MandateMap getMandates(void)
    {
        return mandates_;
    }

    /** @brief Set mandates */
    void setMandates(const MandateMap &mandates);

    /** @brief Get Scores */
    ScoreMap getScores(void)
    {
        return scores_;
    }

    void updateSentStatistics(FlowUID flow,
                              double timestamp,
                              unsigned first_mp,
                              std::vector<size_t> npackets,
                              std::vector<size_t> nbytes);

    void updateReceivedStatistics(FlowUID flow,
                                  double timestamp,
                                  unsigned first_mp,
                                  std::vector<size_t> npackets,
                                  std::vector<size_t> nbytes);

    void updateScore(unsigned final_mp);

protected:
    MandateMap mandates_;

    ScoreMap scores_;
};

#endif /* SCORING_HH_ */
