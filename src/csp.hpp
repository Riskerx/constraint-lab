#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace csp {
using Domain = std::vector<int>;
struct Variable { std::string name; Domain domain; bool visible; };
struct Constraint {
    int left, right;
    std::function<bool(int, int)> allows;
};
struct Arc { int from, to, constraint; bool reverse; };
struct Model {
    std::vector<Variable> variables;
    std::vector<Constraint> constraints;
    std::vector<Arc> arcs;
    std::vector<std::vector<int>> incoming;

    int variable(std::string name, Domain domain, bool visible = true) {
        std::sort(domain.begin(), domain.end());
        domain.erase(std::unique(domain.begin(), domain.end()), domain.end());
        const int id = static_cast<int>(variables.size());
        variables.push_back({std::move(name), std::move(domain), visible});
        incoming.emplace_back();
        return id;
    }
    void constrain(int left, int right, std::function<bool(int, int)> allows) {
        if (left == right) throw std::invalid_argument("binary constraints need distinct variables");
        const int id = static_cast<int>(constraints.size());
        constraints.push_back({left, right, std::move(allows)});
        incoming.at(right).push_back(static_cast<int>(arcs.size()));
        arcs.push_back({left, right, id, false});
        incoming.at(left).push_back(static_cast<int>(arcs.size()));
        arcs.push_back({right, left, id, true});
    }
};

struct Options {
    std::uint64_t solution_limit = 1, max_nodes = 0;
    int timeout_ms = 2000;
    std::size_t trace_limit = 2000;
    bool ac3 = true;
};
struct Stats {
    std::uint64_t nodes = 0, decisions = 0, backtracks = 0;
    std::uint64_t arc_revisions = 0, constraint_checks = 0, pruned_values = 0;
    std::size_t max_queue = 0, max_trail_entries = 0;
    int max_depth = 0;
    double elapsed_ms = 0;
};
struct Event {
    std::string type, detail;
    int depth;
    std::vector<Domain> domains;
};
struct Result {
    std::string status;
    bool complete = false, trace_truncated = false;
    Stats stats;
    std::vector<std::vector<int>> solutions;
    std::vector<Event> trace;
};

// Reusable binary CSP engine. N-ary constraints can use hidden tuple variables.
class Solver {
    using Clock = std::chrono::steady_clock;
    const Model& model;
    Options options;
    Result result;
    std::vector<Domain> domains;
    std::vector<std::pair<int, Domain>> trail;
    Clock::time_point started;
    std::string stop;

    bool expired() {
        if (!stop.empty()) return true;
        if (options.timeout_ms > 0 && Clock::now() - started >= std::chrono::milliseconds(options.timeout_ms)) {
            stop = "timeout";
            return true;
        }
        return false;
    }
    void event(std::string type, std::string detail, int depth) {
        if (options.trace_limit == 0) return;
        if (result.trace.size() >= options.trace_limit) {
            result.trace_truncated = true;
            return;
        }
        Event entry{std::move(type), std::move(detail), depth, {}};
        for (std::size_t v = 0; v < domains.size(); ++v)
            if (model.variables[v].visible) entry.domains.push_back(domains[v]);
        result.trace.push_back(std::move(entry));
    }
    void replace(int variable, Domain next) {
        result.stats.pruned_values += domains[variable].size() - next.size();
        trail.emplace_back(variable, std::move(domains[variable]));
        domains[variable] = std::move(next);
        result.stats.max_trail_entries = std::max(result.stats.max_trail_entries, trail.size());
    }
    void undo(std::size_t checkpoint) {
        while (trail.size() > checkpoint) {
            auto& entry = trail.back();
            domains[entry.first] = std::move(entry.second);
            trail.pop_back();
        }
    }
    bool allows(const Arc& arc, int a, int b) {
        ++result.stats.constraint_checks;
        const auto& constraint = model.constraints[arc.constraint];
        return arc.reverse ? constraint.allows(b, a) : constraint.allows(a, b);
    }
    bool propagate(const std::vector<int>& seeds, int depth) {
        // Prioritize arcs with a small support domain. ID breaks ties deterministically.
        using Item = std::pair<std::size_t, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
        std::unordered_set<int> pending;
        auto enqueue = [&](int id) {
            if (pending.insert(id).second) {
                queue.push({domains[model.arcs[id].to].size(), id});
                result.stats.max_queue = std::max(result.stats.max_queue, queue.size());
            }
        };
        for (int id : seeds) enqueue(id);
        while (!queue.empty()) {
            if (expired()) return false;
            const int id = queue.top().second;
            queue.pop();
            pending.erase(id);
            const auto& arc = model.arcs[id];
            ++result.stats.arc_revisions;
            Domain kept;
            for (int a : domains[arc.from]) {
                bool supported = false;
                for (int b : domains[arc.to]) {
                    if ((result.stats.constraint_checks & 255U) == 0 && expired()) return false;
                    if (allows(arc, a, b)) { supported = true; break; }
                }
                if (supported) kept.push_back(a);
            }
            if (kept.size() != domains[arc.from].size()) {
                replace(arc.from, std::move(kept));
                event("prune", "AC-3 revised " + model.variables[arc.from].name + " using " + model.variables[arc.to].name, depth);
                if (domains[arc.from].empty()) {
                    event("dead_end", "Domain exhausted: " + model.variables[arc.from].name, depth);
                    return false;
                }
                // Include all incoming arcs: also correct for multiple relations per pair.
                for (int incoming : model.incoming[arc.from]) enqueue(incoming);
            }
        }
        return true;
    }
    bool singleton_consistent() {
        for (const auto& constraint : model.constraints) {
            if (expired()) return false;
            if (domains[constraint.left].size() == 1 && domains[constraint.right].size() == 1) {
                ++result.stats.constraint_checks;
                if (!constraint.allows(domains[constraint.left][0], domains[constraint.right][0])) return false;
            }
        }
        return true;
    }
    int choose() const {
        int best = -1, best_degree = -1;
        for (std::size_t v = 0; v < domains.size(); ++v) {
            if (domains[v].size() <= 1) continue;
            int degree = 0;
            for (int id : model.incoming[v])
                degree += domains[model.arcs[id].from].size() > 1;
            if (best == -1 || domains[v].size() < domains[best].size() ||
                (domains[v].size() == domains[best].size() && degree > best_degree)) {
                best = static_cast<int>(v);
                best_degree = degree;
            }
        }
        return best;
    }
    void search(int depth) {
        if (expired()) return;
        if (options.max_nodes && result.stats.nodes >= options.max_nodes) { stop = "node_limit"; return; }
        ++result.stats.nodes;
        result.stats.max_depth = std::max(result.stats.max_depth, depth);
        const int variable = choose();
        if (variable == -1) {
            if (!singleton_consistent()) return;
            std::vector<int> solution;
            for (std::size_t v = 0; v < domains.size(); ++v)
                if (model.variables[v].visible) solution.push_back(domains[v][0]);
            result.solutions.push_back(std::move(solution));
            event("solution", "Solution " + std::to_string(result.solutions.size()), depth);
            if (options.solution_limit && result.solutions.size() >= options.solution_limit) stop = "solution_limit";
            return;
        }
        const auto choices = domains[variable];
        for (int value : choices) {
            if (expired()) return;
            const std::size_t checkpoint = trail.size();
            ++result.stats.decisions;
            replace(variable, {value});
            event("decision", model.variables[variable].name + " = " + std::to_string(value), depth + 1);
            const bool consistent = options.ac3 ? propagate(model.incoming[variable], depth + 1) : singleton_consistent();
            if (consistent) search(depth + 1);
            else if (stop.empty() && !options.ac3) event("dead_end", "Assigned values violate a constraint", depth + 1);
            undo(checkpoint);
            if (!stop.empty()) return;
            ++result.stats.backtracks;
            event("backtrack", "Restore domain of " + model.variables[variable].name, depth);
        }
    }
public:
    Solver(const Model& input, Options configuration) : model(input), options(configuration) {}
    Result solve() {
        result = {};
        stop.clear();
        trail.clear();
        domains.clear();
        for (const auto& variable : model.variables) domains.push_back(variable.domain);
        started = Clock::now();
        event("start", "Initial domains", 0);
        bool consistent = std::none_of(domains.begin(), domains.end(), [](const Domain& d) { return d.empty(); });
        if (consistent && options.ac3) {
            std::vector<int> seeds(model.arcs.size());
            std::iota(seeds.begin(), seeds.end(), 0);
            consistent = propagate(seeds, 0);
        }
        if (consistent) search(0);
        result.complete = stop.empty();
        result.status = stop.empty() ? (result.solutions.empty() ? "unsatisfiable" : "solved") : stop;
        event("done", result.status, 0);
        result.stats.elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        return std::move(result);
    }
};
} // namespace csp
