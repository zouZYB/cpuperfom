// 从 Config 生成内核输入数组。
#include "branch_lab.h"

namespace fz {

Prepared prepare(const Config& cfg) {
    Prepared p;
    const size_t n = cfg.n;
    make_data(cfg.order, n, cfg.seed, p.v);
    make_bits(cfg.pattern, n, cfg.seed, p.b1);
    make_corr_bits(cfg.corr, p.b1, n, cfg.seed, p.b2);
    make_idx(n, cfg.seed, cfg.p1, cfg.p2, p.idx);
    make_trips(n, cfg.seed, cfg.a, cfg.p2, p.trips);
    p.dst.assign(n, 0);

    p.in.v = p.v.data();
    p.in.b1 = p.b1.data();
    p.in.b2 = p.b2.data();
    p.in.idx = p.idx.data();
    p.in.trips = p.trips.data();
    p.in.dst = p.dst.data();
    p.in.n = n;
    p.in.a = cfg.a;
    p.in.b = cfg.b;
    p.in.p1 = cfg.p1;
    p.in.p2 = cfg.p2;
    return p;
}

}  // namespace fz
