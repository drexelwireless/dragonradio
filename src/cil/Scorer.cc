#include "cil/CIL.hh"
#include "cil/Scorer.hh"

const double kFTSuccessMandate = 0.9;

Scorer::Scorer()
{
}

Scorer::~Scorer()
{
}

void Scorer::setMandates(const MandateMap &mandates)
{
    // Set mandates
    mandates_ = mandates;

    // Reset scores
    scores_.clear();

    for (auto mandate = mandates.begin(); mandate != mandates.end(); ++mandate)
        scores_.insert({mandate->second.flow_uid, Scores()});
}

void Scorer::updateSentStatistics(FlowUID flow,
                                  double timestamp,
                                  unsigned first_mp,
                                  std::vector<size_t> npackets,
                                  std::vector<size_t> nbytes)
{
    // Find scores for given flow
    auto it = scores_.find(flow);

    if (it == scores_.end())
        return;

    Scores &scores = it->second;

    // The npackets and nbytes arrays should be the same size, but to be safe,
    // take n to be the minimum of the two sizes so we are guaranteed both have
    // at least n entries.
    unsigned n = std::min(npackets.size(), nbytes.size());

    // Resize scores if we have data for new MP's
    if (first_mp + n >= scores.size())
        scores.resize(first_mp + n);

    // Add the new data
    for (unsigned i = 0; i < n; i++) {
        unsigned mp = first_mp + i;
        Score    &score = scores[mp];

        // Don't add data with a timestamp that is before the timestamp on the
        // data we have right now.
        if (timestamp > score.update_timestamp_sent) {
            // Make sure this MP is invalidated since we have new data for it.
            if (mp < scores.invalid_mp)
                scores.invalid_mp = mp;

            score.npackets_sent = npackets[i];
            score.nbytes_sent = nbytes[i];
            score.update_timestamp_sent = timestamp;
        }
    }
}

void Scorer::updateReceivedStatistics(FlowUID flow,
                                      double timestamp,
                                      unsigned first_mp,
                                      std::vector<size_t> npackets,
                                      std::vector<size_t> nbytes)
{
    // Just like updateSentStatistics, except for received bytes and packets...
    auto it = scores_.find(flow);

    if (it == scores_.end())
        return;

    Scores   &scores = it->second;
    unsigned n = std::min(npackets.size(), nbytes.size());

    if (first_mp + n >= scores.size())
        scores.resize(first_mp + n);

    for (unsigned i = 0; i < n; i++) {
        unsigned mp = first_mp + i;
        Score    &score = scores[mp];

        if (timestamp > score.update_timestamp_recv) {
            if (mp < scores.invalid_mp)
                scores.invalid_mp = mp;

            score.npackets_recv = npackets[i];
            score.nbytes_recv = nbytes[i];
            score.update_timestamp_recv = timestamp;
        }
    }
}

void Scorer::updateScore(unsigned final_mp)
{
    for (auto it = scores_.begin(); it != scores_.end(); ++it) {
        auto mit = mandates_.find(it->first);

        if (mit == mandates_.end())
            continue;

        Scores  &scores = it->second;
        Mandate &mandate = mit->second;

        if (final_mp >= scores.size())
            scores.resize(final_mp + 1);

        for (unsigned mp = scores.invalid_mp; mp <= final_mp; ++mp) {
            Score &score = scores[mp];

            if (score.nbytes_sent == 0) {
                // If no bytes were sent, use value from previous MP
                if (mp > 0)
                    score.goal = scores[mp-1].goal;
            } else if (mandate.max_latency_s) {
                // This is a throughput mandate
                score.goal = (score.nbytes_recv*8 >= *mandate.min_throughput_bps) ||
                             (score.nbytes_recv == score.nbytes_sent);
            } else {
                // This is a file transfer mandate
                score.goal = (static_cast<double>(score.npackets_recv)/score.npackets_sent) >= kFTSuccessMandate;
            }

            if (score.goal) {
                if (mp == 0)
                    score.achieved_duration = 1;
                else
                    score.achieved_duration = scores[mp-1].achieved_duration + 1;
            } else
                score.achieved_duration = 0;

            score.goal_stable = score.achieved_duration >= mandate.hold_period;

            score.mp_score = score.goal_stable ? mandate.point_value : 0;
        }

        scores.invalid_mp = final_mp + 1;
    }
}
