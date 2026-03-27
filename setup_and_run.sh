#!/bin/bash
# ============================================================
# setup_and_run.sh
#
# Build the C++ binary and run examples that mirror examples.R
# Place in your "Cpp version" folder and run: bash setup_and_run.sh
# ============================================================

set -e
cd "$(dirname "$0")"

echo "============================================"
echo " Two-Stage INR Design (C++ version)"
echo "============================================"
echo ""

# ------ Step 1: Check compiler ------
if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
    echo "ERROR: No C++ compiler found."
    echo ""
    echo "  Install Xcode Command Line Tools first:"
    echo "    xcode-select --install"
    echo ""
    echo "  Then re-run this script."
    exit 1
fi
echo "[OK] Compiler found"

# ------ Step 2: Build ------
echo "[..] Compiling..."
make clean 2>/dev/null || true
make
echo "[OK] Build successful"
echo ""

# =====================================================
# 1. CREATE DESIGNS (mirrors examples.R section 1)
# =====================================================

echo "============================================"
echo " 1. Design search (p0=0.20, p1=0.35,"
echo "    p0L=0.18, p0U=0.22, n.ub=150)"
echo "============================================"
./twostage_inr \
    --p0 0.20 --p1 0.35 \
    --p0L 0.18 --p0U 0.22 \
    --n_ub 150

echo ""
echo "============================================"
echo " 2. Design search with n.ub=100"
echo "============================================"
./twostage_inr \
    --p0 0.20 --p1 0.35 \
    --p0L 0.18 --p0U 0.22 \
    --n_ub 100

echo ""
echo "============================================"
echo " 3. Point-null only (no interval null)"
echo "============================================"
./twostage_inr \
    --p0 0.20 --p1 0.35 \
    --n_ub 150

# =====================================================
# 4. SUMMARY (mirrors examples.R summary calls)
# =====================================================

echo ""
echo "============================================"
echo " 4. Summary for Optimal design"
echo "============================================"
./twostage_inr \
    --p0 0.20 --p1 0.35 \
    --p0L 0.18 --p0U 0.22 \
    --n_ub 150 \
    --summary "Optimal" 2>/dev/null | grep -A999 "^--- Summary"

echo ""
echo "============================================"
echo " 5. Summary for INR (Worst Regret)"
echo "============================================"
./twostage_inr \
    --p0 0.20 --p1 0.35 \
    --p0L 0.18 --p0U 0.22 \
    --n_ub 150 \
    --summary "INR (Worst Regret)" 2>/dev/null | grep -A999 "^--- Summary"

# =====================================================
# 6. OPERATING CHARACTERISTICS (mirrors examples.R section 4)
# =====================================================

echo ""
echo "============================================"
echo " 6. Operating characteristics"
echo "============================================"
./twostage_inr \
    --p0 0.20 --p1 0.35 \
    --p0L 0.18 --p0U 0.22 \
    --n_ub 150 \
    --opchar 2>/dev/null | grep -A999 "^Operating"

# =====================================================
# 7. JSON OUTPUT (for programmatic use)
# =====================================================

echo ""
echo "============================================"
echo " 7. JSON output"
echo "============================================"
./twostage_inr \
    --p0 0.20 --p1 0.35 \
    --p0L 0.18 --p0U 0.22 \
    --n_ub 150 \
    --format json 2>/dev/null

# =====================================================
# 8. HTML REPORTS (mirrors examples.R section 11)
# =====================================================

echo ""
echo "============================================"
echo " 8a. HTML report: INR (Worst Regret)"
echo "     All designs included"
echo "============================================"
# R: export_design_html(design, design_name="INR (Worst Regret)",
#                       file="inr_worst_regret.html")
./twostage_inr \
    --p0 0.20 --p1 0.35 \
    --p0L 0.18 --p0U 0.22 \
    --n_ub 150 \
    --html inr_worst_regret.html \
    --html_design "INR (Worst Regret)" \
    2>/dev/null
echo "[OK] Saved: inr_worst_regret.html"

echo ""
echo "============================================"
echo " 8b. HTML report: INR (Average EN)"
echo "     Only INR + Optimal + Minimax"
echo "============================================"
# R: export_design_html(design, design_name="INR (Average EN)",
#                       designs_keep=c("INR","Optimal","Minimax"),
#                       file="inr_avg_en.html")
./twostage_inr \
    --p0 0.20 --p1 0.35 \
    --p0L 0.18 --p0U 0.22 \
    --n_ub 150 \
    --html inr_avg_en.html \
    --html_design "INR (Average EN)" \
    --html_keep "INR,Optimal,Minimax" \
    2>/dev/null
echo "[OK] Saved: inr_avg_en.html"

echo ""
echo "============================================"
echo " 8c. HTML report: Optimal only"
echo "============================================"
./twostage_inr \
    --p0 0.20 --p1 0.35 \
    --p0L 0.18 --p0U 0.22 \
    --n_ub 150 \
    --html optimal_report.html \
    --html_design "Optimal" \
    --html_keep "Optimal" \
    2>/dev/null
echo "[OK] Saved: optimal_report.html"

# =====================================================
# 9. DIFFERENT PARAMETER SET (p0=0.10, p1=0.25)
# =====================================================

echo ""
echo "============================================"
echo " 9. Different parameters (p0=0.10, p1=0.25)"
echo "============================================"
./twostage_inr \
    --alpha 0.05 --power 0.80 \
    --p0 0.10 --p1 0.25 \
    --p0L 0.05 --p0U 0.15 \
    --n_ub 150 \
    --objectives worst_regret,avg_en \
    --html inr_p010_report.html \
    --html_design "INR (Worst Regret)"
echo "[OK] Saved: inr_p010_report.html"

# =====================================================
# 10. CSV REJECTION RATE DATA (for plotting in R/Python)
# =====================================================

echo ""
echo "============================================"
echo " 10. Rejection rate CSV (first 10 lines)"
echo "============================================"
./twostage_inr \
    --p0 0.20 --p1 0.35 \
    --p0L 0.18 --p0U 0.22 \
    --n_ub 150 \
    --rejection_csv 2>/dev/null | head -10

# =====================================================
# Done
# =====================================================

echo ""
echo "============================================"
echo " Done! Generated HTML reports:"
echo "   - inr_worst_regret.html"
echo "   - inr_avg_en.html"
echo "   - optimal_report.html"
echo "   - inr_p010_report.html"
echo "============================================"

# Auto-open reports on macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
    open inr_worst_regret.html 2>/dev/null || true
fi