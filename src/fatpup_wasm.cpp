#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <emscripten/emscripten.h>

#include "fatpup/engine.h"
#include "fatpup/move.h"
#include "fatpup/position.h"
#include "fatpup/square.h"

namespace
{
namespace search
{
constexpr int InfScore = 1000000;
constexpr int MateScore = 100000;

int PieceMaterial(unsigned char piece)
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

int PiecePositional(unsigned char piece, int row, int col, bool isWhite)
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

int Evaluate(const fatpup::Position& pos)
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

bool GivesCheck(const fatpup::Position& pos, fatpup::Move move)
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
FilterMode FilterForPly(int ply, std::size_t allMoveCount)
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

void OrderMoves(const fatpup::Position& pos, const std::vector<fatpup::Move>& moves, FilterMode mode, std::vector<fatpup::Move>* out)
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

int Search(const fatpup::Position& pos, int ply, int alpha, int beta)
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

fatpup::Move FindBestMove(const fatpup::Position& pos)
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

enum class EngineMode { Weak, Strong };

struct AppState
{
    fatpup::Position position;
    fatpup::Position startPosition;

    bool userPlaysWhite = true;
    EngineMode engineMode = EngineMode::Weak;
    std::unique_ptr<fatpup::Engine> weakEngine;

    std::vector<fatpup::Move> moveHistory;
    std::vector<std::string> whiteMoves;
    std::vector<std::string> blackMoves;

    int startHalfMoveClock = 0;
    int startFullMoveNumber = 1;
    int halfMoveClock = 0;
    int fullMoveNumber = 1;

    fatpup::Move lastMove;
    bool hasLastMove = false;

    bool initialized = false;
};

AppState g_app;
std::string g_resultBuffer;
std::string g_stateBuffer;

std::string Trim(const std::string& text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
    {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        --end;
    }

    return text.substr(start, end - start);
}

std::string ToLower(const std::string& text)
{
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool IsSquareSymbol(char file, char rank)
{
    const char fileLower = static_cast<char>(std::tolower(static_cast<unsigned char>(file)));
    return (fileLower >= 'a' && fileLower <= 'h') && (rank >= '1' && rank <= '8');
}

std::string NormalizeMoveInput(const std::string& rawInput)
{
    std::string normalized;
    normalized.reserve(rawInput.size());
    for (char c : rawInput)
    {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '-' || c == 'x')
        {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return normalized;
}

int PromotionPieceFromSymbol(char symbol)
{
    switch (symbol)
    {
    case 'q': return fatpup::Queen;
    case 'r': return fatpup::Rook;
    case 'b': return fatpup::Bishop;
    case 'n': return fatpup::Knight;
    default: return 0;
    }
}

bool ParseAndResolveMove(const fatpup::Position& position, const std::string& rawInput, fatpup::Move* resultMove, std::string* error)
{
    const std::string input = NormalizeMoveInput(rawInput);
    if (input.length() != 4 && input.length() != 5)
    {
        *error = "Move must look like e2e4 or e7e8q.";
        return false;
    }

    if (!IsSquareSymbol(input[0], input[1]) || !IsSquareSymbol(input[2], input[3]))
    {
        *error = "Square names must be in range a1..h8.";
        return false;
    }

    const int srcRow = fatpup::symbolToRowIdx(input[1]);
    const int srcCol = fatpup::symbolToColumnIdx(input[0]);
    const int dstRow = fatpup::symbolToRowIdx(input[3]);
    const int dstCol = fatpup::symbolToColumnIdx(input[2]);
    const std::vector<fatpup::Move> candidates = position.possibleMoves(srcRow, srcCol, dstRow, dstCol);
    if (candidates.empty())
    {
        *error = "Illegal move in current position.";
        return false;
    }

    if (candidates.size() == 1)
    {
        if (input.length() == 5)
        {
            const int requestedPromotion = PromotionPieceFromSymbol(input[4]);
            if (requestedPromotion == 0)
            {
                *error = "Promotion piece must be one of q, r, b, n.";
                return false;
            }

            if (candidates[0].fields.promoted_to != 0 && candidates[0].fields.promoted_to != requestedPromotion)
            {
                *error = "Promotion piece does not match legal move.";
                return false;
            }
        }

        *resultMove = candidates[0];
        return true;
    }

    if (input.length() != 5)
    {
        *error = "Promotion requires a 5th symbol (q, r, b, or n), for example a7a8q.";
        return false;
    }

    const int requestedPromotion = PromotionPieceFromSymbol(input[4]);
    if (requestedPromotion == 0)
    {
        *error = "Promotion piece must be one of q, r, b, n.";
        return false;
    }

    for (const fatpup::Move& move : candidates)
    {
        if (move.fields.promoted_to == requestedPromotion)
        {
            *resultMove = move;
            return true;
        }
    }

    *error = "Requested promotion piece is not legal for this move.";
    return false;
}

bool IsGameOverState(fatpup::Position::State state)
{
    return state == fatpup::Position::State::Checkmate || state == fatpup::Position::State::Stalemate;
}

bool OnlyKingsRemain(const fatpup::Position& position)
{
    int kingCount = 0;

    for (int row = 0; row < fatpup::BOARD_SIZE; ++row)
    {
        for (int col = 0; col < fatpup::BOARD_SIZE; ++col)
        {
            const unsigned char piece = position.square(row, col).piece();
            if (piece == 0)
            {
                continue;
            }
            if (piece != fatpup::King)
            {
                return false;
            }
            ++kingCount;
        }
    }

    return kingCount == 2;
}

bool IsGameOverPosition(const fatpup::Position& position)
{
    return IsGameOverState(position.getState()) || OnlyKingsRemain(position);
}

bool IsUserToMove(const AppState& app)
{
    return app.position.isWhiteTurn() == app.userPlaysWhite;
}

std::string StatusText(const fatpup::Position& position)
{
    if (OnlyKingsRemain(position))
    {
        return "Draw.";
    }

    const fatpup::Position::State state = position.getState();
    if (state == fatpup::Position::State::Check)
    {
        return "Check.";
    }
    if (state == fatpup::Position::State::Checkmate)
    {
        return std::string("Checkmate. ") + (position.isWhiteTurn() ? "Black" : "White") + " wins.";
    }
    if (state == fatpup::Position::State::Stalemate)
    {
        return "Stalemate.";
    }
    return std::string();
}

char PieceToFenChar(const fatpup::Square& square)
{
    const unsigned char piece = square.piece();
    char symbol = 0;
    switch (piece)
    {
    case fatpup::Pawn: symbol = 'P'; break;
    case fatpup::Knight: symbol = 'N'; break;
    case fatpup::Bishop: symbol = 'B'; break;
    case fatpup::Rook: symbol = 'R'; break;
    case fatpup::Queen: symbol = 'Q'; break;
    case fatpup::King: symbol = 'K'; break;
    default: return 0;
    }

    if (!square.isWhite())
    {
        symbol = static_cast<char>(std::tolower(static_cast<unsigned char>(symbol)));
    }

    return symbol;
}

std::string PositionToFen(const fatpup::Position& position, int halfMoveClock, int fullMoveNumber)
{
    std::string fen;

    for (int row = fatpup::BOARD_SIZE - 1; row >= 0; --row)
    {
        int emptySquares = 0;
        for (int col = 0; col < fatpup::BOARD_SIZE; ++col)
        {
            const fatpup::Square square = position.square(row, col);
            const char pieceChar = PieceToFenChar(square);
            if (pieceChar == 0)
            {
                ++emptySquares;
                continue;
            }

            if (emptySquares > 0)
            {
                fen += static_cast<char>('0' + emptySquares);
                emptySquares = 0;
            }

            fen += pieceChar;
        }

        if (emptySquares > 0)
        {
            fen += static_cast<char>('0' + emptySquares);
        }

        if (row > 0)
        {
            fen += '/';
        }
    }

    fen += position.isWhiteTurn() ? " w " : " b ";

    std::string castlingAvailability;
    const fatpup::Square e1 = position.square("e1");
    const fatpup::Square e8 = position.square("e8");

    if (e1.pieceWithColor() == (fatpup::King | fatpup::White))
    {
        const fatpup::Square h1 = position.square("h1");
        const fatpup::Square a1 = position.square("a1");
        if (h1.pieceWithColor() == (fatpup::Rook | fatpup::White) && h1.isFlagSet(fatpup::CanCastle))
        {
            castlingAvailability += 'K';
        }
        if (a1.pieceWithColor() == (fatpup::Rook | fatpup::White) && a1.isFlagSet(fatpup::CanCastle))
        {
            castlingAvailability += 'Q';
        }
    }

    if (e8.pieceWithColor() == (fatpup::King | fatpup::Black))
    {
        const fatpup::Square h8 = position.square("h8");
        const fatpup::Square a8 = position.square("a8");
        if (h8.pieceWithColor() == (fatpup::Rook | fatpup::Black) && h8.isFlagSet(fatpup::CanCastle))
        {
            castlingAvailability += 'k';
        }
        if (a8.pieceWithColor() == (fatpup::Rook | fatpup::Black) && a8.isFlagSet(fatpup::CanCastle))
        {
            castlingAvailability += 'q';
        }
    }

    fen += castlingAvailability.empty() ? "-" : castlingAvailability;
    fen += " ";

    std::string enPassant = "-";
    for (int row = 0; row < fatpup::BOARD_SIZE; ++row)
    {
        for (int col = 0; col < fatpup::BOARD_SIZE; ++col)
        {
            if (position.square(row, col).isFlagSet(fatpup::EnPassant))
            {
                enPassant = std::string{
                    static_cast<char>('a' + col),
                    static_cast<char>('1' + row)
                };
                break;
            }
        }
        if (enPassant != "-")
        {
            break;
        }
    }

    fen += enPassant;
    fen += " ";
    fen += std::to_string(halfMoveClock);
    fen += " ";
    fen += std::to_string(std::max(1, fullMoveNumber));

    return fen;
}

void ParseFenCounters(const std::string& fen, int* halfMoveClock, int* fullMoveNumber)
{
    *halfMoveClock = 0;
    *fullMoveNumber = 1;

    std::istringstream input(fen);
    std::vector<std::string> fields;
    std::string field;
    while (input >> field)
    {
        fields.push_back(field);
    }

    if (fields.size() >= 6)
    {
        try
        {
            const int parsedHalfMove = std::stoi(fields[4]);
            if (parsedHalfMove >= 0)
            {
                *halfMoveClock = parsedHalfMove;
            }
        }
        catch (...)
        {
        }

        try
        {
            const int parsedFullMove = std::stoi(fields[5]);
            if (parsedFullMove >= 1)
            {
                *fullMoveNumber = parsedFullMove;
            }
        }
        catch (...)
        {
        }
    }
}

void UpdateFenCountersForMove(const fatpup::Position& positionBeforeMove, fatpup::Move move, int* halfMoveClock, int* fullMoveNumber)
{
    const fatpup::Square srcSquare = positionBeforeMove.square(move.fields.src_row, move.fields.src_col);
    const bool isPawnMove = (srcSquare.piece() == fatpup::Pawn);
    const bool isCapture = positionBeforeMove.isMoveCapture(move);

    if (isPawnMove || isCapture)
    {
        *halfMoveClock = 0;
    }
    else
    {
        ++(*halfMoveClock);
    }

    if (!positionBeforeMove.isWhiteTurn())
    {
        ++(*fullMoveNumber);
    }
}

void ApplyMoveAndTrackCounters(fatpup::Position* position, fatpup::Move move, int* halfMoveClock, int* fullMoveNumber)
{
    UpdateFenCountersForMove(*position, move, halfMoveClock, fullMoveNumber);
    *position += move;
}

std::string PgnResultToken(const fatpup::Position& position)
{
    if (OnlyKingsRemain(position))
    {
        return "1/2-1/2";
    }

    const fatpup::Position::State state = position.getState();
    if (state == fatpup::Position::State::Checkmate)
    {
        return position.isWhiteTurn() ? "0-1" : "1-0";
    }
    if (state == fatpup::Position::State::Stalemate)
    {
        return "1/2-1/2";
    }
    return "*";
}

std::string CurrentDateForPgnTag()
{
    const std::time_t now = std::time(nullptr);
    std::tm localTime = {};
#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif

    char buffer[11] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%04d.%02d.%02d",
        localTime.tm_year + 1900,
        localTime.tm_mon + 1,
        localTime.tm_mday);
    return std::string(buffer);
}

const char* SidePlayerName(bool whiteSide, bool userPlaysWhite)
{
    const bool userOnThisSide = (whiteSide == userPlaysWhite);
    return userOnThisSide ? "NN" : "Fatpup";
}

std::string GameScoreToPgn(
    const fatpup::Position& startPosition,
    const std::vector<fatpup::Move>& moveHistory,
    int startFullMoveNumber,
    const fatpup::Position& currentPosition)
{
    fatpup::Position replayPosition = startPosition;
    int fullMoveNumber = std::max(1, startFullMoveNumber);
    std::vector<std::string> tokens;
    tokens.reserve(moveHistory.size() * 2 + 2);

    for (const fatpup::Move move : moveHistory)
    {
        const bool whiteToMove = replayPosition.isWhiteTurn();
        if (whiteToMove)
        {
            tokens.push_back(std::to_string(fullMoveNumber) + ".");
        }
        else if (tokens.empty())
        {
            tokens.push_back(std::to_string(fullMoveNumber) + "...");
        }

        tokens.push_back(replayPosition.moveToStringPGN(move));
        replayPosition.moveDone(move);

        if (!whiteToMove)
        {
            ++fullMoveNumber;
        }
    }

    tokens.push_back(PgnResultToken(currentPosition));

    std::ostringstream out;
    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        if (i)
        {
            out << " ";
        }
        out << tokens[i];
    }
    return out.str();
}

std::string GameScoreWithTags(const AppState& app)
{
    const std::string result = PgnResultToken(app.position);

    std::ostringstream out;
    out
        << "[Date \"" << CurrentDateForPgnTag() << "\"]\n"
        << "[White \"" << SidePlayerName(true, app.userPlaysWhite) << "\"]\n"
        << "[Black \"" << SidePlayerName(false, app.userPlaysWhite) << "\"]\n"
        << "[Result \"" << result << "\"]\n\n"
        << GameScoreToPgn(app.startPosition, app.moveHistory, app.startFullMoveNumber, app.position);

    return out.str();
}

std::string HelpText()
{
    return std::string()
        + "Commands:\n"
        + "  e2e4 / e7e8q  make a move (use q/r/b/n for promotion)\n"
        + "  board          print board\n"
        + "  moves          list legal moves for side to move\n"
        + "  score          print current game score in PGN format\n"
        + "  back           take 1 move back (2 plies)\n"
        + "  flip           switch side in current game\n"
        + "  restart        restart game with current side\n"
        + "  getfen         print current position as FEN\n"
        + "  game [white|black]  start a new game and choose your side\n"
        + "  engine [weak|strong]  switch engine (default weak)\n"
        + "  fen <string>   set position from FEN\n"
        + "  help           show commands\n"
        + "  quit           no-op in web app\n";
}

std::string MoveToUci(fatpup::Move move)
{
    std::string out;
    out.reserve(5);
    out.push_back(static_cast<char>('a' + move.fields.src_col));
    out.push_back(static_cast<char>('1' + move.fields.src_row));
    out.push_back(static_cast<char>('a' + move.fields.dst_col));
    out.push_back(static_cast<char>('1' + move.fields.dst_row));

    if (move.fields.promoted_to != 0)
    {
        char promotion = 'q';
        switch (move.fields.promoted_to)
        {
        case fatpup::Rook: promotion = 'r'; break;
        case fatpup::Bishop: promotion = 'b'; break;
        case fatpup::Knight: promotion = 'n'; break;
        default: promotion = 'q'; break;
        }
        out.push_back(promotion);
    }

    return out;
}

std::string BoardToText(const fatpup::Position& position)
{
    std::ostringstream out;

    for (int row = fatpup::BOARD_SIZE - 1; row >= 0; --row)
    {
        out << (row + 1) << " ";
        for (int col = 0; col < fatpup::BOARD_SIZE; ++col)
        {
            const fatpup::Square square = position.square(row, col);
            char piece = PieceToFenChar(square);
            if (piece == 0)
            {
                piece = ((row + col) & 1) ? '#' : '.';
            }
            out << piece << " ";
        }
        out << "\n";
    }
    out << "  a b c d e f g h";

    return out.str();
}

void PushMoveText(const fatpup::Position& positionBeforeMove, fatpup::Move move, std::vector<std::string>* white, std::vector<std::string>* black)
{
    const std::string moveText = positionBeforeMove.moveToStringPGN(move);
    if (positionBeforeMove.isWhiteTurn())
    {
        white->push_back(moveText);
    }
    else
    {
        black->push_back(moveText);
    }
}

void CommitMove(AppState* app, fatpup::Move move)
{
    PushMoveText(app->position, move, &app->whiteMoves, &app->blackMoves);
    ApplyMoveAndTrackCounters(&app->position, move, &app->halfMoveClock, &app->fullMoveNumber);
    app->moveHistory.push_back(move);
    app->lastMove = move;
    app->hasLastMove = true;
}

void ClearRenderedHistory(AppState* app)
{
    app->whiteMoves.clear();
    app->blackMoves.clear();
    app->hasLastMove = false;
    app->lastMove.setEmpty();
}

void ClearRecordedGame(AppState* app)
{
    app->moveHistory.clear();
    ClearRenderedHistory(app);
}

void RebuildFromHistory(AppState* app)
{
    app->position = app->startPosition;
    app->halfMoveClock = app->startHalfMoveClock;
    app->fullMoveNumber = app->startFullMoveNumber;
    ClearRenderedHistory(app);

    for (const fatpup::Move move : app->moveHistory)
    {
        PushMoveText(app->position, move, &app->whiteMoves, &app->blackMoves);
        ApplyMoveAndTrackCounters(&app->position, move, &app->halfMoveClock, &app->fullMoveNumber);
        app->lastMove = move;
        app->hasLastMove = true;
    }
}

void ResetGame(AppState* app, bool userPlaysWhite)
{
    app->userPlaysWhite = userPlaysWhite;
    app->position.setInitial();
    app->startPosition = app->position;

    app->startHalfMoveClock = 0;
    app->startFullMoveNumber = 1;
    app->halfMoveClock = app->startHalfMoveClock;
    app->fullMoveNumber = app->startFullMoveNumber;

    ClearRecordedGame(app);
}

fatpup::Move PickEngineMove(AppState* app)
{
    if (app->engineMode == EngineMode::Strong)
    {
        return search::FindBestMove(app->position);
    }

    if (!app->weakEngine)
    {
        app->weakEngine.reset(fatpup::Engine::Create("minimax"));
    }
    if (!app->weakEngine)
    {
        return fatpup::Move();
    }

    app->weakEngine->SetPosition(app->position);
    return app->weakEngine->GetBestMove();
}

std::string AdvanceEngineIfNeeded(AppState* app)
{
    std::ostringstream out;

    while (!IsGameOverPosition(app->position) && !IsUserToMove(*app))
    {
        out << (app->position.isWhiteTurn() ? "White" : "Black") << " (engine) is thinking...\n";

        const fatpup::Move engineMove = PickEngineMove(app);
        if (engineMove.isEmpty())
        {
            out << "Engine has no legal move.\n";
            break;
        }

        const bool engineMoveByWhite = app->position.isWhiteTurn();
        const std::string moveText = app->position.moveToString(engineMove);

        CommitMove(app, engineMove);

        out << (engineMoveByWhite ? "White" : "Black") << " played: " << moveText << "\n";
    }

    const std::string status = StatusText(app->position);
    if (!status.empty())
    {
        out << status << "\n";
    }

    return out.str();
}

std::string JsonEscape(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 16);
    for (const char c : text)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buf[7] = {};
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                out += buf;
            }
            else
            {
                out.push_back(c);
            }
            break;
        }
    }
    return out;
}

std::string BoardFlat64(const fatpup::Position& position)
{
    std::string board;
    board.reserve(64);

    for (int row = fatpup::BOARD_SIZE - 1; row >= 0; --row)
    {
        for (int col = 0; col < fatpup::BOARD_SIZE; ++col)
        {
            char piece = PieceToFenChar(position.square(row, col));
            board.push_back(piece == 0 ? '.' : piece);
        }
    }

    return board;
}

std::string LegalMovesText(const fatpup::Position& position)
{
    const std::vector<fatpup::Move> moves = position.possibleMoves();
    if (moves.empty())
    {
        return "No legal moves.";
    }

    std::ostringstream out;
    out << "Legal moves (" << moves.size() << "): ";
    for (std::size_t i = 0; i < moves.size(); ++i)
    {
        if (i)
        {
            out << ", ";
        }
        out << position.moveToString(moves[i]);
    }

    return out.str();
}

bool EnsureInitialized(std::string* /*error*/)
{
    if (g_app.initialized)
    {
        return true;
    }

    ResetGame(&g_app, true);
    g_app.initialized = true;
    return true;
}

std::string ExecuteCommand(const std::string& rawInput)
{
    std::string initError;
    if (!EnsureInitialized(&initError))
    {
        return initError;
    }

    const std::string trimmed = Trim(rawInput);
    if (trimmed.empty())
    {
        return std::string();
    }

    const std::string command = ToLower(trimmed);

    if (command == "quit" || command == "exit")
    {
        return "quit: no-op in web app.";
    }
    if (command == "help")
    {
        return HelpText();
    }
    if (command == "board")
    {
        return BoardToText(g_app.position);
    }
    if (command == "moves")
    {
        return LegalMovesText(g_app.position);
    }
    if (command == "score")
    {
        return GameScoreWithTags(g_app);
    }
    if (command == "back")
    {
        if (g_app.moveHistory.empty())
        {
            return "No moves to take back.";
        }

        const std::size_t pliesToRevert = std::min<std::size_t>(2, g_app.moveHistory.size());
        g_app.moveHistory.resize(g_app.moveHistory.size() - pliesToRevert);
        RebuildFromHistory(&g_app);

        std::ostringstream out;
        out << "Reverted " << pliesToRevert << " ply.";
        return out.str();
    }
    if (command == "getfen")
    {
        return PositionToFen(g_app.position, g_app.halfMoveClock, g_app.fullMoveNumber);
    }
    if (command == "restart")
    {
        const bool playWhite = g_app.userPlaysWhite;
        ResetGame(&g_app, playWhite);

        std::ostringstream out;
        out << "New game started. You play " << (playWhite ? "White" : "Black") << ".\n";
        out << AdvanceEngineIfNeeded(&g_app);
        return out.str();
    }
    if (command == "engine" || (command.size() > 7 && command.substr(0, 7) == "engine "))
    {
        const std::string mode = (command.size() > 7) ? Trim(command.substr(7)) : std::string();
        if (mode == "weak")
        {
            g_app.engineMode = EngineMode::Weak;
        }
        else if (mode == "strong")
        {
            g_app.engineMode = EngineMode::Strong;
        }
        else if (!mode.empty())
        {
            return "Usage: engine [weak|strong]";
        }

        std::ostringstream out;
        out << "Engine: " << (g_app.engineMode == EngineMode::Weak ? "Weak" : "Strong") << ".\n";
        out << AdvanceEngineIfNeeded(&g_app);
        return out.str();
    }

    if (command == "flip" || command == "flip sides" || command == "flipsides")
    {
        g_app.userPlaysWhite = !g_app.userPlaysWhite;

        std::ostringstream out;
        out << "You now play " << (g_app.userPlaysWhite ? "White" : "Black") << ".\n";
        out << AdvanceEngineIfNeeded(&g_app);
        return out.str();
    }

    if (command == "game" || (command.size() > 5 && command.substr(0, 5) == "game "))
    {
        const std::string side = Trim(command.substr(4));
        bool playWhite = true;
        if (side.empty() || side == "white")
        {
            playWhite = true;
        }
        else if (side == "black")
        {
            playWhite = false;
        }
        else
        {
            return "Usage: game [white|black]";
        }

        ResetGame(&g_app, playWhite);

        std::ostringstream out;
        out << "New game started. You play " << (playWhite ? "White" : "Black") << ".\n";
        out << AdvanceEngineIfNeeded(&g_app);
        return out.str();
    }

    if (command.size() > 4 && command.substr(0, 4) == "fen ")
    {
        const std::string fen = Trim(rawInput.substr(4));

        fatpup::Position newPosition;
        if (!newPosition.setFEN(fen))
        {
            return "Invalid FEN.";
        }

        g_app.position = newPosition;
        g_app.startPosition = g_app.position;
        g_app.userPlaysWhite = g_app.position.isWhiteTurn();
        ClearRecordedGame(&g_app);

        ParseFenCounters(fen, &g_app.startHalfMoveClock, &g_app.startFullMoveNumber);
        g_app.halfMoveClock = g_app.startHalfMoveClock;
        g_app.fullMoveNumber = g_app.startFullMoveNumber;

        std::ostringstream out;
        out << "Position set from FEN.\n";
        out << AdvanceEngineIfNeeded(&g_app);
        return out.str();
    }

    if (IsGameOverPosition(g_app.position))
    {
        return "Game is over. Use 'game [white|black]' or 'fen <string>'.";
    }

    if (!IsUserToMove(g_app))
    {
        return "It's engine's turn.";
    }

    fatpup::Move userMove;
    std::string parseError;
    if (!ParseAndResolveMove(g_app.position, rawInput, &userMove, &parseError))
    {
        return parseError;
    }

    const bool userMoveByWhite = g_app.position.isWhiteTurn();
    const std::string moveText = g_app.position.moveToString(userMove);

    CommitMove(&g_app, userMove);

    std::ostringstream out;
    out << (userMoveByWhite ? "White" : "Black") << " played: " << moveText << "\n";
    out << AdvanceEngineIfNeeded(&g_app);
    return out.str();
}

std::string BuildStateJson()
{
    std::string initError;
    if (!EnsureInitialized(&initError))
    {
        return std::string("{\"error\":\"") + JsonEscape(initError) + "\"}";
    }

    const std::string status = StatusText(g_app.position);
    const std::string board = BoardFlat64(g_app.position);
    const std::string fen = PositionToFen(g_app.position, g_app.halfMoveClock, g_app.fullMoveNumber);

    std::ostringstream out;
    out << "{";
    out << "\"board\":\"" << JsonEscape(board) << "\",";
    out << "\"fen\":\"" << JsonEscape(fen) << "\",";
    out << "\"status\":\"" << JsonEscape(status) << "\",";
    out << "\"gameOver\":" << (IsGameOverPosition(g_app.position) ? "true" : "false") << ",";
    out << "\"whiteTurn\":" << (g_app.position.isWhiteTurn() ? "true" : "false") << ",";
    out << "\"userPlaysWhite\":" << (g_app.userPlaysWhite ? "true" : "false") << ",";
    out << "\"userToMove\":" << (IsUserToMove(g_app) ? "true" : "false") << ",";
    out << "\"engineMode\":\"" << (g_app.engineMode == EngineMode::Weak ? "weak" : "strong") << "\",";
    out << "\"startWhiteTurn\":" << (g_app.startPosition.isWhiteTurn() ? "true" : "false") << ",";
    out << "\"startFullMoveNumber\":" << std::max(1, g_app.startFullMoveNumber) << ",";
    out << "\"lastMove\":\"" << JsonEscape(g_app.hasLastMove ? MoveToUci(g_app.lastMove) : std::string()) << "\",";

    out << "\"whiteMoves\":[";
    for (std::size_t i = 0; i < g_app.whiteMoves.size(); ++i)
    {
        if (i)
        {
            out << ",";
        }
        out << "\"" << JsonEscape(g_app.whiteMoves[i]) << "\"";
    }
    out << "],";

    out << "\"blackMoves\":[";
    for (std::size_t i = 0; i < g_app.blackMoves.size(); ++i)
    {
        if (i)
        {
            out << ",";
        }
        out << "\"" << JsonEscape(g_app.blackMoves[i]) << "\"";
    }
    out << "]";

    out << "}";
    return out.str();
}

} // namespace

extern "C"
{
EMSCRIPTEN_KEEPALIVE const char* fp_init()
{
    std::string initError;
    if (!EnsureInitialized(&initError))
    {
        g_resultBuffer = initError;
        return g_resultBuffer.c_str();
    }

    g_resultBuffer = "Fatpup Web initialized.\n" + HelpText();
    return g_resultBuffer.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* fp_command(const char* input)
{
    g_resultBuffer = ExecuteCommand(input ? input : "");
    return g_resultBuffer.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* fp_get_state_json()
{
    g_stateBuffer = BuildStateJson();
    return g_stateBuffer.c_str();
}
}
