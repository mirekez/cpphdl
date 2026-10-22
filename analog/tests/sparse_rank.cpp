#include <sparse_rank.hpp>
#include <Eigen/Dense>
#include <iostream>
#include <random>
#include <stdexcept>

using matrix = Eigen::SparseMatrix<double>;
using solver = Eigen::SparseLU<matrix, Eigen::COLAMDOrdering<int>>;

static void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

int main() {
    std::mt19937 generator(73945);
    std::uniform_real_distribution<double> random(-1, 1);
    unsigned certified = 0, inconclusive = 0;
    // Dense SVD is an independent reference. Permutations, indefinite and
    // nonsymmetric matrices exercise the factor views and both inverse norms.
    for (unsigned trial = 0; trial < 240; ++trial) {
        const int n = 1 + trial % 20;
        Eigen::MatrixXd dense(n, n);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                dense(i, j) = random(generator) * (trial % 3 == 0 ? .1 : 1);
        dense.diagonal().array() += trial % 3 == 0 ? 3.0 : .2;
        if (trial % 4 == 0)
            dense.col(n - 1) *= 1e-14;
        if (trial % 7 == 0)
            dense.row(0) *= 1e6;
        if (n > 1 && trial % 5 == 0)
            dense.row(0).swap(dense.row(n - 1));
        matrix a = dense.sparseView();
        solver lu;
        lu.compute(a);
        if (lu.info() != Eigen::Success)
            continue;
        const auto result = analog::detail::certify_sparse_lu_rank(a, lu, 1e-12);
        Eigen::JacobiSVD<Eigen::MatrixXd> svd(dense);
        const double sigma = svd.singularValues().tail(1)[0];
        if (result.full_rank) {
            ++certified;
            require(sigma > 1e-12, "certificate accepted an independently rank-deficient matrix");
            require(result.minimum_singular_value_bound <= sigma * (1 + 1e-8),
                    "certificate is not a lower singular-value bound");
        } else {
            ++inconclusive;
        }
    }
    require(certified > 100 && inconclusive > 30, "rank reference did not exercise both paths");

    Eigen::MatrixXd diagonal = Eigen::MatrixXd::Identity(4, 4);
    diagonal(3, 3) = 1e-14;
    matrix near = diagonal.sparseView();
    solver near_lu;
    near_lu.compute(near);
    require(near_lu.info() == Eigen::Success, "near-singular LU fixture failed unexpectedly");
    require(!analog::detail::certify_sparse_lu_rank(near, near_lu, 1e-12).full_rank,
            "nonzero pivots alone were mistaken for adequate rank");

    matrix identity(4, 4);
    identity.setIdentity();
    solver identity_lu;
    identity_lu.compute(identity);
    require(analog::detail::certify_sparse_lu_rank(identity, identity_lu, 1e-12).full_rank,
            "well-conditioned factorization was not certified");
    matrix different = 3 * identity;
    require(!analog::detail::certify_sparse_lu_rank(different, identity_lu, 1e-12).full_rank,
            "a factorization of another matrix passed its residual certificate");
    require(!analog::detail::certify_sparse_lu_rank(identity, identity_lu, 0).full_rank,
            "invalid rank threshold was accepted");

    // Large triangular growth makes the bound uninformative. It must request
    // the fallback, rather than replacing uncertainty with a rank assertion.
    Eigen::MatrixXd growing = Eigen::MatrixXd::Identity(20, 20);
    for (int i = 0; i < 19; ++i)
        growing(i, i + 1) = 1e20;
    matrix growth = growing.sparseView();
    solver growth_lu;
    growth_lu.compute(growth);
    require(!analog::detail::certify_sparse_lu_rank(growth, growth_lu, 1e-12).full_rank,
            "overflowing inverse bound did not request QR");
    std::cout << "PASS " << certified << " SVD-checked certificates, " << inconclusive
              << " inconclusive bounds, tiny pivots, permutations, stale factors and overflow\n";
}
