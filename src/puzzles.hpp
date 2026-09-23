#pragma once
#include "csp.hpp"
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace puzzles {
struct Puzzle {
    std::string kind, title;
    csp::Model model;
    int size = 0;
    std::vector<std::string> words;
};

inline Puzzle queens(int size, const std::vector<std::pair<int, int>>& fixed,
                     const std::vector<std::pair<int, int>>& blocked) {
    if (size < 1 || size > 32) throw std::invalid_argument("N must be between 1 and 32");
    Puzzle puzzle{"nqueens", std::to_string(size) + "-Queens", {}, size, {}};
    std::vector<csp::Domain> domains(size);
    for (auto& domain : domains) { domain.resize(size); std::iota(domain.begin(), domain.end(), 0); }
    auto validate = [size](std::pair<int, int> cell) {
        if (cell.first < 0 || cell.first >= size || cell.second < 0 || cell.second >= size)
            throw std::invalid_argument("cell coordinates must be in [0, N)");
    };
    for (const auto& cell : fixed) {
        validate(cell);
        auto& domain = domains[cell.first];
        domain.erase(std::remove_if(domain.begin(), domain.end(), [&](int row) { return row != cell.second; }), domain.end());
    }
    for (const auto& cell : blocked) {
        validate(cell);
        auto& domain = domains[cell.first];
        domain.erase(std::remove(domain.begin(), domain.end(), cell.second), domain.end());
    }
    for (int col = 0; col < size; ++col) puzzle.model.variable("Q" + std::to_string(col), domains[col]);
    for (int left = 0; left < size; ++left)
        for (int right = left + 1; right < size; ++right)
            puzzle.model.constrain(left, right, [left, right](int a, int b) {
                return a != b && std::abs(a - b) != right - left;
            });
    return puzzle;
}

inline Puzzle cryptarithm(std::string expression) {
    expression.erase(std::remove_if(expression.begin(), expression.end(), [](unsigned char c) { return std::isspace(c); }), expression.end());
    for (char& c : expression) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const auto plus = expression.find('+'), equal = expression.find('=');
    if (plus == std::string::npos || equal == std::string::npos || plus >= equal)
        throw std::invalid_argument("use an expression such as SEND+MORE=MONEY");
    std::vector<std::string> words{expression.substr(0, plus), expression.substr(plus + 1, equal - plus - 1), expression.substr(equal + 1)};
    std::set<char> letters, leading;
    for (const auto& word : words) {
        if (word.empty() || word.size() > 12 || !std::all_of(word.begin(), word.end(), [](char c) { return c >= 'A' && c <= 'Z'; }))
            throw std::invalid_argument("each word must contain 1 to 12 ASCII letters; exactly two addends are supported");
        letters.insert(word.begin(), word.end());
        if (word.size() > 1) leading.insert(word.front());
    }
    if (letters.size() > 10) throw std::invalid_argument("base-10 puzzles support at most 10 unique letters");
    Puzzle puzzle{"cryptarithm", expression, {}, 0, words};
    std::map<char, int> ids;
    for (char letter : letters) {
        csp::Domain domain;
        for (int value = leading.count(letter) ? 1 : 0; value < 10; ++value) domain.push_back(value);
        ids[letter] = puzzle.model.variable(std::string(1, letter), std::move(domain));
    }
    for (auto left = ids.begin(); left != ids.end(); ++left)
        for (auto right = std::next(left); right != ids.end(); ++right)
            puzzle.model.constrain(left->second, right->second, [](int a, int b) { return a != b; });
    const int width = static_cast<int>(std::max({words[0].size(), words[1].size(), words[2].size()}));
    std::vector<int> carry;
    for (int col = 0; col <= width; ++col)
        carry.push_back(puzzle.model.variable("carry" + std::to_string(col), (col == 0 || col == width) ? csp::Domain{0} : csp::Domain{0, 1}, false));
    auto symbol = [&](int word, int col) {
        return col < static_cast<int>(words[word].size()) ? ids.at(words[word][words[word].size() - 1 - col]) : -1;
    };
    for (int col = 0; col < width; ++col) {
        const std::vector<int> positions{symbol(0, col), symbol(1, col), symbol(2, col), carry[col], carry[col + 1]};
        auto tuples = std::make_shared<std::vector<std::vector<int>>>();
        for (int a = 0; a < 10; ++a) for (int b = 0; b < 10; ++b) for (int cin = 0; cin < 2; ++cin) {
            const int sum = a + b + cin;
            const std::vector<int> values{a, b, sum % 10, cin, sum / 10};
            bool valid = true;
            for (int p = 0; p < 5; ++p) {
                if (positions[p] == -1) { valid &= values[p] == 0; continue; }
                const auto& domain = puzzle.model.variables[positions[p]].domain;
                valid &= std::find(domain.begin(), domain.end(), values[p]) != domain.end();
                for (int q = 0; q < p; ++q)
                    if (positions[p] == positions[q]) valid &= values[p] == values[q];
            }
            if (valid) tuples->push_back(values);
        }
        csp::Domain domain(tuples->size());
        std::iota(domain.begin(), domain.end(), 0);
        const int hidden = puzzle.model.variable("column" + std::to_string(col), std::move(domain), false);
        std::set<int> linked;
        for (int p = 0; p < 5; ++p) {
            if (positions[p] < 0 || !linked.insert(positions[p]).second) continue;
            puzzle.model.constrain(hidden, positions[p], [tuples, p](int tuple_id, int value) { return (*tuples)[tuple_id][p] == value; });
        }
    }
    return puzzle;
}
} // namespace puzzles
