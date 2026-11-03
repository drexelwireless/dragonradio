// Translation unit A: constructs the engine directly from FastPHYCore.hh.
#include "simulation/FastPHYCore.hh"
using dragonradio::native::C;
using dragonradio::native::FastPHY;
FastPHY *make_engine(int n, int cp, int ns, int nu, const int *ids, const int *nd, const int *db,
                     const int *np, const int *pb, const C *tr, const unsigned char *prbs) {
    return new FastPHY(n, cp, ns, nu, ids, nd, db, np, pb, tr, prbs);
}
std::mutex *planner_lock_a() { return &dragonradio::native::planner_mutex; }
