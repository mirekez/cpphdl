#include <sparse.hpp>
#include <chrono>
#include <iostream>
#include <random>
#include <fstream>

using namespace analog;

static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

static double rc_array(unsigned count, bool sparse, bool analytic, unsigned refinement = 1,
                       bool directions = false, bool certificates = false) {
    simulation sim;
    sim.analytic_jacobian = analytic;
    if (sparse)
        use_sparse_solver(sim, certificates ? sparse_strategy::certified_lu
            : directions ? sparse_strategy::newton_directions : sparse_strategy::rank_revealing_qr);
    electrical source, rail, ground;
    sim.reference(ground);
    source = sim_voltage_source(ground, voltage(1.2));
    rail = resistor(source, resistance(100));
    std::vector<electrical> branches(count);
    for (auto& node : branches) {
        node = resistor(rail, resistance(1000));
        node = capacitor(ground, capacitance_value(1e-12));
        sim.observe("branch", node, ground);
    }
    // Symmetry reduces this coupled network to an independently known pole.
    // The finite common supply resistance slows it as more branches connect.
    const double tau = (1000 + 100 * count) * 1e-12;
    sim.run(duration(5 * tau), duration(tau / (512 * refinement)));
    double worst = 0;
    for (const auto& row : sim.trace) {
        const double expected = 1.2 * (1 - std::exp(-row.time / tau));
        for (const auto& value : row.values) {
            require(value.has_value(), "sparse array lost an observation");
            worst = std::max(worst, std::abs(*value - expected));
        }
    }
    require(worst < .0005, "coupled RC array missed its independent continuous-time pole");
    return worst;
}

static std::vector<sample> nonlinear_array(unsigned count, bool sparse, bool analytic,
                                         bool directions = false, bool certificates = false) {
    simulation sim;
    sim.analytic_jacobian = analytic;
    if (sparse)
        use_sparse_solver(sim, certificates ? sparse_strategy::certified_lu
            : directions ? sparse_strategy::newton_directions : sparse_strategy::rank_revealing_qr);
    electrical source, rail, ground;
    sim.reference(ground);
    source = sim_voltage_source(ground, voltage(1.2));
    rail = resistor(source, resistance(50));
    std::vector<electrical> nodes(count);
    const mos_model channel{channel_strength(.01), voltage(.35)};
    for (unsigned i = 0; i < count; ++i) {
        nodes[i] = resistor(rail, resistance(1000));
        ground = nmos(nodes[i], nodes[i], channel);
        nodes[i] = capacitor(ground, capacitance_value(.05e-12));
        if (i)
            nodes[i] = capacitor(nodes[i - 1], capacitance_value(.02e-12));
        sim.observe("diode-connected MOS", nodes[i], ground);
    }
    sim.run(duration(3e-9), duration(2e-12));
    const double k = .005 * (1000 + 50 * count);
    const double expected = .35 + (std::sqrt(1 + 4 * k * (1.2 - .35)) - 1) / (2 * k);
    for (const auto& value : sim.trace.back().values)
        require(value && std::abs(*value - expected) < 1e-7, "loaded MOS array missed its independent DC operating point");
    return sim.trace;
}

int main() {
    try {
        const auto start = std::chrono::steady_clock::now();
        detail::sparse_linear_solver sparse;
        detail::sparse_linear_solver directions(true);
        detail::sparse_linear_solver certified(true, true);
        // This real four-slice ADC Newton system exposed the need to reject
        // an inaccurate direction and retry a smaller transient interval.
        // A future backend may solve it accurately; accepting an inaccurate
        // direction or misclassifying it as structural rank loss is forbidden.
        {
            std::ifstream file(std::filesystem::path(__FILE__).parent_path() / "fixtures/adc_feedback_linear.txt");
            std::size_t size = 0, count = 0;
            file >> size >> count;
            require(size == 349 && count == 1478, "missing/corrupt captured ADC equation fixture");
            std::vector<double> right(size), scale(size);
            std::vector<matrix_entry> entries(count);
            std::vector<std::vector<double>> matrix(size, std::vector<double>(size));
            for (auto& value : right)
                file >> value;
            for (auto& entry : entries) {
                file >> entry.row >> entry.column >> entry.value;
                require(entry.row < size && entry.column < size, "invalid captured equation index");
                matrix[entry.row][entry.column] += entry.value;
            }
            require(bool(file), "incomplete captured ADC equation fixture");
            try {
                auto solution = sparse(size, entries, right);
                for (unsigned row = 0; row < size; ++row) {
                    double residual = -right[row];
                    for (unsigned column = 0; column < size; ++column) {
                        scale[row] = std::max(scale[row], std::abs(matrix[row][column]));
                        residual += matrix[row][column] * solution[column];
                    }
                    require(std::abs(residual) / scale[row] <= 1e-8,
                            "captured ADC direction violated the independent original-equation residual bound");
                }
            } catch (const diagnostic& error) {
                require(error.code == "E_LINEAR_ACCURACY", "ill-conditioned full-rank ADC system was misclassified");
            }
        }
        std::mt19937 random(0x125ADC);
        for (unsigned size : {3u, 11u, 37u}) {
            std::vector<std::vector<double>> a(size, std::vector<double>(size));
            std::vector<double> known(size), b(size);
            for (unsigned i = 0; i < size; ++i) {
                known[i] = std::sin(i + .3);
                for (unsigned j = 0; j < size; ++j)
                    if (random() % 5 == 0)
                        a[i][j] = double(int(random() % 19) - 9) / 16;
                double magnitude = 1;
                for (double x : a[i])
                    magnitude += std::abs(x);
                a[i][i] += magnitude;
            }
            std::vector<matrix_entry> entries;
            for (unsigned i = 0; i < size; ++i) {
                const double scale = std::pow(10., int(i % 13) - 6);
                for (unsigned j = 0; j < size; ++j) {
                    b[i] += a[i][j] * known[j] * scale;
                    a[i][j] *= scale;
                    if (a[i][j] != 0)
                        entries.push_back({i, j, a[i][j]});
                }
            }
            const auto x = sparse(size, entries, b), fast = directions(size, entries, b), dense = linear_solve(a, b);
            for (unsigned i = 0; i < size; ++i)
                require(std::abs(x[i] - known[i]) < 1e-10 && std::abs(x[i] - dense[i]) < 1e-10,
                        "row-scaled sparse linear solve missed the independent known solution");
            for (unsigned i = 0; i < size; ++i)
                require(std::abs(fast[i] - known[i]) < 1e-10, "LU/QR direction solve missed the known solution");
        }
        // Mixed voltage/charge units can scale columns as well as equations.
        // The independently known, scaled solution must survive both changes.
        for (unsigned size : {7u, 23u}) {
            std::vector<matrix_entry> entries;
            std::vector<double> b(size), expected(size), columns(size);
            for (unsigned j = 0; j < size; ++j) {
                columns[j] = std::pow(10., int(j % 13) - 6);
                expected[j] = std::cos(j + .7) / columns[j];
            }
            for (unsigned i = 0; i < size; ++i) {
                const double row = std::pow(10., int((i * 7) % 13) - 6);
                for (unsigned j = 0; j < size; ++j) {
                    const double coefficient = (i == j ? 4. : (j == (i + 1) % size ? -.5 : 0.)) * row * columns[j];
                    if (coefficient != 0) {
                        entries.push_back({i, j, coefficient});
                        b[i] += coefficient * expected[j];
                    }
                }
            }
            const auto x = sparse(size, entries, b);
            const auto fast = directions(size, entries, b);
            for (unsigned i = 0; i < size; ++i)
                require(std::abs(x[i] - expected[i]) < 1e-10 * std::max(1., std::abs(expected[i])),
                        "row/column equilibration changed the independent solution");
            for (unsigned i = 0; i < size; ++i)
                require(std::abs(fast[i] - expected[i]) < 1e-10 * std::max(1., std::abs(expected[i])),
                        "LU/QR row/column scaling changed the independent solution");
        }
        for (unsigned strategy : {0u, 1u, 2u}) {
            detail::sparse_linear_solver backend(strategy != 0, strategy == 2);
            const std::vector<matrix_entry> valid{{0, 0, 1}, {0, 1, -.5}, {1, 0, -.5}, {1, 1, 1}};
            for (double forcing : {0., 1.}) {
                const auto first = backend(2, valid, {1, 0});
                require(std::abs(first[0] - 4. / 3) < 1e-10, "initial factorization was incorrect");
                bool rejected = false;
                try {
                    backend(2, {{0, 0, 1}, {0, 1, -1}, {1, 0, -1}, {1, 1, 1}}, {forcing, -forcing});
                } catch (const diagnostic& error) {
                    rejected = error.code == "E_SINGULAR";
                }
                require(rejected, "zero/nonzero-residual rank loss was accepted");
                const auto restored = backend(2, valid, {0, 1});
                require(std::abs(restored[1] - 4. / 3) < 1e-10 && std::abs(restored[0] - 2. / 3) < 1e-10,
                        "failed numeric factorization poisoned a previously valid cache entry");
            }
            bool rejected = false;
            try {
                backend(2, {{0, 0, 1}, {0, 1, -1}, {1, 0, -1}, {1, 1, 1 - 1e-14}}, {0, 0});
            } catch (const diagnostic& error) {
                rejected = error.code == "E_SINGULAR";
            }
            require(rejected, "accepted-state QR guard missed numerical rank deficiency");
        }
        for (const auto& coefficients : std::vector<std::vector<matrix_entry>>{
                {{0, 0, 1}, {0, 1, -1}, {1, 0, -1}, {1, 1, 1}},
                {{0, 0, 1}, {0, 0, -1}, {1, 1, 1}}}) {
            bool rejected = false;
            try {
                sparse(2, coefficients, {0, 0});
            } catch (const diagnostic& error) {
                rejected = error.code == "E_SINGULAR";
            }
            require(rejected, "sparse backend accepted a singular zero-residual circuit");
        }
        for (unsigned count : {1u, 3u, 16u}) {
            const auto dense = rc_array(count, false, true), actual = rc_array(count, true, true);
            require(std::abs(actual - dense) < 1e-10, "dense/sparse RC trajectories differ");
            const auto fast = rc_array(count, true, true, 1, true);
            require(std::abs(fast - dense) < 1e-10, "LU/QR direction strategy changed the independent RC pole");
            const auto ranked = rc_array(count, true, true, 1, true, true);
            require(std::abs(ranked - dense) < 1e-10, "certified rank strategy changed the independent RC pole");
        }
        rc_array(3, true, false);
        const double coarse = rc_array(128, true, true), fine = rc_array(128, true, true, 2);
        require(fine < .6 * coarse, "large sparse RC network did not converge under time refinement");
        for (unsigned count : {1u, 8u}) {
            const auto dense = nonlinear_array(count, false, true), actual = nonlinear_array(count, true, true);
            const auto fast = nonlinear_array(count, true, true, true);
            const auto ranked = nonlinear_array(count, true, true, true, true);
            require(dense.size() == actual.size(), "nonlinear backends changed the time grid");
            for (unsigned i = 0; i < dense.size(); ++i)
                for (unsigned j = 0; j < dense[i].values.size(); ++j)
                    require(std::abs(*dense[i].values[j] - *actual[i].values[j]) < 1e-8,
                            "coupled MOS/charge trajectory differs between linear backends");
            require(fast.size() == dense.size(), "LU/QR changed the nonlinear time grid");
            for (unsigned i = 0; i < dense.size(); ++i)
                for (unsigned j = 0; j < dense[i].values.size(); ++j)
                    require(std::abs(*dense[i].values[j] - *fast[i].values[j]) < 1e-8,
                            "LU/QR direction strategy changed the independent nonlinear trajectory");
            require(ranked.size() == dense.size(), "certified rank strategy changed the nonlinear time grid");
            for (unsigned i = 0; i < dense.size(); ++i)
                for (unsigned j = 0; j < dense[i].values.size(); ++j)
                    require(std::abs(*dense[i].values[j] - *ranked[i].values[j]) < 1e-8,
                            "certified rank strategy changed the independent nonlinear trajectory");
        }
        nonlinear_array(32, true, true);
        bool rejected = false;
        try {
            simulation sim;
            use_sparse_solver(sim);
            sim.analytic_jacobian = true;
            electrical a, b, ground;
            sim.reference(ground);
            a = resistor(b, resistance(1000));
            sim.observe("floating", a, ground);
            sim.run(duration(1e-9), duration(1e-10));
        } catch (const diagnostic& error) {
            rejected = error.code == "E_SINGULAR";
        }
        require(rejected, "sparse Newton accepted a floating circuit whose residual was already zero");
        std::cout << "Sparse rank/residual, row/column scaling, coupled RC poles through 128 branches, "
                     "MOS arrays, dense equivalence and time refinement passed in "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
