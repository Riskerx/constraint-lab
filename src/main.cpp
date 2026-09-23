#include "puzzles.hpp"
#include <iomanip>
#include <iostream>
#include <limits>

namespace {
std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch) << std::dec;
            else out << ch;
        }
    }
    return out.str() + '"';
}
template<class T> void array(std::ostream& out, const std::vector<T>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) { if (i) out << ','; out << values[i]; }
    out << ']';
}
void matrix(std::ostream& out, const std::vector<csp::Domain>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) { if (i) out << ','; array(out, values[i]); }
    out << ']';
}
void write_json(const puzzles::Puzzle& puzzle, const csp::Options& options, const csp::Result& result) {
    auto& out = std::cout;
    out << std::boolalpha << std::fixed << std::setprecision(3);
    out << "{\n\"schema_version\":1,\n\"puzzle\":{\"kind\":" << quote(puzzle.kind)
        << ",\"title\":" << quote(puzzle.title) << ",\"size\":" << puzzle.size << ",\"words\":[";
    for (std::size_t i = 0; i < puzzle.words.size(); ++i) { if (i) out << ','; out << quote(puzzle.words[i]); }
    out << "]},\n\"variables\":[";
    bool first = true;
    std::vector<csp::Domain> initial;
    for (const auto& variable : puzzle.model.variables) {
        if (!variable.visible) continue;
        if (!first) out << ',';
        first = false;
        out << quote(variable.name);
        initial.push_back(variable.domain);
    }
    out << "],\n\"initial_domains\":"; matrix(out, initial);
    out << ",\n\"graph\":{\"nodes\":[";
    for (std::size_t i = 0; i < puzzle.model.variables.size(); ++i) {
        if (i) out << ',';
        const auto& variable = puzzle.model.variables[i];
        out << "{\"name\":" << quote(variable.name) << ",\"visible\":" << variable.visible << ",\"domain_size\":" << variable.domain.size() << '}';
    }
    out << "],\"edges\":[";
    for (std::size_t i = 0; i < puzzle.model.constraints.size(); ++i) {
        if (i) out << ',';
        const auto& edge = puzzle.model.constraints[i];
        out << '[' << edge.left << ',' << edge.right << ']';
    }
    out << "]},\n\"options\":{\"ac3\":" << options.ac3 << ",\"limit\":" << options.solution_limit
        << ",\"max_nodes\":" << options.max_nodes << ",\"timeout_ms\":" << options.timeout_ms << "},\n";
    out << "\"status\":" << quote(result.status) << ",\"complete\":" << result.complete
        << ",\"trace_truncated\":" << result.trace_truncated << ",\n\"stats\":{";
    const auto& s = result.stats;
    out << "\"nodes\":" << s.nodes << ",\"decisions\":" << s.decisions << ",\"backtracks\":" << s.backtracks
        << ",\"arc_revisions\":" << s.arc_revisions << ",\"constraint_checks\":" << s.constraint_checks
        << ",\"pruned_values\":" << s.pruned_values << ",\"max_queue\":" << s.max_queue
        << ",\"max_trail_entries\":" << s.max_trail_entries << ",\"max_depth\":" << s.max_depth << ",\"elapsed_ms\":" << s.elapsed_ms << "},\n\"solutions\":";
    matrix(out, result.solutions);
    out << ",\n\"trace\":[";
    for (std::size_t i = 0; i < result.trace.size(); ++i) {
        if (i) out << ',';
        const auto& event = result.trace[i];
        out << "{\"type\":" << quote(event.type) << ",\"detail\":" << quote(event.detail)
            << ",\"depth\":" << event.depth << ",\"domains\":";
        matrix(out, event.domains);
        out << '}';
    }
    out << "]\n}\n";
}
std::uint64_t number(const std::string& text, std::uint64_t max, const std::string& name) {
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        throw std::invalid_argument(name + " must be a nonnegative integer");
    std::uint64_t value;
    try { value = std::stoull(text); }
    catch (...) { throw std::invalid_argument(name + " is too large"); }
    if (value > max) throw std::invalid_argument(name + " exceeds " + std::to_string(max));
    return value;
}
std::pair<int, int> cell(const std::string& value) {
    const auto split = value.find(':');
    if (split == std::string::npos) throw std::invalid_argument("use zero-based column:row coordinates");
    return {static_cast<int>(number(value.substr(0, split), 31, "column")), static_cast<int>(number(value.substr(split + 1), 31, "row"))};
}
void help() {
    std::cout << "Constraint Lab | C++17 CSP solver\n\n"
        "  solver nqueens N [options]\n  solver cryptarithm SEND+MORE=MONEY [options]\n\n"
        "  --fix C:R          Fix a queen (repeatable; zero-based)\n"
        "  --block C:R        Forbid a board cell (repeatable)\n"
        "  --limit K          Stop after K solutions; 0 = all (default 1)\n"
        "  --timeout-ms MS    Cooperative solve deadline; 0 = unlimited (default 2000)\n"
        "  --max-nodes K      Deterministic search budget; 0 = unlimited\n"
        "  --trace-limit K    Maximum replay snapshots, 0 = disabled (default 2000)\n"
        "  --no-ac3           Compare with backtracking alone\n\n"
        "JSON goes to stdout; input errors go to stderr with exit code 2.\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") { help(); return 0; }
        if (argc < 3) throw std::invalid_argument("expected puzzle type and argument; run solver --help");
        const std::string kind = argv[1];
        csp::Options options;
        std::vector<std::pair<int, int>> fixed, blocked;
        for (int i = 3; i < argc; ++i) {
            const std::string flag = argv[i];
            if (flag == "--no-ac3") { options.ac3 = false; continue; }
            if (i + 1 >= argc) throw std::invalid_argument("missing value for " + flag);
            const std::string value = argv[++i];
            if (flag == "--fix") fixed.push_back(cell(value));
            else if (flag == "--block") blocked.push_back(cell(value));
            else if (flag == "--limit") options.solution_limit = number(value, 1000000, flag);
            else if (flag == "--max-nodes") options.max_nodes = number(value, std::numeric_limits<std::uint64_t>::max(), flag);
            else if (flag == "--timeout-ms") options.timeout_ms = static_cast<int>(number(value, 3600000, flag));
            else if (flag == "--trace-limit") options.trace_limit = static_cast<std::size_t>(number(value, 20000, flag));
            else throw std::invalid_argument("unknown option: " + flag);
        }
        if (kind != "nqueens" && (!fixed.empty() || !blocked.empty())) throw std::invalid_argument("--fix and --block apply only to nqueens");
        puzzles::Puzzle puzzle;
        if (kind == "nqueens") puzzle = puzzles::queens(static_cast<int>(number(argv[2], 32, "N")), fixed, blocked);
        else if (kind == "cryptarithm") puzzle = puzzles::cryptarithm(argv[2]);
        else throw std::invalid_argument("unknown puzzle type: " + kind);
        csp::Solver solver(puzzle.model, options);
        write_json(puzzle, options, solver.solve());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
