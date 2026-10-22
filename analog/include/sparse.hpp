#pragma once
#include "analog.hpp"
#include "sparse_rank.hpp"
#if __has_include(<Eigen/SparseQR>)
#include <Eigen/SparseQR>
#include <Eigen/SparseLU>
#elif __has_include(<eigen3/Eigen/SparseQR>)
#include <eigen3/Eigen/SparseQR>
#include <eigen3/Eigen/SparseLU>
#else
#error "The optional analog sparse backend requires Eigen 3 headers"
#endif
#include <memory>

namespace analog {
enum class sparse_strategy { rank_revealing_qr, newton_directions, certified_lu };
namespace detail {
class sparse_linear_solver {
    using matrix = Eigen::SparseMatrix<double>;
    Eigen::SparseQR<matrix, Eigen::COLAMDOrdering<int>> qr;
    Eigen::SparseLU<matrix, Eigen::COLAMDOrdering<int>> lu;
    std::vector<int> previous_outer, previous_inner;
    std::vector<int> lu_outer, lu_inner;
    std::vector<double> qr_values, lu_values;
    bool newton_directions;
    bool rank_certificates;
    bool lu_rank_certified = false;
public:
    explicit sparse_linear_solver(bool directions = false, bool certificates = false)
        : newton_directions(directions), rank_certificates(certificates) {}
    std::vector<double> operator()(std::size_t dimension, const std::vector<matrix_entry>& entries,
                                   const std::vector<double>& right) {
        if (dimension != right.size() || dimension > std::size_t(std::numeric_limits<int>::max()))
            throw diagnostic("E_LINEAR_SOLVER", "invalid sparse linear system size");
        if (dimension == 0 && entries.empty())
            return {};
        std::vector<Eigen::Triplet<double>> stamps;
        stamps.reserve(entries.size());
        for (const auto& e : entries) {
            if (e.row >= dimension || e.column >= dimension || !std::isfinite(e.value))
                throw diagnostic("E_LINEAR_SOLVER", "invalid sparse matrix coefficient");
            stamps.emplace_back(int(e.row), int(e.column), e.value);
        }
        matrix a{int(dimension), int(dimension)};
        a.setFromTriplets(stamps.begin(), stamps.end());
        a.prune(0.0);
        a.makeCompressed();
        std::vector<double> scale(dimension);
        for (int column = 0; column < a.outerSize(); ++column)
            for (matrix::InnerIterator i(a, column); i; ++i)
                scale[i.row()] = std::max(scale[i.row()], std::abs(i.value()));
        Eigen::VectorXd b{int(dimension)};
        for (std::size_t row = 0; row < dimension; ++row) {
            if (scale[row] == 0)
                throw diagnostic("E_SINGULAR", "zero sparse equation row; inspect floating wires and ideal constraints");
            if (!std::isfinite(right[row]))
                throw diagnostic("E_LINEAR_SOLVER", "nonfinite sparse right-hand side");
            b[int(row)] = right[row] / scale[row];
        }
        for (int column = 0; column < a.outerSize(); ++column)
            for (matrix::InnerIterator i(a, column); i; ++i)
                i.valueRef() /= scale[i.row()];
        // Charge-current unknowns and node voltages have different units.
        // Equilibrate columns too, retaining the original row-scaled system
        // for the residual check after recovering the physical unknowns.
        const matrix original = a;
        const Eigen::VectorXd original_b = b;
        Eigen::VectorXd columns = Eigen::VectorXd::Ones(int(dimension));
        Eigen::VectorXd rows = Eigen::VectorXd::Ones(int(dimension));
        for (unsigned iteration = 0; iteration < 12; ++iteration) {
            Eigen::VectorXd norms = Eigen::VectorXd::Zero(int(dimension));
            for (int column = 0; column < a.outerSize(); ++column)
                for (matrix::InnerIterator i(a, column); i; ++i)
                    norms[i.row()] = std::max(norms[i.row()], std::abs(i.value()));
            norms = norms.cwiseSqrt();
            rows = rows.cwiseProduct(norms);
            b = b.cwiseQuotient(norms);
            for (int column = 0; column < a.outerSize(); ++column) {
                double norm = 0;
                for (matrix::InnerIterator i(a, column); i; ++i) {
                    i.valueRef() /= norms[i.row()];
                    norm = std::max(norm, std::abs(i.value()));
                }
                if (norm == 0)
                    throw diagnostic("E_SINGULAR", "zero sparse equation column");
                norm = std::sqrt(norm);
                columns[column] *= norm;
                for (matrix::InnerIterator i(a, column); i; ++i)
                    i.valueRef() /= norm;
            }
        }
        const std::vector<int> outer(a.outerIndexPtr(), a.outerIndexPtr() + a.outerSize() + 1);
        const std::vector<int> inner(a.innerIndexPtr(), a.innerIndexPtr() + a.nonZeros());
        const std::vector<double> values(a.valuePtr(), a.valuePtr() + a.nonZeros());
        auto rank_factorization = [&] {
            if (outer != previous_outer || inner != previous_inner) {
                qr.analyzePattern(a);
                previous_outer = outer;
                previous_inner = inner;
                qr_values.clear();
            }
            if (values != qr_values) {
                // A failed factorization invalidates the prior numeric cache.
                qr_values.clear();
                qr.setPivotThreshold(1e-12);
                qr.factorize(a);
                if (qr.info() != Eigen::Success || qr.rank() != int(dimension))
                    throw diagnostic("E_SINGULAR", "rank-deficient sparse equation Jacobian");
                qr_values = values;
            }
        };
        const bool accepted_state = !std::any_of(right.begin(), right.end(),
            [](double value) { return std::abs(value) > newton_residual_tolerance; });
        // The engine checks rank before accepting even a zero-residual state.
        // The certified strategy may establish that rank from LU; uncertain
        // bounds fall back to the same QR pivot threshold. All directions
        // still pass the independent original-system residual check below.
        bool use_lu = newton_directions && (rank_certificates || !accepted_state);
        if (use_lu) {
            if (outer != lu_outer || inner != lu_inner) {
                lu.analyzePattern(a);
                lu_outer = outer;
                lu_inner = inner;
                lu_values.clear();
            }
            if (values != lu_values) {
                lu_values.clear();
                lu_rank_certified = false;
                lu.factorize(a);
                if (lu.info() != Eigen::Success)
                    use_lu = false;
                else
                    lu_values = values;
            }
        }
        if (use_lu && rank_certificates && accepted_state && !lu_rank_certified) {
            lu_rank_certified = certify_sparse_lu_rank(a, lu, 1e-12).full_rank;
            if (!lu_rank_certified)
                use_lu = false;
        }
        Eigen::VectorXd solution;
        const double limit = 1e-8 * std::max(1.0, original_b.template lpNorm<Eigen::Infinity>());
        double residual = std::numeric_limits<double>::infinity();
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            if (!use_lu)
                rank_factorization();
            auto solve = [&](const Eigen::VectorXd& rhs) -> Eigen::VectorXd {
                return use_lu ? Eigen::VectorXd(lu.solve(rhs)) : Eigen::VectorXd(qr.solve(rhs));
            };
            solution = solve(b).cwiseQuotient(columns);
            Eigen::VectorXd remainder = original_b - original * solution;
            for (unsigned iteration = 0; iteration < 3 && solution.allFinite(); ++iteration) {
                const Eigen::VectorXd correction = solve(remainder.cwiseQuotient(rows));
                solution += correction.cwiseQuotient(columns);
                remainder = original_b - original * solution;
            }
            residual = remainder.template lpNorm<Eigen::Infinity>();
            if (solution.allFinite() && std::isfinite(residual) && residual <= limit)
                break;
            if (!use_lu)
                break;
            use_lu = false; // Retry with QR before rejecting the direction.
        }
        if (!solution.allFinite() || !std::isfinite(residual))
            throw diagnostic("E_LINEAR_SOLVER", "sparse linear solution failed its independent residual check");
        if (residual > limit)
            throw diagnostic("E_LINEAR_ACCURACY", "sparse linear solution failed its independent residual check");
        return {solution.data(), solution.data() + solution.size()};
    }
};
} // namespace detail

// Numerical configuration only: all device laws, topology, charge and Newton
// acceptance criteria remain in the common integration engine.
inline void use_sparse_solver(simulation& sim, sparse_strategy strategy = sparse_strategy::rank_revealing_qr) {
    auto backend = std::make_shared<detail::sparse_linear_solver>(strategy != sparse_strategy::rank_revealing_qr,
                                                               strategy == sparse_strategy::certified_lu);
    sim.linear_backend = [backend](std::size_t dimension, const std::vector<matrix_entry>& coefficients,
                                  const std::vector<double>& right) {
        return (*backend)(dimension, coefficients, right);
    };
}
} // namespace analog
