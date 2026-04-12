#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "fatpup/engine.h"
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

enum class Player { Strong, Weak };

const char* PlayerName(Player p)
{
    return p == Player::Strong ? "Strong" : "Weak";
}

struct GameOutcome
{
    enum class Kind { StrongWin, WeakWin, Draw };
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

    // Given the move history (concatenated UCI), return a random next-ply UCI string,
    // or empty if we're out of book.
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

fatpup::Move PickStrongMove(const fatpup::Position& pos)
{
    return search::FindBestMove(pos);
}

fatpup::Move PickWeakMove(fatpup::Engine* weakEngine, const fatpup::Position& pos)
{
    weakEngine->SetPosition(pos);
    return weakEngine->GetBestMove();
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

GameResult PlayGame(Player whitePlayer, fatpup::Engine* weakEngine, const Book& book, std::mt19937* rng)
{
    fatpup::Position pos;
    pos.setInitial();

    GameResult gr{};
    std::string historyUci;

    int plies = 0;
    const int maxPlies = MaxFullMoves * 2;

    while (plies < maxPlies)
    {
        const Player toMove = pos.isWhiteTurn() == (whitePlayer == Player::Strong)
            ? Player::Strong
            : Player::Weak;

        const std::vector<fatpup::Move> moves = pos.possibleMoves();
        if (moves.empty())
        {
            const fatpup::Position::State s = pos.getState();
            if (s == fatpup::Position::State::Checkmate)
            {
                const Player winner = (toMove == Player::Strong) ? Player::Weak : Player::Strong;
                gr.outcome = { winner == Player::Strong ? GameOutcome::Kind::StrongWin : GameOutcome::Kind::WeakWin, plies, "checkmate" };
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
            if (toMove == Player::Strong)
            {
                move = PickStrongMove(pos);
            }
            else
            {
                move = PickWeakMove(weakEngine, pos);
            }
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
    if (outcome.kind == GameOutcome::Kind::StrongWin)
    {
        return (whitePlayer == Player::Strong) ? "1-0" : "0-1";
    }
    if (outcome.kind == GameOutcome::Kind::WeakWin)
    {
        return (whitePlayer == Player::Weak) ? "1-0" : "0-1";
    }
    return "1/2-1/2";
}

std::string BuildPgn(int round, Player whitePlayer, const GameResult& gr)
{
    const std::string result = ResultToken(gr.outcome, whitePlayer);
    const char* white = (whitePlayer == Player::Strong) ? "Strong" : "Weak";
    const char* black = (whitePlayer == Player::Strong) ? "Weak" : "Strong";

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

} // namespace

int main()
{
    std::unique_ptr<fatpup::Engine> weakEngine(fatpup::Engine::Create("minimax"));
    if (!weakEngine)
    {
        std::fprintf(stderr, "failed to create fatpup minimax engine\n");
        return 1;
    }

    Book book;
    if (!book.load("tools/opening_book.txt"))
    {
        std::fprintf(stderr, "failed to load tools/opening_book.txt\n");
        return 1;
    }
    std::printf("loaded %zu book lines\n", book.size());

    std::mt19937 rng(RngSeed);

    const std::string pgnDir = "tools/match-games";
    // The caller must ensure this directory exists (the runner script does this).

    int strongWins = 0;
    int weakWins = 0;
    int draws = 0;
    int strongAsWhiteWins = 0;
    int strongAsBlackWins = 0;
    int weakAsWhiteWins = 0;
    int weakAsBlackWins = 0;

    for (int game = 1; game <= Games; ++game)
    {
        const Player whitePlayer = (game % 2 == 1) ? Player::Strong : Player::Weak;
        const GameResult gr = PlayGame(whitePlayer, weakEngine.get(), book, &rng);

        const std::string resultStr = ResultToken(gr.outcome, whitePlayer);
        if (gr.outcome.kind == GameOutcome::Kind::StrongWin)
        {
            ++strongWins;
            if (whitePlayer == Player::Strong) ++strongAsWhiteWins; else ++strongAsBlackWins;
        }
        else if (gr.outcome.kind == GameOutcome::Kind::WeakWin)
        {
            ++weakWins;
            if (whitePlayer == Player::Weak) ++weakAsWhiteWins; else ++weakAsBlackWins;
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
            const std::string pgn = BuildPgn(game, whitePlayer, gr);
            std::fwrite(pgn.data(), 1, pgn.size(), f);
            std::fclose(f);
        }
        else
        {
            std::fprintf(stderr, "warn: could not write %s\n", fname);
        }

        std::printf("game %3d: white=%-6s  result %-7s  plies=%3d  (%s)\n",
                    game, PlayerName(whitePlayer), resultStr.c_str(), gr.outcome.plies, gr.outcome.reason);
        std::fflush(stdout);
    }

    std::printf("\n=== Match summary (%d games) ===\n", Games);
    std::printf("Strong: %d wins (%d as white, %d as black)\n", strongWins, strongAsWhiteWins, strongAsBlackWins);
    std::printf("Weak:   %d wins (%d as white, %d as black)\n", weakWins, weakAsWhiteWins, weakAsBlackWins);
    std::printf("Draws:  %d\n", draws);
    const double strongScore = strongWins + 0.5 * draws;
    const double weakScore = weakWins + 0.5 * draws;
    std::printf("Score:  Strong %.1f - %.1f Weak\n", strongScore, weakScore);
    return 0;
}
