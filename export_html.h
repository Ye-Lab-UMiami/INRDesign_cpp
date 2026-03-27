#pragma once
// ============================================================
// export_html.h  --  Self-contained HTML report with SVG plot
//
// Mirrors the R export_design_html() output:
//   - Operating characteristics table (styled)
//   - Rejection probability SVG plot
//   - Design summary paragraph
//   - Decision rules table
//   - OpChar summary paragraph
//   - Design inputs table
//   - References
// ============================================================

#include "twostage_core.h"
#include "calc_opchar.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>

namespace html_export {

// ---- formatting helpers ----
static std::string f4(double x) { char b[32]; std::snprintf(b,32,"%.4f",x); return b; }
static std::string f2(double x) { char b[32]; std::snprintf(b,32,"%.2f",x); return b; }
static std::string fi(int x)    { return std::to_string(x); }

// ---- escape HTML ----
static std::string esc(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (char c : s) {
        if (c=='<') out += "&lt;";
        else if (c=='>') out += "&gt;";
        else if (c=='&') out += "&amp;";
        else if (c=='"') out += "&quot;";
        else out += c;
    }
    return out;
}

// ---- collect all design names from a TwoStageDesigns ----
static std::vector<std::string> all_design_names(const TwoStageDesigns& d) {
    std::vector<std::string> names;
    if (d.optimal.valid())  names.push_back("Optimal");
    if (d.minimax.valid())  names.push_back("Minimax");
    if (d.balanced.valid()) names.push_back("Balanced");
    for (auto& r : d.robust) if (r.params.valid()) names.push_back(r.label);
    return names;
}

// ---- case-insensitive partial match (like R's grepl + tolower) ----
static std::string str_lower(const std::string& s) {
    std::string out = s;
    for (auto& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

// Match patterns against available design names.
// Supports exact match (case-insensitive) and substring match.
// e.g. "INR" matches "INR (Worst Regret)" and "INR (Average EN)"
static std::vector<std::string> match_design_names(
    const std::vector<std::string>& patterns,
    const std::vector<std::string>& available)
{
    std::vector<std::string> matched;
    for (auto& pat : patterns) {
        std::string pat_lc = str_lower(pat);
        // Try exact match first
        bool found_exact = false;
        for (auto& av : available) {
            if (str_lower(av) == pat_lc) {
                if (std::find(matched.begin(), matched.end(), av) == matched.end())
                    matched.push_back(av);
                found_exact = true;
            }
        }
        // If no exact match, try substring
        if (!found_exact) {
            for (auto& av : available) {
                if (str_lower(av).find(pat_lc) != std::string::npos) {
                    if (std::find(matched.begin(), matched.end(), av) == matched.end())
                        matched.push_back(av);
                }
            }
        }
    }
    return matched;
}

// ---- check if a design name is in the keep list ----
static bool should_keep(const std::string& name,
                        const std::vector<std::string>& keep) {
    if (keep.empty()) return true;  // empty = keep all
    return std::find(keep.begin(), keep.end(), name) != keep.end();
}

// ---- color palette (matches ggplot2 default hue scale) ----
static std::string design_color(int idx, int total) {
    // Use a nice categorical palette
    const char* palette[] = {
        "#F8766D", "#00BA38", "#619CFF", "#E76BF3", "#00BFC4",
        "#FF6C91", "#A3A500", "#00B0F6", "#E58700", "#CD9600"
    };
    return palette[idx % 10];
}

// ============================================================
// Build SVG rejection rate plot
// ============================================================
static std::string build_rejection_svg(
    const TwoStageDesigns& d,
    const std::vector<std::string>& designs_keep = {},
    int grid_n = 201)
{
    struct DesignInfo { std::string name; int n1, r1, n, r; };
    std::vector<DesignInfo> designs;
    auto add = [&](const std::string& nm, const DesignParams& dp) {
        if (dp.valid() && should_keep(nm, designs_keep))
            designs.push_back({nm, dp.n1, dp.r1, dp.n, dp.r});
    };
    add("Optimal", d.optimal);
    add("Minimax", d.minimax);
    add("Balanced", d.balanced);
    for (auto& rob : d.robust) add(rob.label, rob.params);

    if (designs.empty()) return "";

    // Chart dimensions
    const double W = 700, H = 380;
    const double ML = 70, MR = 20, MT = 20, MB = 70;  // margins
    const double PW = W - ML - MR, PH = H - MT - MB;   // plot area

    double pL = d.p0L, pU = d.p0U;
    double ymin = 0, ymax = -1;

    // Compute all curves
    struct Curve { std::string name; std::vector<double> x, y; };
    std::vector<Curve> curves;
    for (auto& di : designs) {
        Curve c; c.name = di.name;
        for (int i = 0; i < grid_n; ++i) {
            double p = pL + (pU - pL) * (double)i / (double)(grid_n - 1);
            double rp = pr_reject_two_stage(di.n1, di.r1, di.n, di.r, p);
            c.x.push_back(p);
            c.y.push_back(rp);
            ymax = std::max(ymax, rp);
        }
        curves.push_back(c);
    }
    // Round ymax up for nice axis
    ymax = std::ceil(ymax * 20.0) / 20.0;
    if (ymax > 1.0) ymax = 1.0;

    auto px = [&](double p) { return ML + (p - pL) / (pU - pL) * PW; };
    auto py = [&](double v) { return MT + PH - (v - ymin) / (ymax - ymin) * PH; };

    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << W << "\" height=\"" << H + 30 * ((int)designs.size() / 3 + 1) << "\" "
        << "viewBox=\"0 0 " << W << " " << H + 30 * ((int)designs.size() / 3 + 1) << "\" "
        << "style=\"font-family:Arial,sans-serif;\">\n";

    // Background
    svg << "<rect x=\"" << ML << "\" y=\"" << MT << "\" width=\"" << PW
        << "\" height=\"" << PH << "\" fill=\"#fafafa\" stroke=\"#ddd\"/>\n";

    // Grid lines & Y-axis labels
    int ny_ticks = 5;
    for (int i = 0; i <= ny_ticks; ++i) {
        double v = ymin + (ymax - ymin) * i / ny_ticks;
        double y_pos = py(v);
        svg << "<line x1=\"" << ML << "\" y1=\"" << y_pos << "\" x2=\"" << ML + PW
            << "\" y2=\"" << y_pos << "\" stroke=\"#eee\" stroke-width=\"1\"/>\n";
        char lbl[16]; std::snprintf(lbl, 16, "%.1f%%", v * 100);
        svg << "<text x=\"" << ML - 5 << "\" y=\"" << y_pos + 4
            << "\" text-anchor=\"end\" font-size=\"10\" fill=\"#666\">" << lbl << "</text>\n";
    }

    // X-axis ticks
    int nx_ticks = 5;
    for (int i = 0; i <= nx_ticks; ++i) {
        double p = pL + (pU - pL) * i / nx_ticks;
        double x_pos = px(p);
        svg << "<line x1=\"" << x_pos << "\" y1=\"" << MT + PH << "\" x2=\"" << x_pos
            << "\" y2=\"" << MT + PH + 5 << "\" stroke=\"#999\" stroke-width=\"1\"/>\n";
        char lbl[16]; std::snprintf(lbl, 16, "%.3f", p);
        svg << "<text x=\"" << x_pos << "\" y=\"" << MT + PH + 18
            << "\" text-anchor=\"middle\" font-size=\"10\" fill=\"#666\">" << lbl << "</text>\n";
    }

    // Axis labels
    svg << "<text x=\"" << ML + PW / 2 << "\" y=\"" << H - 10
        << "\" text-anchor=\"middle\" font-size=\"11\" fill=\"#333\">"
        << "True underlying response rate (p) across the null interval</text>\n";
    svg << "<text x=\"15\" y=\"" << MT + PH / 2
        << "\" text-anchor=\"middle\" font-size=\"11\" fill=\"#333\" "
        << "transform=\"rotate(-90,15," << MT + PH / 2 << ")\">"
        << "Probability of rejection</text>\n";

    // Curves
    for (size_t ci = 0; ci < curves.size(); ++ci) {
        auto& c = curves[ci];
        std::string color = design_color((int)ci, (int)curves.size());
        svg << "<polyline fill=\"none\" stroke=\"" << color << "\" stroke-width=\"2.2\" "
            << "stroke-linecap=\"round\" stroke-linejoin=\"round\" points=\"";
        for (size_t i = 0; i < c.x.size(); ++i) {
            if (i > 0) svg << " ";
            svg << px(c.x[i]) << "," << py(c.y[i]);
        }
        svg << "\"/>\n";
    }

    // Alpha line
    svg << "<line x1=\"" << ML << "\" y1=\"" << py(d.alpha) << "\" x2=\"" << ML + PW
        << "\" y2=\"" << py(d.alpha) << "\" stroke=\"#999\" stroke-width=\"1\" "
        << "stroke-dasharray=\"6,3\"/>\n";
    svg << "<text x=\"" << ML + PW + 3 << "\" y=\"" << py(d.alpha) + 4
        << "\" font-size=\"9\" fill=\"#999\">" << "&#945;=" << f2(d.alpha) << "</text>\n";

    // Legend (below plot)
    double legend_y = H + 5;
    double legend_x_start = ML;
    double col_width = 200;
    int cols = 3;
    for (size_t ci = 0; ci < curves.size(); ++ci) {
        int row = (int)ci / cols;
        int col = (int)ci % cols;
        double lx = legend_x_start + col * col_width;
        double ly = legend_y + row * 22;
        std::string color = design_color((int)ci, (int)curves.size());
        svg << "<line x1=\"" << lx << "\" y1=\"" << ly + 5 << "\" x2=\"" << lx + 20
            << "\" y2=\"" << ly + 5 << "\" stroke=\"" << color << "\" stroke-width=\"2.5\"/>\n";
        svg << "<text x=\"" << lx + 25 << "\" y=\"" << ly + 9
            << "\" font-size=\"10\" fill=\"#333\">" << esc(curves[ci].name) << "</text>\n";
    }

    svg << "</svg>\n";
    return svg.str();
}

// ============================================================
// Build styled HTML table
// ============================================================
static std::string table_css() {
    return R"(
    .styled-table {
        border-collapse: collapse;
        font-size: 12px;
        font-family: Arial, sans-serif;
        margin-bottom: 20px;
    }
    .styled-table th {
        background: #f0f0f0;
        border: 1px solid #ccc;
        padding: 6px 10px;
        text-align: center;
        font-weight: bold;
        font-size: 11px;
    }
    .styled-table td {
        border: 1px solid #ddd;
        padding: 5px 10px;
        text-align: center;
    }
    .styled-table td:first-child {
        text-align: left;
        font-weight: 600;
    }
    .styled-table tr:nth-child(even) {
        background: #fafafa;
    }
    .styled-table .spanner {
        background: #e0e0e0;
        font-weight: bold;
        text-align: center;
    }
    .footnote { font-size: 10px; color: #555; margin-top: 4px; line-height: 1.6; }
    .footnote b { font-weight: 600; }
    )";
}

// ============================================================
// Operating Characteristics HTML table
// ============================================================
static std::string build_opchar_table(
    const TwoStageDesigns& d, const OpcharResult& op,
    const std::vector<std::string>& designs_keep = {},
    bool details = true)
{
    bool has_iv = (d.p0L >= 0 && d.p0U >= 0 && d.p0L != d.p0U);

    std::ostringstream s;
    s << "<table class=\"styled-table\">\n";

    // Spanner row
    s << "<tr>";
    s << "<th rowspan=\"2\">Design</th>";
    s << "<th rowspan=\"2\">(n1, r1, n, r)</th>";
    if (has_iv) s << "<th colspan=\"4\" class=\"spanner\">Expected Sample Size (EN)</th>";
    else        s << "<th colspan=\"1\" class=\"spanner\">EN</th>";
    if (has_iv) s << "<th colspan=\"3\" class=\"spanner\">Prob. Early Termination (PET)</th>";
    else        s << "<th colspan=\"1\" class=\"spanner\">PET</th>";
    s << "<th colspan=\"" << (has_iv ? 4 : 2) << "\" class=\"spanner\">Type I Error</th>";
    s << "<th colspan=\"2\" class=\"spanner\">Power</th>";
    s << "</tr>\n";

    // Sub-header row
    s << "<tr>";
    if (has_iv) {
        s << "<th>EN(p0L)</th><th>EN(p0)</th><th>EN(p0U)</th><th>Avg. EN</th>";
        s << "<th>PET(p0L)</th><th>PET(p0)</th><th>PET(p0U)</th>";
        s << "<th>Target</th><th>&#945;(p0L)</th><th>&#945;(p0)</th><th>&#945;(p0U)</th>";
    } else {
        s << "<th>EN(p0)</th>";
        s << "<th>PET(p0)</th>";
        s << "<th>Target</th><th>&#945;(p0)</th>";
    }
    s << "<th>Target</th><th>Power(p1)</th>";
    s << "</tr>\n";

    // Data rows
    for (auto& row : op.rows) {
        if (row.skipped) continue;
        if (!should_keep(row.design_name, designs_keep)) continue;
        char bounds[64];
        std::snprintf(bounds, 64, "(%d, %d, %d, %d)", row.n1, row.r1, row.n, row.r);

        s << "<tr>";
        s << "<td>" << esc(row.design_name) << "</td>";
        s << "<td>" << bounds << "</td>";
        if (has_iv) {
            s << "<td>" << f2(row.EN_p0L) << "</td><td>" << f2(row.EN_p0)
              << "</td><td>" << f2(row.EN_p0U) << "</td><td>" << f2(row.avg_EN) << "</td>";
            s << "<td>" << f4(row.PET_p0L) << "</td><td>" << f4(row.PET_p0)
              << "</td><td>" << f4(row.PET_p0U) << "</td>";
            s << "<td>" << f2(row.alpha_target) << "</td><td>" << f4(row.alpha_at_p0L)
              << "</td><td>" << f4(row.alpha_at_p0) << "</td><td>" << f4(row.alpha_at_p0U) << "</td>";
        } else {
            s << "<td>" << f2(row.EN_p0) << "</td>";
            s << "<td>" << f4(row.PET_p0) << "</td>";
            s << "<td>" << f2(row.alpha_target) << "</td><td>" << f4(row.alpha_at_p0) << "</td>";
        }
        s << "<td>" << f2(1.0 - d.beta_val) << "</td><td>" << f4(row.power_at_p1) << "</td>";
        s << "</tr>\n";
    }
    s << "</table>\n";

    // Footnotes
    s << "<div class=\"footnote\">\n";
    // Design definitions
    auto has_design = [&](const std::string& name) {
        if (!should_keep(name, designs_keep)) return false;
        for (auto& r : op.rows) if (r.design_name == name) return true;
        return false;
    };
    s << "<p><b>Designs:</b> ";
    if (has_design("Optimal"))  s << "Optimal: Simon's (1989) design that minimizes expected sample size. ";
    if (has_design("Minimax"))  s << "Minimax: Simon's (1989) design minimizes maximum sample size. ";
    if (has_design("Balanced")) s << "Balanced: Ye &amp; Shyr (2007) design that balances stage sizes. ";
    if (has_design("INR (Worst Regret)")) s << "INR (Worst Regret): minimizes worst-case regret. ";
    if (has_design("INR (Average EN)"))   s << "INR (Average EN): minimizes average EN. ";
    if (has_design("INR (Worst EN)"))     s << "INR (Worst EN): minimizes worst-case EN. ";
    if (has_design("INR (Average Regret)")) s << "INR (Average Regret): minimizes average regret. ";
    if (has_design("INR (Min N)"))        s << "INR (Min N): minimizes total sample size. ";
    s << "</p>\n";

    if (details) {
        s << "<p>n1: sample size in stage 1. "
          << "r1: early stopping boundary (stop if responses &#8804; r1). "
          << "n: total sample size. "
          << "r: final rejection boundary (reject if responses &gt; r).</p>\n";
        s << "<p>Expected Sample Size (EN): expected (average) sample size if the design was repeated. ";
        if (has_iv) {
            s << "EN(p0L), EN(p0), EN(p0U): evaluated at the corresponding response rates. "
              << "Avg. EN: average expected sample size across the interval [p0L, p0U]. ";
        }
        s << "</p>\n";
        s << "<p>Probability of Early Termination (PET): probability of stopping after stage 1. ";
        if (has_iv) {
            s << "PET(p0L), PET(p0), PET(p0U): evaluated at the corresponding response rates. ";
        }
        s << "</p>\n";
        s << "<p>Type I Error: probability of rejecting H0 when true. "
          << "Target: required type I error rate. ";
        if (has_iv)
            s << "&#945;(p0L), &#945;(p0), &#945;(p0U): achieved type I error at corresponding response rates. ";
        s << "</p>\n";
        s << "<p>Power: probability of rejecting H0 when p = p1. "
          << "Target: required power. Power(p1): achieved power at p1.</p>\n";
    }
    s << "</div>\n";
    return s.str();
}

// ============================================================
// Decision rules table
// ============================================================
static std::string build_rules_table(const DesignParams& dp) {
    std::ostringstream s;
    s << "<table class=\"styled-table\">\n";
    s << "<tr><th>Stage</th><th>Sample Size (n)</th><th>Decision</th></tr>\n";
    s << "<tr><td>Stage 1</td><td style=\"text-align:center\">" << dp.n1
      << "</td><td>Stop if &#8804; " << dp.r1 << " responses</td></tr>\n";
    s << "<tr><td>Stage 2</td><td style=\"text-align:center\">" << dp.n
      << "</td><td>Declare success if &gt; " << dp.r << " responses</td></tr>\n";
    s << "</table>\n";
    return s.str();
}

// ============================================================
// Design inputs table
// ============================================================
static std::string build_inputs_table(const TwoStageDesigns& d) {
    std::ostringstream s;
    s << "<table class=\"styled-table\">\n";
    s << "<tr><th>Parameter</th><th>Value</th></tr>\n";
    s << "<tr><td>p0</td><td>" << f2(d.p0) << "</td></tr>\n";
    s << "<tr><td>p1</td><td>" << f2(d.p1) << "</td></tr>\n";
    bool degenerate = (d.p0 == d.p0L && d.p0 == d.p0U);
    if (!degenerate) {
        s << "<tr><td>p0L</td><td>" << f2(d.p0L) << "</td></tr>\n";
        s << "<tr><td>p0U</td><td>" << f2(d.p0U) << "</td></tr>\n";
    }
    s << "<tr><td>alpha</td><td>" << f2(d.alpha) << "</td></tr>\n";
    s << "<tr><td>power</td><td>" << f2(d.power) << "</td></tr>\n";
    s << "<tr><td>n.ub</td><td>" << d.n_ub << "</td></tr>\n";
    s << "</table>\n";
    return s.str();
}

// ============================================================
// Design summary paragraph (same as print_summary in main.cpp)
// ============================================================
static std::string build_summary_text(
    const TwoStageDesigns& d, const std::string& design_name)
{
    const DesignParams* dp = nullptr;
    std::string label;
    if (design_name == "Optimal")  { dp = &d.optimal;  label = "Simon (Optimal)"; }
    else if (design_name == "Minimax")  { dp = &d.minimax;  label = "Simon (Minimax)"; }
    else if (design_name == "Balanced") { dp = &d.balanced; label = "Balanced"; }
    else {
        for (auto& r : d.robust)
            if (r.label == design_name) { dp = &r.params; label = r.label; break; }
    }
    if (!dp || !dp->valid()) return "(Design not found)";

    bool is_inr = (label.substr(0,3) == "INR");
    char buf[2048];
    char hyp[256], enp[256];

    if (is_inr && d.p0L >= 0 && d.p0U >= 0)
        std::snprintf(hyp, 256, "H0: P &#8712; [%.2f, %.2f] versus H1: P &#8805; %.2f", d.p0L, d.p0U, d.p1);
    else
        std::snprintf(hyp, 256, "H0: P &#8804; %.2f versus H1: P &#8805; %.2f", d.p0, d.p1);

    if (is_inr && d.p0L >= 0 && d.p0U >= 0)
        std::snprintf(enp, 256,
            " At the upper bound of the null interval (p = %.2f), the expected sample size "
            "for this design is %.2f, with a probability of stopping after Stage 1 of %.3f.",
            d.p0U, dp->en, dp->pet);
    else
        std::snprintf(enp, 256,
            " The expected sample size for this design is %.2f, with a probability of stopping "
            "after Stage 1 of %.3f.", dp->en, dp->pet);

    int n2 = dp->n - dp->n1;
    std::snprintf(buf, sizeof(buf),
        "A two-stage phase II single-arm clinical trial design, %s, is defined to "
        "evaluate whether the true response rate (P) supports continuation to the "
        "next phase of the clinical trial (%s). "
        "The design is specified with a type I error rate of %.2f and power of %.0f%%. "
        "The total number of subjects required if the trial continues to the second "
        "stage is %d, with %d subjects enrolled in the first stage and an additional "
        "%d subjects enrolled in the second stage, if necessary.%s "
        "After the first stage, the trial will be terminated early for futility if "
        "%d or fewer responses are observed. "
        "Otherwise, the study will continue to the second stage. "
        "At the conclusion of the trial, the treatment will be considered promising "
        "if more than %d of the %d subjects respond.",
        label.c_str(), hyp, d.alpha, (1-d.beta_val)*100,
        dp->n, dp->n1, n2, enp, dp->r1, dp->r, dp->n);

    return buf;
}

// ============================================================
// OpChar summary paragraph
// ============================================================
static std::string build_opchar_summary_text(
    const TwoStageDesigns& d, const OpcharResult& op,
    const std::string& design_name)
{
    const OpcharRow* row = nullptr;
    for (auto& r : op.rows)
        if (r.design_name == design_name) { row = &r; break; }
    if (!row || row->skipped) return "(Design not found)";

    bool has_iv = (d.p0L >= 0 && d.p0U >= 0 && d.p0L != d.p0U);
    char buf[2048];

    if (has_iv) {
        std::snprintf(buf, sizeof(buf),
            "Operating characteristics are evaluated at p0 = %.2f, p0L = %.2f, p0U = %.2f, "
            "and p1 = %.2f. The probability of early termination ranges from %.4f to %.4f. "
            "The expected sample size ranges from %.2f to %.2f (average %.2f). "
            "The type I error rate ranges from %.4f to %.4f, and the achieved power is %.4f.",
            op.p0, op.p0L, op.p0U, op.p1,
            row->PET_p0L, row->PET_p0U,
            row->EN_p0L, row->EN_p0U, row->avg_EN,
            row->alpha_at_p0L, row->alpha_at_p0U,
            row->power_at_p1);
    } else {
        std::snprintf(buf, sizeof(buf),
            "Operating characteristics are evaluated at p0 = %.2f and p1 = %.2f. "
            "The probability of early termination is %.4f and the expected sample size is %.2f. "
            "The type I error rate is %.4f, and the achieved power is %.4f.",
            op.p0, op.p1,
            row->PET_p0, row->EN_p0,
            row->alpha_at_p0, row->power_at_p1);
    }
    return buf;
}

// ============================================================
// MAIN EXPORT FUNCTION
// ============================================================
static void export_design_html(
    const TwoStageDesigns& d,
    const std::string& design_name,
    const std::string& filepath,
    std::vector<std::string> designs_keep = {})
{
    // Find the selected design
    const DesignParams* dp = nullptr;
    if (design_name == "Optimal")  dp = &d.optimal;
    else if (design_name == "Minimax")  dp = &d.minimax;
    else if (design_name == "Balanced") dp = &d.balanced;
    else {
        for (auto& r : d.robust)
            if (r.label == design_name) { dp = &r.params; break; }
    }
    if (!dp || !dp->valid()) {
        std::fprintf(stderr, "Error: design '%s' not found.\n", design_name.c_str());
        return;
    }

    // Resolve designs_keep: if empty, keep all; otherwise match patterns
    if (!designs_keep.empty()) {
        auto avail = all_design_names(d);
        designs_keep = match_design_names(designs_keep, avail);
        // Always include the featured design
        if (std::find(designs_keep.begin(), designs_keep.end(), design_name)
            == designs_keep.end()) {
            designs_keep.push_back(design_name);
        }
    }

    // Compute operating characteristics
    auto op = calc_opchar_from_designs(d);

    // Build components (pass designs_keep filter)
    std::string opchar_html = build_opchar_table(d, op, designs_keep, true);
    std::string svg_plot    = build_rejection_svg(d, designs_keep);
    std::string design_summary = build_summary_text(d, design_name);
    std::string opchar_summary = build_opchar_summary_text(d, op, design_name);
    std::string rules_html  = build_rules_table(*dp);
    std::string inputs_html = build_inputs_table(d);

    // Assemble full HTML
    std::ofstream out(filepath);
    if (!out.is_open()) {
        std::fprintf(stderr, "Error: cannot open '%s' for writing.\n", filepath.c_str());
        return;
    }

    out << R"(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Two-Stage Designs</title>
<style>
body {
    font-family: Arial, Helvetica, sans-serif;
    margin: 40px;
    max-width: 1400px;
    line-height: 1.6;
    color: #333;
}
h1 { font-size: 26px; margin-bottom: 5px; }
.subtitle { font-size: 18px; font-weight: 600; color: #555; margin-top: -5px; margin-bottom: 25px; }
h3 { margin-top: 30px; color: #222; }
h4 { margin-top: 20px; color: #333; }
hr { border: none; border-top: 1px solid #ccc; margin: 10px 0 25px 0; }
p { margin: 8px 0; }
ol { padding-left: 20px; }
ol li { margin: 4px 0; font-size: 13px; }
)" << table_css() << R"(
</style>
</head>
<body>

<h1>Two-Stage Designs for Tests of One Proportion</h1>
<div class="subtitle">Simon and Interval-Null Robust (INR)</div>
<hr>

)" << opchar_html << R"(

<h4>Figure: Rejection Probability Across the Null Interval</h4>
<div style="text-align:left;">
)" << svg_plot << R"(
</div>

<h3>)" << esc(design_name) << R"( Design</h3>

<h4>Design Summary</h4>
<p>)" << design_summary << R"(</p>

<h4>Decision Rules</h4>
)" << rules_html << R"(

<h4>Operating Characteristics Summary</h4>
<p>)" << opchar_summary << R"(</p>

<h3>References</h3>
<ol>
<li>Simon, R. (1989). Optimal two-stage designs for phase II clinical trials.</li>
<li>Ye, F., &amp; Shyr, Y. (2007). Balanced two-stage designs for phase II clinical trials.</li>
<li>INR designs: Irlmeier, R., Zhuoli, J., &amp; Ye, F. (in preparation).</li>
</ol>

<h3>Appendix: Design Inputs</h3>
)" << inputs_html << R"(

<div style="font-size:10px; color:#999; margin-top:30px; border-top:1px solid #eee; padding-top:10px;">
Generated by twostage_inr (C++) in )" << f2(d.elapsed_sec) << R"( seconds.
</div>

</body>
</html>
)";

    out.close();
    std::fprintf(stderr, "Saved to: %s\n", filepath.c_str());
}

} // namespace html_export