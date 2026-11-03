// Translation unit B: a second includer, as a production PHY adapter would be.
// Reads a binary spec from stdin, writes one modulated frame to stdout.
#include <cstdio>
#include <memory>
#include <vector>
#include "simulation/FastPHYCore.hh"
using dragonradio::native::C;
using dragonradio::native::FastPHY;
FastPHY *make_engine(int, int, int, int, const int *, const int *, const int *,
                     const int *, const int *, const C *, const unsigned char *);
std::mutex *planner_lock_a();
template <class T> static std::vector<T> take(size_t count) {
    std::vector<T> v(count);
    if (count && std::fread(v.data(), sizeof(T), count, stdin) != count) throw std::runtime_error("short input");
    return v;
}
int main() {
    // The planner lock must be one object shared by both units (inline variable).
    if (planner_lock_a() != &dragonradio::native::planner_mutex) return 3;
    auto dims = take<int>(4);  // n, cp, ns, nu
    int n = dims[0], cp = dims[1], ns = dims[2], nu = dims[3];
    auto ids = take<int>(nu), nd = take<int>(nu), np = take<int>(nu);
    int td = 0, tp = 0;
    for (int u = 0; u < nu; ++u) { td += nd[u]; tp += np[u]; }
    auto db = take<int>(td), pb = take<int>(tp);
    auto tr = take<C>(n);
    auto prbs = take<unsigned char>(2 * ns * td);
    auto sizes = take<int>(nu);
    int total = 0;
    for (int s : sizes) total += s;
    auto bytes = take<unsigned char>(total);
    std::unique_ptr<FastPHY> engine(make_engine(n, cp, ns, nu, ids.data(), nd.data(), db.data(),
                                                np.data(), pb.data(), tr.data(), prbs.data()));
    std::vector<C> out(engine->frame);
    int count = engine->write_checked(bytes.data(), sizes.data(), out.data(), int(out.size()));
    std::fwrite(out.data(), sizeof(C), count, stdout);
    return 0;
}
