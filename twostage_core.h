#pragma once
// ============================================================
// twostage_core.h  –  Core two-stage design computations
//
// KEY OPTIMIZATION: precompute pbinom CDF lookup tables per
// (n, p) pair so inner loops use O(1) array lookups instead
// of the expensive incomplete-beta continued fraction.
// ============================================================

#include "stat_math.h"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>
#include <cstdio>
#include <chrono>

// ============================================================
// Basic two-stage operating-characteristic functions
// (used outside the hot loop for opchar computation)
// ============================================================

inline double pr_reject_two_stage(int n1, int r1, int n, int r_orig, double p) {
    int r = r_orig + 1;
    double prob = 0.0;
    for (int k = 0; k <= n1; ++k) {
        double pmf1 = stat::dbinom(k, n1, p);
        if (k <= r1) continue;
        int needed2 = std::max(r - k, 0);
        double tail2 = (needed2 <= 0) ? 1.0
            : stat::pbinom(needed2 - 1, n - n1, p, false);
        prob += pmf1 * tail2;
    }
    return std::min(std::max(prob, 0.0), 1.0);
}

inline double pet_two_stage(int n1, int r1, double p) {
    return stat::pbinom(r1, n1, p);
}

inline double en_two_stage(int n1, int r1, int n, double p) {
    return (double)n1 + (1.0 - stat::pbinom(r1, n1, p)) * (double)(n - n1);
}

// ============================================================
// Precomputed binomial CDF+PMF table for fixed (n, p)
// ============================================================
struct BinomLUT {
    std::vector<double> cdf;   // cdf[k] = P(X <= k)
    std::vector<double> pmf;   // pmf[k] = P(X = k)
    int n;

    BinomLUT() : n(0) {}

    void build(int n_, double p) {
        n = n_;
        pmf.resize(n + 1);
        cdf.resize(n + 1);
        for (int k = 0; k <= n; ++k)
            pmf[k] = stat::dbinom(k, n, p);
        cdf[0] = pmf[0];
        for (int k = 1; k <= n; ++k)
            cdf[k] = std::min(cdf[k - 1] + pmf[k], 1.0);
    }

    inline double pb(int k) const {          // P(X <= k)
        if (k < 0) return 0.0;
        if (k >= n) return 1.0;
        return cdf[k];
    }
    inline double pb_upper(int k) const {    // P(X > k) = 1 - P(X <= k)
        if (k < 0) return 1.0;
        if (k >= n) return 0.0;
        return 1.0 - cdf[k];
    }
};

// ============================================================
// Design result structures
// ============================================================

struct DesignParams {
    int r1 = -1, n1 = -1, r = -1, n = -1;
    double ratio = 0, pet = 0, en = 0;
    double alpha1 = 0, alpha2 = 0, alpha = 0;
    double beta1 = 0, beta2 = 0, beta = 0;
    bool valid() const { return r1 >= 0; }
};

struct RobustCandidate {
    int n, n1, n2, r1, r;
    double alpha1U, alpha2U, alphaU;
    double beta1, beta2, beta;
};

struct TwoStageDesigns {
    DesignParams optimal, minimax, balanced;
    struct RobustResult { std::string label; DesignParams params; };
    std::vector<RobustResult> robust;
    double alpha, power, beta_val, p0, p1, p0L, p0U;
    int n_lb, n_ub, pgrid_points;
    std::vector<std::string> robust_objectives;
    std::string robust_ref;
    double robust_max_inflation;
    int robust_N_cap;
    double elapsed_sec;
};

inline std::string robust_label_map(const std::string& obj) {
    if (obj == "worst_regret") return "INR (Worst Regret)";
    if (obj == "worst_en")     return "INR (Worst EN)";
    if (obj == "avg_en")       return "INR (Average EN)";
    if (obj == "avg_regret")   return "INR (Average Regret)";
    if (obj == "min_N")        return "INR (Min N)";
    return obj;
}

// ============================================================
// MAIN SEARCH
// ============================================================
inline TwoStageDesigns twostage_inr(
    double alpha, double power, double p0, double p1,
    int n_lb_in = -1, int n_ub = 150,
    double p0L_in = -1, double p0U_in = -1,
    int pgrid_points_in = -1,
    std::vector<std::string> robust_objective = {},
    std::string robust_ref = "Minimax",
    double robust_max_inflation = -1,
    int robust_N_cap = -1,
    bool robust_auto_expand = false,
    double robust_expand_step = 0.05,
    double robust_expand_max = 2.0)
{
    auto t_start = std::chrono::high_resolution_clock::now();

    const std::vector<std::string> valid_objectives =
        {"worst_regret", "worst_en", "avg_en", "avg_regret", "min_N"};
    if (robust_objective.empty()) {
        robust_objective = {"worst_regret", "avg_en"};
    } else {
        std::vector<std::string> filtered;
        for (auto& obj : valid_objectives)
            for (auto& ro : robust_objective)
                if (ro == obj) { filtered.push_back(obj); break; }
        if (filtered.empty()) throw std::runtime_error("No valid robust_objective");
        robust_objective = filtered;
    }

    double beta = 1.0 - power;
    double p0L = (p0L_in < 0) ? p0 : p0L_in;
    double p0U = (p0U_in < 0) ? p0 : p0U_in;
    if (p0L > p0U) throw std::runtime_error("Require p0L <= p0U");

    int pgrid_points;
    if (pgrid_points_in > 0) pgrid_points = pgrid_points_in;
    else if (p0L == p0U) pgrid_points = 1;
    else {
        double w = p0U - p0L;
        double step = (w <= 0.05) ? 0.0025 : (w <= 0.10) ? 0.005 : 0.01;
        pgrid_points = std::max(11, std::min(201, (int)std::floor(w / step) + 1));
    }

    // N hint
    { double za = stat::qnorm(1 - alpha), zb = stat::qnorm(power);
      double num = za*std::sqrt(p0U*(1-p0U)) + zb*std::sqrt(p1*(1-p1));
      int Nh = (int)std::ceil(num*num / ((p1-p0U)*(p1-p0U)));
      if (n_ub < Nh) std::fprintf(stderr, "Hint: n.ub=%d may be small; ~N>=%d needed.\n", n_ub, Nh);
    }

    int n_lb;
    if (n_lb_in >= 0) n_lb = n_lb_in;
    else {
        double za = stat::qnorm(1 - alpha), zb = stat::qnorm(1 - beta);
        double pa = (p0 + p1) / 2.0;
        int ns = (int)std::floor(pa*(1-pa)*std::pow((za+zb)/(p1-p0), 2));
        n_lb = std::max(2, (int)std::floor(0.5*std::min(ns, n_ub)));
    }

    double opt_en_tmp = 1e18, mm_en_tmp = 1e18, blc_en_tmp = 1e18;
    int n_tmp = n_ub + 1, diff_tmp = n_ub + 1;
    DesignParams opt, mm, blc;

    std::vector<RobustCandidate> robust_cands_all, robust_cands;
    robust_cands_all.reserve(100000);
    robust_cands.reserve(100000);

    auto rnd4 = [](double x) { return std::round(x*1e4)/1e4; };

    BinomLUT lut_n1_p1, lut_n2_p1, lut_n1_p0, lut_n2_p0, lut_n1_p0U, lut_n2_p0U;

    // ========== MAIN LOOP ==========
    for (int n = n_lb; n <= n_ub; ++n) {
        for (int n1 = 1; n1 < n; ++n1) {
            int n2 = n - n1;

            // Build LUTs ONCE per (n1, n2) — this is the key optimization
            lut_n1_p1.build(n1, p1);  lut_n2_p1.build(n2, p1);
            lut_n1_p0.build(n1, p0);  lut_n2_p0.build(n2, p0);
            lut_n1_p0U.build(n1, p0U); lut_n2_p0U.build(n2, p0U);

            for (int r1 = 0; r1 <= n1; ++r1) {
                double beta1_tmp = lut_n1_p1.pb(r1);
                if (beta1_tmp > beta) continue;

                // Find largest r with beta <= target (scan from top)
                int r_max = -1;
                double beta2_best = 0.0;
                for (int rt = r1 + n2; rt >= r1; --rt) {
                    double b2 = 0.0;
                    int rb_hi = std::min(rt, n1);
                    for (int rb = r1 + 1; rb <= rb_hi; ++rb)
                        b2 += lut_n1_p1.pmf[rb] * lut_n2_p1.pb(rt - rb);
                    if (beta1_tmp + b2 <= beta) { r_max = rt; beta2_best = b2; break; }
                }
                if (r_max < 0) continue;
                double beta_sum = beta1_tmp + beta2_best;

                // Alpha at p0
                double a1 = (r_max <= n1) ? lut_n1_p0.pb_upper(r_max) : 0.0;
                double a2 = 0.0;
                { int ra_n = std::min(r_max, n1);
                  for (int ra = r1+1; ra <= ra_n; ++ra)
                      a2 += lut_n1_p0.pmf[ra] * lut_n2_p0.pb_upper(r_max - ra);
                }
                double a_sum = a1 + a2;

                if (a_sum <= alpha) {
                    double pet = lut_n1_p0.pb(r1);
                    double en  = (double)n1 + (1.0 - pet)*(double)n2;

                    if (en < opt_en_tmp) {
                        opt_en_tmp = en;
                        opt = {r1, n1, r_max, n, rnd4((double)n1/n2), rnd4(pet), rnd4(en),
                               rnd4(a1), rnd4(a2), rnd4(a_sum),
                               rnd4(beta1_tmp), rnd4(beta2_best), rnd4(beta_sum)};
                    }
                    if (n < n_tmp) {
                        n_tmp = n; mm_en_tmp = en;
                        mm = {r1, n1, r_max, n, rnd4((double)n1/n2), rnd4(pet), rnd4(en),
                              rnd4(a1), rnd4(a2), rnd4(a_sum),
                              rnd4(beta1_tmp), rnd4(beta2_best), rnd4(beta_sum)};
                    }
                    if (mm.valid() && n == mm.n && en < mm_en_tmp) {
                        mm_en_tmp = en;
                        mm = {r1, n1, r_max, n, rnd4((double)n1/n2), rnd4(pet), rnd4(en),
                              rnd4(a1), rnd4(a2), rnd4(a_sum),
                              rnd4(beta1_tmp), rnd4(beta2_best), rnd4(beta_sum)};
                    }
                    int diff = std::abs(n1 - n2);
                    if (diff < diff_tmp || (diff == diff_tmp && en < blc_en_tmp)) {
                        diff_tmp = diff; blc_en_tmp = en;
                        blc = {r1, n1, r_max, n, rnd4((double)n1/n2), rnd4(pet), rnd4(en),
                               rnd4(a1), rnd4(a2), rnd4(a_sum),
                               rnd4(beta1_tmp), rnd4(beta2_best), rnd4(beta_sum)};
                    }
                }

                // Robust: alpha at p0U
                double a1U = (r_max <= n1) ? lut_n1_p0U.pb_upper(r_max) : 0.0;
                double a2U = 0.0;
                { int ra_n = std::min(r_max, n1);
                  for (int ra = r1+1; ra <= ra_n; ++ra)
                      a2U += lut_n1_p0U.pmf[ra] * lut_n2_p0U.pb_upper(r_max - ra);
                }
                double aU = a1U + a2U;
                if (aU <= alpha) {
                    robust_cands_all.push_back({n, n1, n2, r1, r_max,
                        a1U, a2U, aU, beta1_tmp, beta2_best, beta_sum});
                    robust_cands.push_back(robust_cands_all.back());
                }
            }
        }
    }

    // ========== Robust (INR) selection ==========
    TwoStageDesigns result;
    result.optimal = opt; result.minimax = mm; result.balanced = blc;
    result.alpha = alpha; result.power = power; result.beta_val = beta;
    result.p0 = p0; result.p1 = p1; result.p0L = p0L; result.p0U = p0U;
    result.n_lb = n_lb; result.n_ub = n_ub; result.pgrid_points = pgrid_points;
    result.robust_objectives = robust_objective; result.robust_ref = robust_ref;
    result.robust_max_inflation = robust_max_inflation; result.robust_N_cap = robust_N_cap;

    bool has_classic = opt.valid() || mm.valid() || blc.valid();
    bool has_inr = !robust_cands.empty();
    if (!has_classic && !has_inr) std::fprintf(stderr, "No design feasible.\n");
    else if (has_classic && !has_inr) std::fprintf(stderr, "No INR design feasible.\n");

    if (has_inr) {
        std::vector<double> pgrid;
        if (p0L == p0U) pgrid.push_back(p0U);
        else { pgrid.resize(pgrid_points);
            for (int i = 0; i < pgrid_points; ++i)
                pgrid[i] = p0L + (p0U-p0L)*(double)i/(double)(pgrid_points-1);
        }

        auto RC = robust_cands;
        int nrows = (int)pgrid.size();

        auto build_EN = [&](const std::vector<RobustCandidate>& C) {
            int nc = (int)C.size();
            std::vector<double> mat(nrows * nc);
            for (int j = 0; j < nc; ++j)
                for (int i = 0; i < nrows; ++i) {
                    double pet = stat::pbinom(C[j].r1, C[j].n1, pgrid[i]);
                    mat[i*nc+j] = (double)C[j].n1 + (1-pet)*(double)C[j].n2;
                }
            return mat;
        };

        auto EN_mat = build_EN(RC);
        int ncols = (int)RC.size();

        // N cap logic
        int Nref = mm.valid() ? mm.n : -1;
        if (robust_ref == "Optimal" && opt.valid()) Nref = opt.n;
        int Ncap = robust_N_cap;
        if (Ncap < 0 && robust_max_inflation >= 0 && Nref > 0)
            Ncap = (int)std::floor((1+robust_max_inflation)*Nref);

        if (Ncap > 0) {
            int kc = 0; for (auto& c : RC) if (c.n <= Ncap) kc++;
            if (kc == 0) {
                if (robust_auto_expand && Nref > 0 && robust_max_inflation >= 0) {
                    double infl = robust_max_inflation;
                    while (infl <= robust_expand_max) {
                        int tc = (int)std::floor((1+infl)*Nref);
                        for (auto& c : robust_cands_all) if (c.n <= tc) { kc=1; break; }
                        if (kc > 0) { Ncap = tc; break; }
                        infl += robust_expand_step;
                    }
                    if (kc > 0) { RC.clear(); for (auto& c : robust_cands_all) if (c.n <= Ncap) RC.push_back(c); }
                    else RC = robust_cands_all;
                } else RC = robust_cands_all;
                EN_mat = build_EN(RC); ncols = (int)RC.size();
            } else {
                std::vector<RobustCandidate> filt; filt.reserve(kc);
                for (auto& c : RC) if (c.n <= Ncap) filt.push_back(c);
                RC = filt; EN_mat = build_EN(RC); ncols = (int)RC.size();
            }
        }

        std::vector<double> best_EN(nrows, 1e18);
        for (int i = 0; i < nrows; ++i)
            for (int j = 0; j < ncols; ++j)
                best_EN[i] = std::min(best_EN[i], EN_mat[i*ncols+j]);

        std::vector<double> wEN(ncols), aEN(ncols), wReg(ncols), aReg(ncols);
        std::vector<int> Nv(ncols);
        for (int j = 0; j < ncols; ++j) {
            Nv[j] = RC[j].n;
            double se=0, sr=0, me=-1e18, mr=-1e18;
            for (int i = 0; i < nrows; ++i) {
                double e = EN_mat[i*ncols+j], rg = e - best_EN[i];
                se += e; sr += rg;
                me = std::max(me, e); mr = std::max(mr, rg);
            }
            wEN[j]=me; aEN[j]=se/nrows; wReg[j]=mr; aReg[j]=sr/nrows;
        }

        for (auto& obj : robust_objective) {
            std::vector<double> sc(ncols);
            for (int j = 0; j < ncols; ++j) {
                if      (obj=="worst_regret") sc[j]=wReg[j];
                else if (obj=="worst_en")     sc[j]=wEN[j];
                else if (obj=="avg_en")       sc[j]=aEN[j];
                else if (obj=="avg_regret")   sc[j]=aReg[j];
                else                          sc[j]=(double)Nv[j];
            }
            double ms = *std::min_element(sc.begin(), sc.end());
            std::vector<int> idx; for (int j=0;j<ncols;++j) if(sc[j]==ms) idx.push_back(j);
            // Tie-breaks
            auto tiebreak = [&](auto& idx_, auto& vec_) {
                if (idx_.size()<=1) return;
                double b=1e18; for (int j:idx_) b=std::min(b,vec_[j]);
                std::vector<int> t; for (int j:idx_) if(vec_[j]==b) t.push_back(j);
                idx_ = t;
            };
            tiebreak(idx, wEN);
            if (idx.size()>1) { std::vector<double> nf(ncols); for(int j=0;j<ncols;++j) nf[j]=Nv[j]; tiebreak(idx,nf); }
            tiebreak(idx, aEN);

            int ch = idx[0];
            DesignParams rob;
            rob.r1=RC[ch].r1; rob.n1=RC[ch].n1; rob.r=RC[ch].r; rob.n=RC[ch].n;
            rob.ratio=rnd4((double)rob.n1/(rob.n-rob.n1));
            rob.pet=rnd4(stat::pbinom(rob.r1,rob.n1,p0U));
            rob.en=rnd4(wEN[ch]);
            rob.alpha1=rnd4(RC[ch].alpha1U); rob.alpha2=rnd4(RC[ch].alpha2U);
            rob.alpha=rnd4(RC[ch].alphaU);
            rob.beta1=rnd4(RC[ch].beta1); rob.beta2=rnd4(RC[ch].beta2);
            rob.beta=rnd4(RC[ch].beta);
            result.robust.push_back({robust_label_map(obj), rob});
        }
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    result.elapsed_sec = std::chrono::duration<double>(t_end - t_start).count();
    return result;
}
