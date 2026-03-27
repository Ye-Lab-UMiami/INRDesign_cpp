#pragma once
// ============================================================
// calc_opchar.h  –  Operating characteristics computation
// ============================================================

#include "twostage_core.h"
#include <vector>
#include <string>
#include <cmath>

struct OpcharRow {
    std::string design_name;
    int n1, r1, n, r;
    double alpha_target;
    double alpha_at_p0, power_at_p1;
    double PET_p0, EN_p0;
    // Interval null (valid only if has_interval)
    double alpha_at_p0L, alpha_at_p0U, sup_alpha;
    double PET_p0L, PET_p0U;
    double EN_p0L, EN_p0U, avg_EN;
    bool has_interval;
    bool skipped;
};

inline OpcharRow calc_opchar_single(
    int n1, int r1, int n, int r,
    double p0, double p1, double alpha_target,
    double p0L = -1, double p0U = -1, int grid_points = 101)
{
    OpcharRow row;
    row.n1 = n1; row.r1 = r1; row.n = n; row.r = r;
    row.alpha_target = alpha_target;
    row.has_interval = (p0L >= 0 && p0U >= 0);
    row.skipped = false;

    // Check finite
    auto is_fin = [](double x) { return std::isfinite(x); };
    if (!is_fin(n1) || !is_fin(r1) || !is_fin(n) || !is_fin(r) ||
        !is_fin(p0) || !is_fin(p1) || !is_fin(alpha_target)) {
        row.skipped = true;
        return row;
    }
    if (row.has_interval && (!is_fin(p0L) || !is_fin(p0U))) {
        row.skipped = true;
        return row;
    }

    row.alpha_at_p0 = std::min(std::max(pr_reject_two_stage(n1, r1, n, r, p0), 0.0), 1.0);
    row.power_at_p1 = std::min(std::max(pr_reject_two_stage(n1, r1, n, r, p1), 0.0), 1.0);
    row.PET_p0 = pet_two_stage(n1, r1, p0);
    row.EN_p0  = en_two_stage(n1, r1, n, p0);

    if (row.has_interval) {
        row.alpha_at_p0L = std::min(std::max(pr_reject_two_stage(n1, r1, n, r, p0L), 0.0), 1.0);
        row.alpha_at_p0U = std::min(std::max(pr_reject_two_stage(n1, r1, n, r, p0U), 0.0), 1.0);
        row.PET_p0L = pet_two_stage(n1, r1, p0L);
        row.PET_p0U = pet_two_stage(n1, r1, p0U);
        row.EN_p0L  = en_two_stage(n1, r1, n, p0L);
        row.EN_p0U  = en_two_stage(n1, r1, n, p0U);

        // Grid evaluation
        double sup_a = -1.0;
        double sum_en = 0.0;
        for (int i = 0; i < grid_points; ++i) {
            double pp = (grid_points == 1) ? p0U :
                p0L + (p0U - p0L) * (double)i / (double)(grid_points - 1);
            double a = pr_reject_two_stage(n1, r1, n, r, pp);
            double en_val = en_two_stage(n1, r1, n, pp);
            sup_a = std::max(sup_a, a);
            sum_en += en_val;
        }
        row.sup_alpha = std::min(std::max(sup_a, 0.0), 1.0);
        row.avg_EN = sum_en / grid_points;
    }

    return row;
}

struct OpcharResult {
    std::vector<OpcharRow> rows;
    double p0, p1, p0L, p0U, alpha_target, beta_target;
};

// Compute opchar for all designs in a TwoStageDesigns result
inline OpcharResult calc_opchar_from_designs(
    const TwoStageDesigns& designs,
    double p0 = -1, double p1 = -1,
    double alpha_target = -1,
    double p0L = -1, double p0U = -1,
    int grid_points = 101)
{
    if (p0 < 0) p0 = designs.p0;
    if (p1 < 0) p1 = designs.p1;
    if (alpha_target < 0) alpha_target = designs.alpha;
    if (p0L < 0) p0L = designs.p0L;
    if (p0U < 0) p0U = designs.p0U;

    OpcharResult result;
    result.p0 = p0; result.p1 = p1;
    result.p0L = p0L; result.p0U = p0U;
    result.alpha_target = alpha_target;
    result.beta_target = designs.beta_val;

    auto add_design = [&](const std::string& name, const DesignParams& dp) {
        if (!dp.valid()) return;
        auto row = calc_opchar_single(dp.n1, dp.r1, dp.n, dp.r,
                                       p0, p1, alpha_target, p0L, p0U, grid_points);
        row.design_name = name;
        result.rows.push_back(row);
    };

    add_design("Optimal", designs.optimal);
    add_design("Minimax", designs.minimax);
    add_design("Balanced", designs.balanced);
    for (auto& rob : designs.robust) {
        add_design(rob.label, rob.params);
    }

    return result;
}
