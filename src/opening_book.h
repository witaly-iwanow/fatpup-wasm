#ifndef FATPUP_WASM_OPENING_BOOK_H
#define FATPUP_WASM_OPENING_BOOK_H

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace opening_book
{

constexpr int UciMoveLen = 4;
constexpr int BookPlyDepth = 8;
constexpr int BookLineLen = BookPlyDepth * UciMoveLen;

// The file is delta-encoded: each line is the suffix that differs from
// the previous full line, so `full = prev[0 .. BookLineLen - line.size()] + line`.
// Lines come out alphabetically sorted because that's how the encoder produced them.
class OpeningBook
{
public:
    bool load(const std::string& path)
    {
        std::ifstream f(path);
        if (!f)
        {
            return false;
        }

        std::string prev;
        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line.size() > static_cast<std::size_t>(BookLineLen))
            {
                continue;
            }
            const std::size_t keep = static_cast<std::size_t>(BookLineLen) - line.size();
            if (keep > prev.size())
            {
                continue;
            }
            std::string full = prev.substr(0, keep) + line;
            if (full.size() != static_cast<std::size_t>(BookLineLen))
            {
                continue;
            }
            lines_.push_back(full);
            prev = std::move(full);
        }
        return !lines_.empty();
    }

    std::size_t size() const { return lines_.size(); }

    // Pick a random next-ply UCI (4 chars) that extends `historyUci`.
    // Probability is proportional to the number of book lines containing each move.
    // Returns an empty string when we're out of book.
    std::string sampleNext(const std::string& historyUci, std::mt19937* rng) const
    {
        if (historyUci.size() >= static_cast<std::size_t>(BookLineLen))
        {
            return std::string();
        }

        auto it = std::lower_bound(lines_.begin(), lines_.end(), historyUci);
        std::vector<std::string> nexts;
        for (; it != lines_.end(); ++it)
        {
            if (it->compare(0, historyUci.size(), historyUci) != 0)
            {
                break;
            }
            nexts.push_back(it->substr(historyUci.size(), UciMoveLen));
        }
        if (nexts.empty())
        {
            return std::string();
        }
        std::uniform_int_distribution<std::size_t> pick(0, nexts.size() - 1);
        return nexts[pick(*rng)];
    }

private:
    std::vector<std::string> lines_;
};

} // namespace opening_book

#endif // FATPUP_WASM_OPENING_BOOK_H
