#ifndef FATPUP_WASM_SEARCH_H
#define FATPUP_WASM_SEARCH_H

#include <cstddef>
#include <vector>

#include "fatpup/move.h"
#include "fatpup/position.h"
#include "fatpup/square.h"

namespace search
{

constexpr int InfScore = 1000000;
constexpr int MateScore = 100000;

inline int PieceMaterial(unsigned char piece)
{
    switch (piece & fatpup::PieceMask)
    {
    case fatpup::Pawn:   return 100;
    case fatpup::Knight: return 320;
    case fatpup::Bishop: return 330;
    case fatpup::Rook:   return 500;
    case fatpup::Queen:  return 900;
    default: return 0;
    }
}

// piece-square tables, white perspective; index is row*8 + col, with row 0 = rank 1.
constexpr int PawnPST[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
      5,  5,  5,-15,-15,  5,  5,  5,
      1,  2,  5, 10, 10,  5,  2,  1,
      2,  4,  8, 20, 20,  8,  4,  2,
      5,  8, 14, 25, 25, 14,  8,  5,
     10, 14, 20, 30, 30, 20, 14, 10,
     50, 50, 50, 50, 50, 50, 50, 50,
      0,  0,  0,  0,  0,  0,  0,  0,
};

constexpr int KnightPST[64] = {
    -30,-20,-10,-10,-10,-10,-20,-30,
    -20,-10,  0,  3,  3,  0,-10,-20,
    -10,  3, 10, 12, 12, 10,  3,-10,
    -10,  5, 12, 18, 18, 12,  5,-10,
    -10,  3, 12, 18, 18, 12,  3,-10,
    -10,  5, 10, 12, 12, 10,  5,-10,
    -20,-10,  0,  5,  5,  0,-10,-20,
    -30,-20,-10,-10,-10,-10,-20,-30,
};

constexpr int BishopPST[64] = {
    -10,-10,-10,-10,-10,-10,-10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 12, 12, 10,  0,-10,
    -10,  5,  8, 12, 12,  8,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,-10,-10,-10,-10,-10,-10,-10,
};

constexpr int RookPST[64] = {
      0,  0,  3,  6,  6,  3,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,
     10, 10, 10, 10, 10, 10, 10, 10,
      5,  5,  5,  5,  5,  5,  5,  5,
};

constexpr int KingPST[64] = {
     20, 30, 10,  0,  0, 10, 30, 20,
     20, 20,  0,  0,  0,  0, 20, 20,
    -10,-20,-20,-20,-20,-20,-20,-10,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
};

inline int PiecePositional(unsigned char piece, int row, int col, bool isWhite)
{
    const int idx = isWhite ? (row * 8 + col) : ((7 - row) * 8 + col);
    switch (piece & fatpup::PieceMask)
    {
    case fatpup::Pawn:   return PawnPST[idx];
    case fatpup::Knight: return KnightPST[idx];
    case fatpup::Bishop: return BishopPST[idx];
    case fatpup::Rook:   return RookPST[idx];
    case fatpup::King:   return KingPST[idx];
    default: return 0;
    }
}

inline int Evaluate(const fatpup::Position& pos)
{
    int score = 0;
    for (int row = 0; row < fatpup::BOARD_SIZE; ++row)
    {
        for (int col = 0; col < fatpup::BOARD_SIZE; ++col)
        {
            const fatpup::Square sq = pos.square(row, col);
            const unsigned char piece = sq.piece();
            if (piece == 0)
            {
                continue;
            }
            const bool white = sq.isWhite();
            const int v = PieceMaterial(piece) + PiecePositional(piece, row, col, white);
            score += white ? v : -v;
        }
    }
    return score;
}

inline bool GivesCheck(const fatpup::Position& pos, fatpup::Move move)
{
    fatpup::Position next = pos;
    next.moveDone(move);
    const fatpup::Position::State s = next.getState();
    return s == fatpup::Position::State::Check || s == fatpup::Position::State::Checkmate;
}

enum FilterMode { AllMoves, ChecksAndCaptures, CapturesOnly };

// Ply 1 (root, ours): all moves.
// Ply 2 (opponent): all moves.
// Ply 3 (ours) and 4 (opponent): all moves if 8 or fewer, else checks+captures.
// Ply 5+: captures only (quiescence).
inline FilterMode FilterForPly(int ply, std::size_t allMoveCount)
{
    if (ply <= 2)
    {
        return AllMoves;
    }
    if (ply <= 4)
    {
        return (allMoveCount > 8) ? ChecksAndCaptures : AllMoves;
    }
    return CapturesOnly;
}

inline void OrderMoves(const fatpup::Position& pos, const std::vector<fatpup::Move>& moves, FilterMode mode, std::vector<fatpup::Move>* out)
{
    out->reserve(moves.size());

    for (const fatpup::Move m : moves)
    {
        if (pos.isMoveCapture(m))
        {
            out->push_back(m);
        }
    }

    if (mode == CapturesOnly)
    {
        return;
    }

    if (mode == ChecksAndCaptures)
    {
        for (const fatpup::Move m : moves)
        {
            if (!pos.isMoveCapture(m) && GivesCheck(pos, m))
            {
                out->push_back(m);
            }
        }
        return;
    }

    for (const fatpup::Move m : moves)
    {
        if (!pos.isMoveCapture(m))
        {
            out->push_back(m);
        }
    }
}

inline int Search(const fatpup::Position& pos, int ply, int alpha, int beta)
{
    const std::vector<fatpup::Move> moves = pos.possibleMoves();
    const bool maximizing = pos.isWhiteTurn();

    if (moves.empty())
    {
        const fatpup::Position::State s = pos.getState();
        if (s == fatpup::Position::State::Checkmate)
        {
            return maximizing ? -(MateScore - ply) : (MateScore - ply);
        }
        return 0;
    }

    const FilterMode mode = FilterForPly(ply, moves.size());

    // Quiescence-style stand-pat: when we restrict to a tactical subset
    // (captures + checks at ply 3-4, captures-only at ply 5+), the side
    // to move can refuse to enter the tactical sequence and accept the
    // static evaluation. Without this, a side may be forced into a losing
    // capture when better quiet moves exist.
    int standPat = 0;
    const bool useStandPat = (mode != AllMoves);
    if (useStandPat)
    {
        standPat = Evaluate(pos);
        if (maximizing)
        {
            if (standPat >= beta)
            {
                return standPat;
            }
            if (standPat > alpha)
            {
                alpha = standPat;
            }
        }
        else
        {
            if (standPat <= alpha)
            {
                return standPat;
            }
            if (standPat < beta)
            {
                beta = standPat;
            }
        }
    }

    std::vector<fatpup::Move> ordered;
    OrderMoves(pos, moves, mode, &ordered);

    if (ordered.empty())
    {
        return useStandPat ? standPat : Evaluate(pos);
    }

    int best = useStandPat ? standPat : (maximizing ? -InfScore : InfScore);
    for (const fatpup::Move m : ordered)
    {
        fatpup::Position next = pos;
        next.moveDone(m);
        const int eval = Search(next, ply + 1, alpha, beta);

        if (maximizing)
        {
            if (eval > best)
            {
                best = eval;
            }
            if (best > alpha)
            {
                alpha = best;
            }
        }
        else
        {
            if (eval < best)
            {
                best = eval;
            }
            if (best < beta)
            {
                beta = best;
            }
        }

        if (alpha >= beta)
        {
            break;
        }
    }

    return best;
}

inline fatpup::Move FindBestMove(const fatpup::Position& pos)
{
    const std::vector<fatpup::Move> moves = pos.possibleMoves();
    if (moves.empty())
    {
        return fatpup::Move();
    }

    const bool maximizing = pos.isWhiteTurn();

    std::vector<fatpup::Move> ordered;
    OrderMoves(pos, moves, AllMoves, &ordered);

    int alpha = -InfScore;
    int beta = InfScore;
    int best = maximizing ? -InfScore : InfScore;
    fatpup::Move bestMove = ordered.front();

    for (const fatpup::Move m : ordered)
    {
        fatpup::Position next = pos;
        next.moveDone(m);
        const int eval = Search(next, 2, alpha, beta);

        if (maximizing)
        {
            if (eval > best)
            {
                best = eval;
                bestMove = m;
            }
            if (best > alpha)
            {
                alpha = best;
            }
        }
        else
        {
            if (eval < best)
            {
                best = eval;
                bestMove = m;
            }
            if (best < beta)
            {
                beta = best;
            }
        }
    }

    return bestMove;
}

} // namespace search

#endif // FATPUP_WASM_SEARCH_H
