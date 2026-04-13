#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "fatpup/move.h"
#include "fatpup/position.h"

#include "../src/search.h"

namespace
{

constexpr int MaxFullMoves = 100; // 100 full moves => 200 plies, then declare draw.
constexpr int BookPlyDepth = 8;
constexpr int Games = 100;
constexpr unsigned RngSeed = 0xC0FFEE;
constexpr int UciMoveLen = 4;
constexpr int BookLineLen = BookPlyDepth * UciMoveLen;

enum class Player { A, B };

struct PlayerInfo
{
    const char* name;
    search::SearchLimits limits;
};

struct GameOutcome
{
    enum class Kind { AWin, BWin, Draw };
    Kind kind;
    int plies;
    const char* reason;
};

struct GameResult
{
    GameOutcome outcome;
    std::vector<fatpup::Move> moves;
};

std::string MoveToUci(fatpup::Move m)
{
    std::string s;
    s.reserve(4);
    s += static_cast<char>('a' + m.fields.src_col);
    s += static_cast<char>('1' + m.fields.src_row);
    s += static_cast<char>('a' + m.fields.dst_col);
    s += static_cast<char>('1' + m.fields.dst_row);
    return s;
}

fatpup::Move FindMoveByUci(const fatpup::Position& pos, const std::string& uci)
{
    const std::vector<fatpup::Move> moves = pos.possibleMoves();
    for (const fatpup::Move m : moves)
    {
        if (MoveToUci(m) == uci)
        {
            return m;
        }
    }
    return fatpup::Move();
}

class Book
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
        std::sort(lines_.begin(), lines_.end());
        return !lines_.empty();
    }

    std::size_t size() const { return lines_.size(); }

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
        std::sort(nexts.begin(), nexts.end());
        nexts.erase(std::unique(nexts.begin(), nexts.end()), nexts.end());
        std::uniform_int_distribution<std::size_t> pick(0, nexts.size() - 1);
        return nexts[pick(*rng)];
    }

private:
    std::vector<std::string> lines_;
};

fatpup::Move TryBookMove(const Book& book, const std::string& historyUci, const fatpup::Position& pos, std::mt19937* rng)
{
    const std::string bookUci = book.sampleNext(historyUci, rng);
    if (bookUci.empty())
    {
        return fatpup::Move();
    }
    return FindMoveByUci(pos, bookUci);
}

bool OnlyKingsRemain(const fatpup::Position& pos)
{
    int kings = 0;
    for (int row = 0; row < fatpup::BOARD_SIZE; ++row)
    {
        for (int col = 0; col < fatpup::BOARD_SIZE; ++col)
        {
            const unsigned char piece = pos.square(row, col).piece();
            if (piece == 0)
            {
                continue;
            }
            if ((piece & fatpup::PieceMask) != fatpup::King)
            {
                return false;
            }
            ++kings;
        }
    }
    return kings == 2;
}

GameResult PlayGame(Player whitePlayer, const PlayerInfo& playerA, const PlayerInfo& playerB, const Book& book, std::mt19937* rng)
{
    fatpup::Position pos;
    pos.setInitial();

    GameResult gr{};
    std::string historyUci;

    int plies = 0;
    const int maxPlies = MaxFullMoves * 2;

    while (plies < maxPlies)
    {
        const Player toMove = pos.isWhiteTurn() == (whitePlayer == Player::A)
            ? Player::A
            : Player::B;

        const std::vector<fatpup::Move> moves = pos.possibleMoves();
        if (moves.empty())
        {
            const fatpup::Position::State s = pos.getState();
            if (s == fatpup::Position::State::Checkmate)
            {
                const Player winner = (toMove == Player::A) ? Player::B : Player::A;
                gr.outcome = { winner == Player::A ? GameOutcome::Kind::AWin : GameOutcome::Kind::BWin, plies, "checkmate" };
                return gr;
            }
            gr.outcome = { GameOutcome::Kind::Draw, plies, "stalemate" };
            return gr;
        }

        if (OnlyKingsRemain(pos))
        {
            gr.outcome = { GameOutcome::Kind::Draw, plies, "K vs K" };
            return gr;
        }

        fatpup::Move move = TryBookMove(book, historyUci, pos, rng);
        if (move.isEmpty())
        {
            const search::SearchLimits& limits = (toMove == Player::A) ? playerA.limits : playerB.limits;
            move = search::FindBestMove(pos, limits);
        }

        if (move.isEmpty())
        {
            gr.outcome = { GameOutcome::Kind::Draw, plies, "engine empty move" };
            return gr;
        }

        gr.moves.push_back(move);
        if (historyUci.size() < static_cast<std::size_t>(BookLineLen))
        {
            historyUci += MoveToUci(move);
        }
        pos += move;
        ++plies;
    }

    gr.outcome = { GameOutcome::Kind::Draw, plies, "move limit" };
    return gr;
}

std::string ResultToken(const GameOutcome& outcome, Player whitePlayer)
{
    if (outcome.kind == GameOutcome::Kind::AWin)
    {
        return (whitePlayer == Player::A) ? "1-0" : "0-1";
    }
    if (outcome.kind == GameOutcome::Kind::BWin)
    {
        return (whitePlayer == Player::B) ? "1-0" : "0-1";
    }
    return "1/2-1/2";
}

std::string BuildPgn(int round, Player whitePlayer, const PlayerInfo& playerA, const PlayerInfo& playerB, const GameResult& gr)
{
    const std::string result = ResultToken(gr.outcome, whitePlayer);
    const char* white = (whitePlayer == Player::A) ? playerA.name : playerB.name;
    const char* black = (whitePlayer == Player::A) ? playerB.name : playerA.name;

    std::ostringstream out;
    out << "[Event \"Engine match\"]\n";
    out << "[Site \"local\"]\n";
    out << "[Round \"" << round << "\"]\n";
    out << "[White \"" << white << "\"]\n";
    out << "[Black \"" << black << "\"]\n";
    out << "[Result \"" << result << "\"]\n";
    out << "[Termination \"" << gr.outcome.reason << "\"]\n\n";

    fatpup::Position replay;
    replay.setInitial();
    int fullMove = 1;
    std::ostringstream line;
    for (std::size_t i = 0; i < gr.moves.size(); ++i)
    {
        const fatpup::Move m = gr.moves[i];
        const bool whiteToMove = replay.isWhiteTurn();
        if (whiteToMove)
        {
            line << fullMove << ". ";
        }
        else if (i == 0)
        {
            line << fullMove << "... ";
        }
        line << replay.moveToStringPGN(m) << ' ';
        replay.moveDone(m);
        if (!whiteToMove)
        {
            ++fullMove;
        }
    }
    line << result;
    out << line.str() << "\n";
    return out.str();
}

bool ParseStrength(const char* arg, PlayerInfo* info)
{
    if (std::strcmp(arg, "weak") == 0)
    {
        info->name = "Weak";
        info->limits = search::SearchLimits::Weak();
        return true;
    }
    if (std::strcmp(arg, "medium") == 0)
    {
        info->name = "Medium";
        info->limits = search::SearchLimits::Medium();
        return true;
    }
    if (std::strcmp(arg, "strong") == 0)
    {
        info->name = "Strong";
        info->limits = search::SearchLimits::Strong();
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: %s <playerA> <playerB>\n  strengths: weak, medium, strong\n", argv[0]);
        return 1;
    }

    PlayerInfo playerA{};
    PlayerInfo playerB{};
    if (!ParseStrength(argv[1], &playerA) || !ParseStrength(argv[2], &playerB))
    {
        std::fprintf(stderr, "invalid strength. use: weak, medium, strong\n");
        return 1;
    }

    Book book;
    if (!book.load("tools/opening_book.txt"))
    {
        std::fprintf(stderr, "failed to load tools/opening_book.txt\n");
        return 1;
    }
    std::printf("loaded %zu book lines\n", book.size());
    std::printf("match: %s vs %s (%d games)\n\n", playerA.name, playerB.name, Games);

    std::mt19937 rng(RngSeed);

    const std::string pgnDir = "tools/match-games";

    int aWins = 0;
    int bWins = 0;
    int draws = 0;
    int aAsWhiteWins = 0;
    int aAsBlackWins = 0;
    int bAsWhiteWins = 0;
    int bAsBlackWins = 0;

    for (int game = 1; game <= Games; ++game)
    {
        const Player whitePlayer = (game % 2 == 1) ? Player::A : Player::B;
        const GameResult gr = PlayGame(whitePlayer, playerA, playerB, book, &rng);

        const char* whiteName = (whitePlayer == Player::A) ? playerA.name : playerB.name;
        const std::string resultStr = ResultToken(gr.outcome, whitePlayer);
        if (gr.outcome.kind == GameOutcome::Kind::AWin)
        {
            ++aWins;
            if (whitePlayer == Player::A) ++aAsWhiteWins; else ++aAsBlackWins;
        }
        else if (gr.outcome.kind == GameOutcome::Kind::BWin)
        {
            ++bWins;
            if (whitePlayer == Player::B) ++bAsWhiteWins; else ++bAsBlackWins;
        }
        else
        {
            ++draws;
        }

        char fname[256];
        std::snprintf(fname, sizeof(fname), "%s/game_%03d.pgn", pgnDir.c_str(), game);
        FILE* f = std::fopen(fname, "w");
        if (f)
        {
            const std::string pgn = BuildPgn(game, whitePlayer, playerA, playerB, gr);
            std::fwrite(pgn.data(), 1, pgn.size(), f);
            std::fclose(f);
        }
        else
        {
            std::fprintf(stderr, "warn: could not write %s\n", fname);
        }

        std::printf("game %3d: white=%-6s  result %-7s  plies=%3d  (%s)\n",
                    game, whiteName, resultStr.c_str(), gr.outcome.plies, gr.outcome.reason);
        std::fflush(stdout);
    }

    std::printf("\n=== Match summary (%d games) ===\n", Games);
    std::printf("%s: %d wins (%d as white, %d as black)\n", playerA.name, aWins, aAsWhiteWins, aAsBlackWins);
    std::printf("%s: %d wins (%d as white, %d as black)\n", playerB.name, bWins, bAsWhiteWins, bAsBlackWins);
    std::printf("Draws:  %d\n", draws);
    const double aScore = aWins + 0.5 * draws;
    const double bScore = bWins + 0.5 * draws;
    std::printf("Score:  %s %.1f - %.1f %s\n", playerA.name, aScore, bScore, playerB.name);
    return 0;
}
