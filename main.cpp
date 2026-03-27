// ============================================================
// main.cpp  --  Two-Stage Phase II Design (INR) CLI
//
// Usage:
//   twostage_inr --alpha 0.05 --power 0.80 --p0 0.20 --p1 0.35 \
//                --p0L 0.18 --p0U 0.22 --n_ub 150 \
//                [--n_lb 10] [--objectives worst_regret,avg_en] \
//                [--robust_ref Minimax] [--format text|json|csv]
//
// Outputs the same design table as the R version.
// ============================================================

#include "twostage_core.h"
#include "calc_opchar.h"
#include "export_html.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <chrono>

// ============================================================
// Helpers
// ============================================================
static std::string fmt4(double x) {
    if (x != x) return "NA";  // NaN check
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", x);
    return buf;
}
static std::string fmt2(double x) {
    if (x != x) return "NA";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", x);
    return buf;
}
static std::string fmtInt(int x) {
    if (x < 0) return "NA";
    return std::to_string(x);
}

// Split comma-separated string
static std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

// ============================================================
// Print design table (mirrors R print.twostage_inr)
// ============================================================
static void print_design_table(const TwoStageDesigns& d) {
    // Collect columns
    struct Col {
        std::string name;
        DesignParams p;
    };
    std::vector<Col> cols;
    cols.push_back({"Optimal",  d.optimal});
    cols.push_back({"Minimax",  d.minimax});
    cols.push_back({"Balanced", d.balanced});
    for (auto& r : d.robust) {
        cols.push_back({r.label, r.params});
    }

    // Row labels
    const char* row_labels[] = {
        "r1", "n1", "PET", "r", "EN", "n", "ratio (n1:n2)",
        "alpha1", "alpha2", "alpha", "beta1", "beta2", "beta"
    };
    const int NROWS = 13;

    auto get_val = [](const DesignParams& p, int row) -> std::string {
        if (!p.valid()) return "NA";
        auto f4 = [](double x) { return fmt4(x); };
        auto f2 = [](double x) { return fmt2(x); };
        switch (row) {
            case 0:  return fmtInt(p.r1);
            case 1:  return fmtInt(p.n1);
            case 2:  return f4(p.pet);
            case 3:  return fmtInt(p.r);
            case 4:  return f2(p.en);
            case 5:  return fmtInt(p.n);
            case 6:  return f4(p.ratio);
            case 7:  return f4(p.alpha1);
            case 8:  return f4(p.alpha2);
            case 9:  return f4(p.alpha);
            case 10: return f4(p.beta1);
            case 11: return f4(p.beta2);
            case 12: return f4(p.beta);
            default: return "NA";
        }
    };

    // Compute column widths
    std::vector<int> col_widths(cols.size());
    for (size_t j = 0; j < cols.size(); ++j) {
        col_widths[j] = (int)cols[j].name.size();
        for (int i = 0; i < NROWS; ++i) {
            int w = (int)get_val(cols[j].p, i).size();
            col_widths[j] = std::max(col_widths[j], w);
        }
    }
    int label_width = 0;
    for (int i = 0; i < NROWS; ++i) {
        label_width = std::max(label_width, (int)std::strlen(row_labels[i]));
    }

    // Print header
    std::cout << "\n Two-Stage Phase II Designs for Tests of One Proportion\n"
              << " Simon and Interval-Null Robust (INR)\n\n";
    std::cout << std::fixed;
    std::cout << " Null response rate (p0): " << fmt2(d.p0) << "\n";
    std::cout << " Interval null [p0L, p0U]: [" << fmt2(d.p0L) << ", " << fmt2(d.p0U) << "]\n";
    std::cout << " Target response rate (p1): " << fmt2(d.p1) << "\n";
    std::cout << " Error rates: alpha = " << fmt2(d.alpha) << "; beta = " << fmt2(d.beta_val) << "\n";
    std::cout << " Maximum allowable sample size: " << d.n_ub << "\n\n";

    // Column headers
    std::cout << std::setw(label_width + 2) << "";
    for (size_t j = 0; j < cols.size(); ++j) {
        std::cout << std::setw(col_widths[j] + 2) << std::right << cols[j].name;
    }
    std::cout << "\n";

    // Data rows
    for (int i = 0; i < NROWS; ++i) {
        std::cout << "  " << std::setw(label_width) << std::left << row_labels[i];
        for (size_t j = 0; j < cols.size(); ++j) {
            std::cout << std::setw(col_widths[j] + 2) << std::right << get_val(cols[j].p, i);
        }
        std::cout << "\n";
    }
    std::cout << "\n Execution time: " << fmt2(d.elapsed_sec) << " seconds\n";
}

// ============================================================
// Print opchar table (mirrors R print.calc_opchar)
// ============================================================
static void print_opchar_table(const OpcharResult& op) {
    std::cout << "\nOperating Characteristics evaluated at:\n\n";
    std::cout << " Null response rate (p0): " << fmt2(op.p0) << "\n";
    bool has_interval = (op.p0L >= 0 && op.p0U >= 0 && op.p0L != op.p0U);
    if (has_interval) {
        std::cout << " Interval null [p0L, p0U]: [" << fmt2(op.p0L) << ", " << fmt2(op.p0U) << "]\n";
    }
    std::cout << " Target response rate (p1): " << fmt2(op.p1) << "\n\n";

    // Header
    std::cout << std::left << std::setw(24) << "Design"
              << std::right
              << std::setw(4)  << "n1"
              << std::setw(4)  << "r1"
              << std::setw(5)  << "n"
              << std::setw(4)  << "r"
              << std::setw(9)  << "alpha_tg"
              << std::setw(10) << "alpha_p0"
              << std::setw(10) << "power_p1"
              << std::setw(9)  << "PET_p0"
              << std::setw(9)  << "EN_p0";
    if (has_interval) {
        std::cout << std::setw(10) << "alpha_p0L"
                  << std::setw(10) << "alpha_p0U"
                  << std::setw(10) << "sup_alpha"
                  << std::setw(9)  << "PET_p0L"
                  << std::setw(9)  << "PET_p0U"
                  << std::setw(9)  << "EN_p0L"
                  << std::setw(9)  << "EN_p0U"
                  << std::setw(9)  << "avg_EN";
    }
    std::cout << "\n";

    for (auto& row : op.rows) {
        if (row.skipped) continue;
        std::cout << std::left << std::setw(24) << row.design_name
                  << std::right
                  << std::setw(4)  << row.n1
                  << std::setw(4)  << row.r1
                  << std::setw(5)  << row.n
                  << std::setw(4)  << row.r
                  << std::setw(9)  << fmt2(row.alpha_target)
                  << std::setw(10) << fmt4(row.alpha_at_p0)
                  << std::setw(10) << fmt4(row.power_at_p1)
                  << std::setw(9)  << fmt4(row.PET_p0)
                  << std::setw(9)  << fmt2(row.EN_p0);
        if (has_interval) {
            std::cout << std::setw(10) << fmt4(row.alpha_at_p0L)
                      << std::setw(10) << fmt4(row.alpha_at_p0U)
                      << std::setw(10) << fmt4(row.sup_alpha)
                      << std::setw(9)  << fmt4(row.PET_p0L)
                      << std::setw(9)  << fmt4(row.PET_p0U)
                      << std::setw(9)  << fmt2(row.EN_p0L)
                      << std::setw(9)  << fmt2(row.EN_p0U)
                      << std::setw(9)  << fmt2(row.avg_EN);
        }
        std::cout << "\n";
    }
}

// ============================================================
// Print summary text (mirrors R summary.twostage_inr)
// ============================================================
static void print_summary(const TwoStageDesigns& d, const std::string& design_name) {
    // Find the design
    const DesignParams* dp = nullptr;
    std::string label;

    if (design_name == "Optimal") { dp = &d.optimal; label = "Simon (Optimal)"; }
    else if (design_name == "Minimax") { dp = &d.minimax; label = "Simon (Minimax)"; }
    else if (design_name == "Balanced") { dp = &d.balanced; label = "Balanced"; }
    else {
        for (auto& r : d.robust) {
            if (r.label == design_name) { dp = &r.params; label = r.label; break; }
        }
    }
    if (!dp || !dp->valid()) {
        std::cerr << "Design '" << design_name << "' not found or invalid.\n";
        return;
    }

    bool is_inr = (label.substr(0, 3) == "INR");
    std::string hyp;
    char buf[512];

    if (is_inr && d.p0L >= 0 && d.p0U >= 0) {
        std::snprintf(buf, sizeof(buf), "H0: P in [%.2f, %.2f] versus H1: P >= %.2f",
                      d.p0L, d.p0U, d.p1);
    } else {
        std::snprintf(buf, sizeof(buf), "H0: P <= %.2f versus H1: P >= %.2f", d.p0, d.p1);
    }
    hyp = buf;

    // EN/PET text
    std::string en_pet;
    if (is_inr && d.p0L >= 0 && d.p0U >= 0) {
        std::snprintf(buf, sizeof(buf),
            " At the upper bound of the null interval (p = %.2f), "
            "the expected sample size for this design is %.2f, "
            "with a probability of stopping after Stage 1 of %.3f.",
            d.p0U, dp->en, dp->pet);
    } else {
        std::snprintf(buf, sizeof(buf),
            " The expected sample size for this design is %.2f, "
            "with a probability of stopping after Stage 1 of %.3f.",
            dp->en, dp->pet);
    }
    en_pet = buf;

    int n2 = dp->n - dp->n1;
    std::printf(
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
        "if more than %d of the %d subjects respond.\n",
        label.c_str(), hyp.c_str(),
        d.alpha, (1.0 - d.beta_val) * 100.0,
        dp->n, dp->n1, n2, en_pet.c_str(),
        dp->r1, dp->r, dp->n);
}

// ============================================================
// JSON output
// ============================================================
static void print_json(const TwoStageDesigns& d) {
    auto dp_json = [](const std::string& name, const DesignParams& p) -> std::string {
        if (!p.valid()) return "";
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "  \"%s\": {\"r1\":%d, \"n1\":%d, \"r\":%d, \"n\":%d, "
            "\"PET\":%s, \"EN\":%s, \"ratio\":%s, "
            "\"alpha1\":%s, \"alpha2\":%s, \"alpha\":%s, "
            "\"beta1\":%s, \"beta2\":%s, \"beta\":%s}",
            name.c_str(), p.r1, p.n1, p.r, p.n,
            fmt4(p.pet).c_str(), fmt4(p.en).c_str(), fmt4(p.ratio).c_str(),
            fmt4(p.alpha1).c_str(), fmt4(p.alpha2).c_str(), fmt4(p.alpha).c_str(),
            fmt4(p.beta1).c_str(), fmt4(p.beta2).c_str(), fmt4(p.beta).c_str());
        return buf;
    };

    std::cout << "{\n";
    std::cout << " \"inputs\": {\"alpha\":" << d.alpha << ", \"power\":" << d.power
              << ", \"p0\":" << d.p0 << ", \"p1\":" << d.p1
              << ", \"p0L\":" << d.p0L << ", \"p0U\":" << d.p0U
              << ", \"n_ub\":" << d.n_ub << "},\n";
    std::cout << " \"elapsed_seconds\": " << d.elapsed_sec << ",\n";
    std::cout << " \"designs\": {\n";

    std::vector<std::string> entries;
    if (d.optimal.valid())  entries.push_back(dp_json("Optimal", d.optimal));
    if (d.minimax.valid())  entries.push_back(dp_json("Minimax", d.minimax));
    if (d.balanced.valid()) entries.push_back(dp_json("Balanced", d.balanced));
    for (auto& r : d.robust) {
        if (r.params.valid()) entries.push_back(dp_json(r.label, r.params));
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        std::cout << entries[i];
        if (i + 1 < entries.size()) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << " }\n}\n";
}

// ============================================================
// CSV output (rejection rate data for plotting)
// ============================================================
static void print_rejection_csv(const TwoStageDesigns& d, int grid_n = 101) {
    struct DesignInfo {
        std::string name;
        const DesignParams* dp;
    };
    std::vector<DesignInfo> designs;
    if (d.optimal.valid())  designs.push_back({"Optimal",  &d.optimal});
    if (d.minimax.valid())  designs.push_back({"Minimax",  &d.minimax});
    if (d.balanced.valid()) designs.push_back({"Balanced", &d.balanced});
    for (auto& r : d.robust) {
        if (r.params.valid()) designs.push_back({r.label, &r.params});
    }

    std::cout << "Design,p,reject_prob\n";
    for (auto& di : designs) {
        for (int i = 0; i < grid_n; ++i) {
            double p = (grid_n == 1) ? d.p0U :
                d.p0L + (d.p0U - d.p0L) * (double)i / (double)(grid_n - 1);
            double rp = pr_reject_two_stage(di.dp->n1, di.dp->r1, di.dp->n, di.dp->r, p);
            std::cout << di.name << "," << p << "," << rp << "\n";
        }
    }
}

// ============================================================
// MAIN
// ============================================================
int main(int argc, char* argv[]) {

    // Defaults
    double alpha = 0.05, power_val = 0.80;
    double p0 = -1, p1 = -1, p0L = -1, p0U = -1;
    int n_lb = -1, n_ub = 150;
    int pgrid_points = -1;
    std::vector<std::string> objectives;
    std::string robust_ref = "Minimax";
    double robust_max_inflation = -1;
    int robust_N_cap = -1;
    std::string format = "text";
    std::string summary_design;
    bool do_opchar = false;
    bool do_rejection_csv = false;
    std::string html_file;
    std::string html_design;
    std::vector<std::string> html_keep;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
            return argv[++i];
        };

        if      (arg == "--alpha")      alpha = std::stod(next());
        else if (arg == "--power")      power_val = std::stod(next());
        else if (arg == "--p0")         p0 = std::stod(next());
        else if (arg == "--p1")         p1 = std::stod(next());
        else if (arg == "--p0L")        p0L = std::stod(next());
        else if (arg == "--p0U")        p0U = std::stod(next());
        else if (arg == "--n_lb")       n_lb = std::stoi(next());
        else if (arg == "--n_ub")       n_ub = std::stoi(next());
        else if (arg == "--pgrid")      pgrid_points = std::stoi(next());
        else if (arg == "--objectives") objectives = split_csv(next());
        else if (arg == "--robust_ref") robust_ref = next();
        else if (arg == "--robust_max_inflation") robust_max_inflation = std::stod(next());
        else if (arg == "--robust_N_cap")         robust_N_cap = std::stoi(next());
        else if (arg == "--format")     format = next();
        else if (arg == "--summary")    summary_design = next();
        else if (arg == "--opchar")     do_opchar = true;
        else if (arg == "--rejection_csv") do_rejection_csv = true;
        else if (arg == "--html")        html_file = next();
        else if (arg == "--html_design") html_design = next();
        else if (arg == "--html_keep")   html_keep = split_csv(next());
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: twostage_inr [options]\n\n"
                << "Required:\n"
                << "  --p0 <float>        Null response probability\n"
                << "  --p1 <float>        Alternative response probability\n\n"
                << "Optional:\n"
                << "  --alpha <float>     Type-I error rate (default: 0.05)\n"
                << "  --power <float>     Desired power (default: 0.80)\n"
                << "  --p0L <float>       Lower bound of interval null\n"
                << "  --p0U <float>       Upper bound of interval null\n"
                << "  --n_lb <int>        Lower bound for N search\n"
                << "  --n_ub <int>        Upper bound for N search (default: 150)\n"
                << "  --pgrid <int>       Grid points for interval null\n"
                << "  --objectives <csv>  Robust objectives (worst_regret,avg_en,...)\n"
                << "  --robust_ref <str>  Reference design: Minimax or Optimal\n"
                << "  --robust_max_inflation <float>\n"
                << "  --robust_N_cap <int>\n\n"
                << "Output:\n"
                << "  --format text|json  Output format (default: text)\n"
                << "  --summary <name>    Print summary for a specific design\n"
                << "  --opchar            Print operating characteristics\n"
                << "  --rejection_csv     Output rejection rate CSV data\n"
                << "  --html <file>       Export full HTML report\n"
                << "  --html_design <n>   Design to feature in HTML report\n"
                << "  --html_keep <csv>   Designs to include (e.g. INR,Optimal,Minimax)\n";
            return 0;
        }
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return 1;
        }
    }

    if (p0 < 0 || p1 < 0) {
        std::cerr << "Error: --p0 and --p1 are required.\n";
        return 1;
    }

    // Run
    auto designs = twostage_inr(
        alpha, power_val, p0, p1,
        n_lb, n_ub, p0L, p0U, pgrid_points,
        objectives, robust_ref,
        robust_max_inflation, robust_N_cap);

    // HTML export
    if (!html_file.empty()) {
        // Default html_design: first robust if available, else Optimal
        if (html_design.empty()) {
            if (!designs.robust.empty()) html_design = designs.robust[0].label;
            else if (designs.optimal.valid()) html_design = "Optimal";
            else if (designs.minimax.valid()) html_design = "Minimax";
        }
        html_export::export_design_html(designs, html_design, html_file, html_keep);
    }

    // Output
    if (format == "json") {
        print_json(designs);
    } else if (do_rejection_csv) {
        print_rejection_csv(designs);
    } else {
        print_design_table(designs);

        if (!summary_design.empty()) {
            std::cout << "\n--- Summary: " << summary_design << " ---\n\n";
            print_summary(designs, summary_design);
        }

        if (do_opchar) {
            auto op = calc_opchar_from_designs(designs);
            print_opchar_table(op);
        }
    }

    return 0;
}