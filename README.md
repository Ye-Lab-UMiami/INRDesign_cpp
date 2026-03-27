# INRDesign (C++ Implementation)

C++ command-line tool for computing Simon and Interval-Null Robust (INR)
two-stage Phase II clinical trial designs.

## Authors

- **Rebecca Irlmeier** — Biostatistics and Bioinformatics Shared Resource, Sylvester Comprehensive Cancer Center; Division of Biostatistics and Bioinformatics, Department of Public Health Sciences, University of Miami, Miami, Florida
- **Zhuoli Jin** — Sylvester Comprehensive Cancer Center; Division of Biostatistics and Bioinformatics, Department of Public Health Sciences, University of Miami, Miami, Florida
- **Fei Ye** — Biostatistics and Bioinformatics Shared Resource, Sylvester Comprehensive Cancer Center; Division of Biostatistics and Bioinformatics, Department of Public Health Sciences, University of Miami, Miami, Florida

## R Package

For the R package version (recommended for most users), see [INRDesign](https://github.com/Ye-Lab-UMiami/INRDesign).
```r
devtools::install_github("Ye-Lab-UMiami/INRDesign")
```

## Build
```bash
make          # optimized build (-O3)
make debug    # debug build with AddressSanitizer
```

Requires C++17. Tested with GCC 13+ and Apple Clang 17+.

## Usage
```bash
# Basic example
./twostage_inr --p0 0.20 --p1 0.35 --p0L 0.18 --p0U 0.22 --n_ub 150

# With summary paragraph for a specific design
./twostage_inr --p0 0.20 --p1 0.35 --p0L 0.18 --p0U 0.22 --n_ub 150 \
    --summary "INR (Worst Regret)"

# With operating characteristics table
./twostage_inr --p0 0.20 --p1 0.35 --p0L 0.18 --p0U 0.22 --n_ub 150 --opchar

# JSON output (for programmatic consumption)
./twostage_inr --p0 0.20 --p1 0.35 --p0L 0.18 --p0U 0.22 --n_ub 150 --format json

# CSV rejection rate data (for plotting in R/Python)
./twostage_inr --p0 0.20 --p1 0.35 --p0L 0.18 --p0U 0.22 --n_ub 150 --rejection_csv

# Full HTML report with tables, plot, and summary
./twostage_inr --p0 0.20 --p1 0.35 --p0L 0.18 --p0U 0.22 --n_ub 150 \
    --html report.html --html_design "INR (Worst Regret)"

# Specify which robust objectives to compute
./twostage_inr --p0 0.10 --p1 0.25 --p0L 0.05 --p0U 0.15 --n_ub 150 \
    --objectives worst_regret,worst_en,avg_en

# All options
./twostage_inr --help
```

## Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--p0` | (required) | Null response probability |
| `--p1` | (required) | Alternative response probability |
| `--alpha` | 0.05 | Type-I error rate |
| `--power` | 0.80 | Desired power (1 - beta) |
| `--p0L` | p0 | Lower bound of interval null |
| `--p0U` | p0 | Upper bound of interval null |
| `--n_lb` | auto | Lower bound for N search |
| `--n_ub` | 150 | Upper bound for N search |
| `--pgrid` | auto | Grid points for interval null evaluation |
| `--objectives` | worst_regret,avg_en | Comma-separated robust objectives |
| `--robust_ref` | Minimax | Reference design for N cap (Minimax or Optimal) |
| `--format` | text | Output format: text or json |
| `--summary` | -- | Print narrative summary for named design |
| `--opchar` | -- | Print operating characteristics table |
| `--rejection_csv` | -- | Output rejection rate CSV for plotting |
| `--html` | -- | Export full HTML report to file |
| `--html_design` | auto | Design to feature in HTML report |

## File Structure

- `stat_math.h` -- Self-contained dbinom/pbinom/qnorm (same algorithms as R's C internals)
- `twostage_core.h` -- Main design search engine with LUT optimization
- `calc_opchar.h` -- Operating characteristics computation
- `export_html.h` -- Full HTML report with styled tables and SVG rejection-rate plot
- `main.cpp` -- CLI interface with text/JSON/CSV/HTML output
- `Makefile` -- Build configuration

## License

MIT
