#pragma once
#if __has_include(<Eigen/SparseLU>)
#include <Eigen/SparseLU>
#elif __has_include(<eigen3/Eigen/SparseLU>)
#include <eigen3/Eigen/SparseLU>
#else
#error "The optional sparse rank certificate requires Eigen 3 headers"
#endif
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace analog::detail {

struct sparse_rank_certificate {
    bool full_rank = false;
    double minimum_singular_value_bound = 0;
    double factorization_error_bound = 0;
};

// A sufficient rank test, not a singularity test. Uncertain bounds require
// the existing rank-revealing QR. The matrix and its LU must have matching
// dimensions; their actual correspondence is checked through A - P' L U Q'.
template <class Matrix, class LU>
sparse_rank_certificate certify_sparse_lu_rank(const Matrix& a, const LU& lu, double threshold) {
    using matrix = Eigen::SparseMatrix<double>;
    using row_matrix = Eigen::SparseMatrix<double, Eigen::RowMajor>;
    const int n = a.rows();
    if (n <= 0 || a.cols() != n || lu.rows() != n || lu.cols() != n
        || lu.info() != Eigen::Success || !std::isfinite(threshold) || threshold <= 0)
        return {};
    // Eigen stores U's diagonal supernodes with L and its remaining entries
    // separately. Read both public factor views; preserve permutations below.
    const auto lower = lu.matrixL();
    const auto upper = lu.matrixU();
    using lower_map = std::decay_t<decltype(lower.m_mapL)>;
    using upper_map = std::decay_t<decltype(upper.m_mapU)>;
    std::vector<Eigen::Triplet<double>> lower_entries, upper_entries;
    for (int column = 0; column < n; ++column) {
        lower_entries.emplace_back(column, column, 1.0);
        for (typename lower_map::InnerIterator entry(lower.m_mapL, column); entry; ++entry)
            (entry.row() > column ? lower_entries : upper_entries)
                .emplace_back(entry.row(), column, entry.value());
        for (typename upper_map::InnerIterator entry(upper.m_mapU, column); entry; ++entry)
            upper_entries.emplace_back(entry.row(), column, entry.value());
    }
    matrix l(n, n), u(n, n);
    l.setFromTriplets(lower_entries.begin(), lower_entries.end());
    u.setFromTriplets(upper_entries.begin(), upper_entries.end());
    const row_matrix lr(l), ur(u);
    Eigen::VectorXd z = Eigen::VectorXd::Ones(n), w = z, p = z, q = z;

    // Absolute triangular recurrences bound |U^-1| |L^-1| 1 and its
    // transpose without constructing an inverse or assuming cancellation.
    for (int i = 0; i < n; ++i)
        for (typename row_matrix::InnerIterator entry(lr, i); entry; ++entry)
            if (entry.col() < i)
                z[i] += std::abs(entry.value()) * z[entry.col()];
    for (int i = n - 1; i >= 0; --i) {
        w[i] = z[i];
        for (typename row_matrix::InnerIterator entry(ur, i); entry; ++entry)
            if (entry.col() > i)
                w[i] += std::abs(entry.value()) * w[entry.col()];
        w[i] /= std::abs(u.coeff(i, i));
    }
    for (int i = 0; i < n; ++i) {
        for (typename matrix::InnerIterator entry(u, i); entry; ++entry)
            if (entry.row() < i)
                p[i] += std::abs(entry.value()) * p[entry.row()];
        p[i] /= std::abs(u.coeff(i, i));
    }
    for (int i = n - 1; i >= 0; --i) {
        q[i] = p[i];
        for (typename matrix::InnerIterator entry(l, i); entry; ++entry)
            if (entry.row() > i)
                q[i] += std::abs(entry.value()) * q[entry.row()];
    }
    const double epsilon = std::numeric_limits<double>::epsilon();
    const double recurrence_rounding = 64 * epsilon * double(n) * double(n);
    if (recurrence_rounding >= .01 || !w.allFinite() || !q.allFinite())
        return {};
    const double inverse_infinity = w.maxCoeff() / (1 - recurrence_rounding);
    const double inverse_one = q.maxCoeff() / (1 - recurrence_rounding);
    if (!std::isfinite(inverse_infinity) || !std::isfinite(inverse_one)
        || inverse_infinity <= 0 || inverse_one <= 0)
        return {};

    auto norms = [](const matrix& value) {
        Eigen::VectorXd rows = Eigen::VectorXd::Zero(value.rows());
        double one = 0;
        for (int column = 0; column < value.outerSize(); ++column) {
            double sum = 0;
            for (typename matrix::InnerIterator entry(value, column); entry; ++entry) {
                const double absolute = std::abs(entry.value());
                if (!std::isfinite(absolute))
                    return std::pair<double, double>{std::numeric_limits<double>::infinity(),
                                                     std::numeric_limits<double>::infinity()};
                rows[entry.row()] += absolute;
                sum += absolute;
            }
            one = std::max(one, sum);
        }
        return std::pair<double, double>{one, rows.maxCoeff()};
    };
    const matrix permuted = lu.rowsPermutation() * a * lu.colsPermutation().inverse();
    const matrix product = l * u;
    const matrix error = permuted - product;
    const matrix absolute_product = l.cwiseAbs() * u.cwiseAbs();
    const auto error_norm = norms(error), product_norm = norms(absolute_product), input_norm = norms(a);
    const double rounding = 64 * epsilon * n;
    const double error_one = error_norm.first + rounding * (product_norm.first + input_norm.first);
    const double error_infinity = error_norm.second + rounding * (product_norm.second + input_norm.second);
    const double error_bound = std::sqrt(error_one) * std::sqrt(error_infinity);
    const double lower_bound = (1 / std::sqrt(inverse_infinity)) / std::sqrt(inverse_one) - error_bound;
    // ||A^-1||2 <= sqrt(||A^-1||1 ||A^-1||inf), followed by the
    // singular-value perturbation bound for the measured LU residual.
    // Every QR diagonal exceeds sigma_min(A). A factor-two margin keeps
    // this sufficient test away from the QR pivot decision boundary.
    return {std::isfinite(lower_bound) && std::isfinite(error_bound) && lower_bound > 2 * threshold,
            lower_bound, error_bound};
}
} // namespace analog::detail
