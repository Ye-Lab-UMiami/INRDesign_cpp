#pragma once
// ============================================================
// stat_math.h  –  Self-contained dbinom / pbinom / qnorm
//
// Implements the exact same algorithms R uses internally
// (Loader's saddle-point dbinom, incomplete-beta pbinom,
//  Beasley-Springer-Moro qnorm).
// ============================================================

#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace stat {

// ------- constants -------
static constexpr double PI      = 3.14159265358979323846;
static constexpr double LN_2PI  = 1.8378770664093454836;  // log(2*pi)
static constexpr double DBL_EPS = std::numeric_limits<double>::epsilon();

// ============================================================
// lgamma – use std::lgamma
// ============================================================

// ============================================================
// Loader's stirlerr  (stirling error)
// stirlerr(n) = log(n!) - (n+0.5)*log(n) + n - 0.5*log(2*pi)
// ============================================================
inline double stirlerr(double n) {
    static const double S0 = 1.0 / 12.0;
    static const double S1 = 1.0 / 360.0;
    static const double S2 = 1.0 / 1260.0;
    static const double S3 = 1.0 / 1680.0;
    static const double S4 = 1.0 / 1188.0;
    static const double sfe[16] = {
        0.0,                          0.08106146679532726,
        0.04134069595540929,          0.02767792568499834,
        0.02079067210376509,          0.01664469118982119,
        0.01387612882307075,          0.01189670994589177,
        0.01042770328857422,          0.009292718528061816,
        0.008380041070032892,         0.007631536785296963,
        0.007006513476086694,         0.006476136264044917,
        0.006019933011690498,         0.005624040488584803
    };
    if (n < 16.0) {
        int nn = (int)n;
        if (n == nn) return sfe[nn];
        return std::lgamma(n + 1.0) - (n + 0.5) * std::log(n) + n - 0.5 * LN_2PI;
    }
    double nn = n * n;
    if (n > 500) return (S0 - S1 / nn) / n;
    if (n > 80)  return (S0 - (S1 - S2 / nn) / nn) / n;
    if (n > 35)  return (S0 - (S1 - (S2 - S3 / nn) / nn) / nn) / n;
    return (S0 - (S1 - (S2 - (S3 - S4 / nn) / nn) / nn) / nn) / n;
}

// ============================================================
// bd0(x, np) = x*log(x/np) + np - x   (deviance term)
// ============================================================
inline double bd0(double x, double np) {
    if (std::fabs(x - np) < 0.1 * (x + np)) {
        double v = (x - np) / (x + np);
        double s = (x - np) * v;
        double ej = 2.0 * x * v;
        v = v * v;
        for (int j = 1; j < 1000; ++j) {
            ej *= v;
            double s1 = s + ej / (2.0 * j + 1.0);
            if (s1 == s) return s1;
            s = s1;
        }
    }
    return x * std::log(x / np) + np - x;
}

// ============================================================
// dbinom(x, n, p)  –  Loader's saddle-point algorithm
// ============================================================
inline double dbinom(int x, int n, double p) {
    if (p < 0 || p > 1 || n < 0) return std::numeric_limits<double>::quiet_NaN();
    if (x < 0 || x > n) return 0.0;
    if (n == 0) return 1.0;
    if (p == 0.0) return (x == 0) ? 1.0 : 0.0;
    if (p == 1.0) return (x == n) ? 1.0 : 0.0;

    double lc;
    if (x == 0) {
        lc = (p < 0.1) ? -bd0((double)n, (double)n * (1.0 - p)) - (double)n * p
                        : (double)n * std::log(1.0 - p);
        return std::exp(lc);
    }
    if (x == n) {
        lc = (p > 0.9) ? -bd0((double)n, (double)n * p) - (double)n * (1.0 - p)
                        : (double)n * std::log(p);
        return std::exp(lc);
    }
    lc = stirlerr((double)n) - stirlerr((double)x) - stirlerr((double)(n - x))
         - bd0((double)x, (double)n * p) - bd0((double)(n - x), (double)n * (1.0 - p));
    return std::exp(lc) * std::sqrt((double)n / (2.0 * PI * (double)x * (double)(n - x)));
}

// ============================================================
// pbinom(q, n, p)  –  CDF via regularised incomplete beta
// ============================================================

// --- log-beta function ---
inline double lbeta_fn(double a, double b) {
    return std::lgamma(a) + std::lgamma(b) - std::lgamma(a + b);
}

// --- continued-fraction for incomplete beta ---
static double betacf(double a, double b, double x) {
    constexpr int MAXIT = 200;
    constexpr double EPS = 3.0e-12;
    constexpr double FPMIN = 1.0e-30;
    double qab = a + b, qap = a + 1.0, qam = a - 1.0;
    double c = 1.0, d = 1.0 - qab * x / qap;
    if (std::fabs(d) < FPMIN) d = FPMIN;
    d = 1.0 / d;
    double h = d;
    for (int m = 1; m <= MAXIT; ++m) {
        int m2 = 2 * m;
        double aa = (double)m * (b - (double)m) * x / ((qam + (double)m2) * (a + (double)m2));
        d = 1.0 + aa * d; if (std::fabs(d) < FPMIN) d = FPMIN;
        c = 1.0 + aa / c; if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d; h *= d * c;
        aa = -(a + (double)m) * (qab + (double)m) * x / ((a + (double)m2) * (qap + (double)m2));
        d = 1.0 + aa * d; if (std::fabs(d) < FPMIN) d = FPMIN;
        c = 1.0 + aa / c; if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d;
        double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < EPS) break;
    }
    return h;
}

// --- regularised incomplete beta  I_x(a,b) ---
inline double betai(double a, double b, double x) {
    if (x < 0.0 || x > 1.0) return std::numeric_limits<double>::quiet_NaN();
    if (x == 0.0 || x == 1.0) return x;
    double bt = std::exp(a * std::log(x) + b * std::log(1.0 - x) - lbeta_fn(a, b));
    if (x < (a + 1.0) / (a + b + 2.0))
        return bt * betacf(a, b, x) / a;
    else
        return 1.0 - bt * betacf(b, a, 1.0 - x) / b;
}

// --- pbinom ---
inline double pbinom(int q, int n, double p, bool lower_tail = true) {
    if (p < 0 || p > 1 || n < 0) return std::numeric_limits<double>::quiet_NaN();
    if (q < 0)  return lower_tail ? 0.0 : 1.0;
    if (q >= n) return lower_tail ? 1.0 : 0.0;
    // P(X <= q) = I_{1-p}(n-q, q+1)   (regularised incomplete beta)
    double val = betai((double)(n - q), (double)(q + 1), 1.0 - p);
    return lower_tail ? val : 1.0 - val;
}

// ============================================================
// qnorm(p)  –  Beasley-Springer-Moro rational approximation
// ============================================================
inline double qnorm(double p) {
    if (p <= 0.0) return -std::numeric_limits<double>::infinity();
    if (p >= 1.0) return  std::numeric_limits<double>::infinity();
    if (p == 0.5) return 0.0;

    // Coefficients for rational approximation
    static const double a[6] = {
        -3.969683028665376e+01,  2.209460984245205e+02,
        -2.759285104469687e+02,  1.383577518672690e+02,
        -3.066479806614716e+01,  2.506628277459239e+00
    };
    static const double b[5] = {
        -5.447609879822406e+01,  1.615858368580409e+02,
        -1.556989798598866e+02,  6.680131188771972e+01,
        -1.328068155288572e+01
    };
    static const double c[6] = {
        -7.784894002430293e-03, -3.223964580411365e-01,
        -2.400758277161838e+00, -2.549732539343734e+00,
         4.374664141464968e+00,  2.938163982698783e+00
    };
    static const double d[4] = {
         7.784695709041462e-03,  3.224671290700398e-01,
         2.445134137142996e+00,  3.754408661907416e+00
    };

    constexpr double p_low  = 0.02425;
    constexpr double p_high = 1.0 - 0.02425;
    double q, r;

    if (p < p_low) {
        q = std::sqrt(-2.0 * std::log(p));
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    } else if (p <= p_high) {
        q = p - 0.5;
        r = q * q;
        return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
               (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
    } else {
        q = std::sqrt(-2.0 * std::log(1.0 - p));
        return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                 ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
}

} // namespace stat
