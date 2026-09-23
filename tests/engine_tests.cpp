#include "../src/csp.hpp"
#include <iostream>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
csp::Result solve(const csp::Model& model, bool ac3 = true) {
    csp::Options options;
    options.solution_limit = 0;
    options.timeout_ms = 0;
    options.ac3 = ac3;
    return csp::Solver(model, options).solve();
}
int main() {
    try {
        csp::Model model;
        const int a = model.variable("A", {1, 2, 3, 3});
        const int b = model.variable("B", {1, 2, 3});
        const int c = model.variable("C", {2, 3, 4});
        model.constrain(a, b, [](int x, int y) { return x < y; });
        model.constrain(a, b, [](int x, int y) { return x + y == 4; });
        model.constrain(b, c, [](int x, int y) { return x + 1 == y; });
        const std::vector<std::vector<int>> expected{{1, 3, 4}};
        require(solve(model).solutions == expected, "asymmetric and parallel binary constraints");
        require(solve(model, false).solutions == expected, "backtracking parity");
        csp::Solver reusable(model, {});
        require(reusable.solve().solutions == reusable.solve().solutions, "repeated solves reset state");
        csp::Model isolated;
        isolated.variable("free", {0, 1});
        require(solve(isolated).solutions.size() == 2, "isolated variable enumeration");
        isolated.variable("empty", {});
        require(solve(isolated).status == "unsatisfiable", "empty domain detection");
        csp::Model contradiction;
        const int x = contradiction.variable("X", {1});
        const int y = contradiction.variable("Y", {1});
        contradiction.constrain(x, y, [](int l, int r) { return l != r; });
        require(solve(contradiction).status == "unsatisfiable", "singleton contradiction");
        require(solve(contradiction, false).status == "unsatisfiable", "singleton contradiction without AC-3");
        std::cout << "Engine regression tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
