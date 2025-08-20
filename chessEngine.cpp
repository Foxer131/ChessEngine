#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <cmath>
#include <algorithm>
#include <thread>
#include <future>
#include <random>
#include <unordered_map>
#include <cctype>   // <-- for tolower/isupper

using namespace std;

class IMoveable;
class Board;
class Engine;

enum Color { WHITE = 0, BLACK = 1, NONE = 2 };

// 0..5 fixed ordering for speed & Zobrist
enum PieceTypeIdx { 
    PT_PAWN=0, 
    PT_KNIGHT=1, 
    PT_BISHOP=2, 
    PT_ROOK=3, 
    PT_QUEEN=4, 
    PT_KING=5 
};

struct Position {
    int row, col;
    bool operator==(const Position& other) const { return row == other.row && col == other.col; }
};

struct Move {
    Position from;
    Position to;
    char promotionPiece = ' '; // 'q', 'r', 'b', 'n'
};

class IMoveable {
public:
    virtual ~IMoveable() = default;
    virtual vector<Move> getValidMoves(const Position& position, const Board& board) const = 0;
    virtual Color getColor() const = 0;
    virtual char getSymbol() const = 0;
    virtual unique_ptr<IMoveable> clone() const = 0;
    // Fast piece type (0..5), avoids dynamic_cast in hot paths
    virtual int type() const = 0;
};

class King; class Queen; class Rook; class Bishop; class Knight; class Pawn;

class Zobrist {
public:
    static uint64_t pieceKeys[2][6][64];
    static uint64_t sideKey;
    static uint64_t castleKeys[16];
    static uint64_t enPassantKeys[8];

    static void init() {
        mt19937_64 gen(12345); // deterministic
        uniform_int_distribution<uint64_t> dist;
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 6; ++j)
                for (int k = 0; k < 64; ++k)
                    pieceKeys[i][j][k] = dist(gen);
        sideKey = dist(gen);
        for (int i = 0; i < 16; ++i) castleKeys[i] = dist(gen);
        for (int i = 0; i < 8;  ++i) enPassantKeys[i] = dist(gen);
    }
};
uint64_t Zobrist::pieceKeys[2][6][64];
uint64_t Zobrist::sideKey;
uint64_t Zobrist::castleKeys[16];
uint64_t Zobrist::enPassantKeys[8];

class Board {
private:
    vector<vector<unique_ptr<IMoveable>>> grid;
    bool whiteKingMoved = false, blackKingMoved = false;
    bool whiteRookAMoved = false, whiteRookHMoved = false;
    bool blackRookAMoved = false, blackRookHMoved = false;
    Position enPassantTargetSquare = {-1, -1};
    uint64_t hashKey = 0;

public:
    Board() {
        grid.resize(8);
        for (auto& row : grid) row.resize(8);
    }

    Board(const Board& other) {
        grid.resize(8);
        for (int r = 0; r < 8; ++r) {
            grid[r].resize(8);
            for (int c = 0; c < 8; ++c)
                grid[r][c] = other.grid[r][c] ? other.grid[r][c]->clone() : nullptr;
        }
        whiteKingMoved = other.whiteKingMoved;
        blackKingMoved = other.blackKingMoved;
        whiteRookAMoved = other.whiteRookAMoved;
        whiteRookHMoved = other.whiteRookHMoved;
        blackRookAMoved = other.blackRookAMoved;
        blackRookHMoved = other.blackRookHMoved;
        enPassantTargetSquare = other.enPassantTargetSquare;
        hashKey = other.hashKey;
    }

    void setupBoard();
    void generateHashKey(Color sideToMove);

    void display() const {
        cout << "  a b c d e f g h\n";
        cout << " +-----------------+\n";
        for (int r = 0; r < 8; ++r) {
            cout << 8 - r << "| ";
            for (int c = 0; c < 8; ++c) {
                cout << (grid[r][c] ? grid[r][c]->getSymbol() : '.') << " ";
            }
            cout << "|\n";
        }
        cout << " +-----------------+\n";
    }

    bool isValidPosition(const Position& position) const {
        return position.row >= 0 && position.row < 8 && position.col >= 0 && position.col < 8;
    }

    Color getPieceColor(const Position& position) const {
        if (isValidPosition(position) && grid[position.row][position.col]) {
            return grid[position.row][position.col]->getColor();
        }
        return NONE;
    }

    IMoveable* getPiece(const Position& position) const {
        if (isValidPosition(position)) return grid[position.row][position.col].get();
        return nullptr;
    }

    void setPiece(const Position& pos, unique_ptr<IMoveable> piece) {
        if (isValidPosition(pos)) grid[pos.row][pos.col] = std::move(piece);
    }

    void makeMove(const Move& move);
    Position findKing(Color kingColor) const;
    bool isKingInCheck(Color kingColor) const;
    bool isSquareAttackedBy(const Position& pos, Color attackerColor) const;
    vector<Move> getAllLegalMoves(Color playerColor, bool capturesOnly);

    bool canWhiteCastleKingside()  const { return !whiteKingMoved && !whiteRookHMoved; }
    bool canWhiteCastleQueenside() const { return !whiteKingMoved && !whiteRookAMoved; }
    bool canBlackCastleKingside()  const { return !blackKingMoved && !blackRookHMoved; }
    bool canBlackCastleQueenside() const { return !blackKingMoved && !blackRookAMoved; }
    Position getEnPassantTarget() const { return enPassantTargetSquare; }
    uint64_t getHashKey() const { return hashKey; }
};

class Pawn : public IMoveable {
private:
    Color color;
public:
    explicit Pawn(Color c) : color(c) {}
    Color getColor() const override { return color; }
    char getSymbol() const override { return (color == WHITE) ? 'P' : 'p'; }
    int type() const override { return PT_PAWN; }
    unique_ptr<IMoveable> clone() const override { return make_unique<Pawn>(*this); }

    vector<Move> getValidMoves(const Position& position, const Board& board) const override {
        vector<Move> validMoves;
        validMoves.reserve(8);
        int direction = (color == WHITE) ? -1 : 1;
        int promotionRank = (color == WHITE) ? 0 : 7;

        Position oneStep = { position.row + direction, position.col };
        if (board.isValidPosition(oneStep) && board.getPieceColor(oneStep) == NONE) {
            if (oneStep.row == promotionRank) {
                validMoves.push_back({position, oneStep, 'q'});
                validMoves.push_back({position, oneStep, 'r'});
                validMoves.push_back({position, oneStep, 'b'});
                validMoves.push_back({position, oneStep, 'n'});
            } else {
                validMoves.push_back({ position, oneStep });
            }

            bool isFirstMove = (color == WHITE && position.row == 6) || (color == BLACK && position.row == 1);
            if (isFirstMove) {
                Position twoSteps = { position.row + 2 * direction, position.col };
                if (board.isValidPosition(twoSteps) && board.getPieceColor(twoSteps) == NONE) {
                    validMoves.push_back({ position, twoSteps });
                }
            }
        }

        for (int dCol = -1; dCol <= 1; dCol += 2) {
            Position capPos = { position.row + direction, position.col + dCol };
            if (board.isValidPosition(capPos) && board.getPieceColor(capPos) != NONE && board.getPieceColor(capPos) != color) {
                if (capPos.row == promotionRank) {
                    validMoves.push_back({position, capPos, 'q'});
                    validMoves.push_back({position, capPos, 'r'});
                    validMoves.push_back({position, capPos, 'b'});
                    validMoves.push_back({position, capPos, 'n'});
                } else {
                    validMoves.push_back({ position, capPos });
                }
            }
        }

        Position enPassantTarget = board.getEnPassantTarget();
        if (enPassantTarget.row != -1) {
            if (enPassantTarget.row == position.row + direction && abs(enPassantTarget.col - position.col) == 1) {
                validMoves.push_back({position, enPassantTarget});
            }
        }

        return validMoves;
    }
};

class Rook : public IMoveable {
private:
    Color color;
    void addSlidingMoves(const Position& position, const Board& board, int dRow, int dCol, vector<Move>& moves) const {
        Position current = position;
        while (true) {
            current.row += dRow;
            current.col += dCol;
            if (!board.isValidPosition(current)) break;
            Color targetColor = board.getPieceColor(current);
            if (targetColor == NONE) {
                moves.push_back({ position, current });
            } else {
                if (targetColor != this->color) moves.push_back({ position, current });
                break;
            }
        }
    }
public:
    explicit Rook(Color c) : color(c) {}
    Color getColor() const override { return color; }
    char getSymbol() const override { return (color == WHITE) ? 'R' : 'r'; }
    int type() const override { return PT_ROOK; }
    unique_ptr<IMoveable> clone() const override { return make_unique<Rook>(*this); }

    vector<Move> getValidMoves(const Position& position, const Board& board) const override {
        vector<Move> validMoves;
        validMoves.reserve(14);
        addSlidingMoves(position, board, 1, 0, validMoves);
        addSlidingMoves(position, board, -1, 0, validMoves);
        addSlidingMoves(position, board, 0, 1, validMoves);
        addSlidingMoves(position, board, 0, -1, validMoves);
        return validMoves;
    }
};

class Bishop : public IMoveable {
private:
    Color color;
public:
    explicit Bishop(Color c) : color(c) {}
    Color getColor() const override { return color; }
    char getSymbol() const override { return (color == WHITE) ? 'B' : 'b'; }
    int type() const override { return PT_BISHOP; }
    unique_ptr<IMoveable> clone() const override { return make_unique<Bishop>(*this); }

    vector<Move> getValidMoves(const Position& position, const Board& board) const override {
        vector<Move> validMoves;
        validMoves.reserve(13);
        for (int dRow = -1; dRow <= 1; dRow += 2) {
            for (int dCol = -1; dCol <= 1; dCol += 2) {
                Position current = position;
                while (true) {
                    current.row += dRow;
                    current.col += dCol;
                    if (!board.isValidPosition(current)) break;
                    Color pieceColor = board.getPieceColor(current);
                    if (pieceColor == NONE) {
                        validMoves.push_back({ position, current });
                    } else {
                        if (pieceColor != color) validMoves.push_back({ position, current });
                        break;
                    }
                }
            }
        }
        return validMoves;
    }
};

class Knight : public IMoveable {
private:
    Color color;
public:
    explicit Knight(Color c) : color(c) {}
    Color getColor() const override { return color; }
    char getSymbol() const override { return (color == WHITE) ? 'N' : 'n'; }
    int type() const override { return PT_KNIGHT; }
    unique_ptr<IMoveable> clone() const override { return make_unique<Knight>(*this); }

    vector<Move> getValidMoves(const Position& position, const Board& board) const override {
        vector<Move> validMoves;
        validMoves.reserve(8);
        const int knightMoves[8][2] = {
            {2, 1}, {2, -1}, {1, 2}, {1, -2},
            {-2, 1}, {-2, -1}, {-1, 2}, {-1, -2}
        };
        for (const auto& move : knightMoves) {
            Position newPos = { position.row + move[0], position.col + move[1] };
            if (board.isValidPosition(newPos)) {
                if (board.getPieceColor(newPos) != color) validMoves.push_back({ position, newPos });
            }
        }
        return validMoves;
    }
};

class Queen : public IMoveable {
private:
    Color color;
    void getMoves(const Position& position, const Board& board, vector<Move>& validMoves, int dRow, int dCol) const {
        Position current = position;
        while (true) {
            current.row += dRow;
            current.col += dCol;
            if (!board.isValidPosition(current)) break;
            Color pieceColor = board.getPieceColor(current);
            if (pieceColor == NONE) {
                validMoves.push_back({ position, current });
            } else {
                if (pieceColor != color) validMoves.push_back({ position, current });
                break;
            }
        }
    }
public:
    explicit Queen(Color c) : color(c) {}
    Color getColor() const override { return color; }
    char getSymbol() const override { return (color == WHITE) ? 'Q' : 'q'; }
    int type() const override { return PT_QUEEN; }
    unique_ptr<IMoveable> clone() const override { return make_unique<Queen>(*this); }

    vector<Move> getValidMoves(const Position& position, const Board& board) const override {
        vector<Move> validMoves;
        validMoves.reserve(27);
        for (int dRow = -1; dRow <= 1; ++dRow) {
            for (int dCol = -1; dCol <= 1; ++dCol) {
                if (dRow == 0 && dCol == 0) continue;
                getMoves(position, board, validMoves, dRow, dCol);
            }
        }
        return validMoves;
    }
};

class King : public IMoveable {
private:
    Color color;

    void addCastlingMoves(const Position& position, const Board& board, vector<Move>& validMoves) const;
public:
    explicit King(Color c) : color(c) {}
    Color getColor() const override { return color; }
    char getSymbol() const override { return (color == WHITE) ? 'K' : 'k'; }
    int type() const override { return PT_KING; }
    unique_ptr<IMoveable> clone() const override { return make_unique<King>(*this); }

    vector<Move> getValidMoves(const Position& position, const Board& board) const override {
        vector<Move> validMoves;
        validMoves.reserve(10);
        for (int dRow = -1; dRow <= 1; ++dRow) {
            for (int dCol = -1; dCol <= 1; ++dCol) {
                if (dRow == 0 && dCol == 0) continue;
                Position newPos = { position.row + dRow, position.col + dCol };
                if (board.isValidPosition(newPos) && board.getPieceColor(newPos) != color) {
                    validMoves.push_back({ position, newPos });
                }
            }
        }
        addCastlingMoves(position, board, validMoves);
        return validMoves;
    }
};

void King::addCastlingMoves(const Position& position, const Board& board, vector<Move>& validMoves) const {
    if (board.isKingInCheck(color)) return;
    Color opp = (color == WHITE) ? BLACK : WHITE;

    // King-side
    if ((color == WHITE && board.canWhiteCastleKingside()) || (color == BLACK && board.canBlackCastleKingside())) {
        if (board.getPiece({position.row, 5}) == nullptr && board.getPiece({position.row, 6}) == nullptr) {
            if (!board.isSquareAttackedBy({position.row, 5}, opp) && !board.isSquareAttackedBy({position.row, 6}, opp)) {
                validMoves.push_back({position, {position.row, 6}});
            }
        }
    }
    // Queen-side
    if ((color == WHITE && board.canWhiteCastleQueenside()) || (color == BLACK && board.canBlackCastleQueenside())) {
        if (board.getPiece({position.row, 1}) == nullptr && board.getPiece({position.row, 2}) == nullptr && board.getPiece({position.row, 3}) == nullptr) {
            if (!board.isSquareAttackedBy({position.row, 2}, opp) && !board.isSquareAttackedBy({position.row, 3}, opp)) {
                validMoves.push_back({position, {position.row, 2}});
            }
        }
    }
}

void Board::generateHashKey(Color sideToMove) {
    hashKey = 0;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            IMoveable* piece = getPiece({r, c});
            if (piece) hashKey ^= Zobrist::pieceKeys[piece->getColor()][piece->type()][r * 8 + c];
        }
    }
    if (enPassantTargetSquare.col != -1) hashKey ^= Zobrist::enPassantKeys[enPassantTargetSquare.col];
    int castleRights = (canWhiteCastleKingside() << 3) | (canWhiteCastleQueenside() << 2) | (canBlackCastleKingside() << 1) | canBlackCastleQueenside();
    hashKey ^= Zobrist::castleKeys[castleRights];
    if (sideToMove == BLACK) hashKey ^= Zobrist::sideKey;
}

void Board::setupBoard() {
    // Black
    grid[0][0] = make_unique<Rook>(BLACK);
    grid[0][1] = make_unique<Knight>(BLACK);
    grid[0][2] = make_unique<Bishop>(BLACK);
    grid[0][3] = make_unique<Queen>(BLACK);
    grid[0][4] = make_unique<King>(BLACK);
    grid[0][5] = make_unique<Bishop>(BLACK);
    grid[0][6] = make_unique<Knight>(BLACK);
    grid[0][7] = make_unique<Rook>(BLACK);
    for (int c = 0; c < 8; ++c) grid[1][c] = make_unique<Pawn>(BLACK);

    // White
    grid[7][0] = make_unique<Rook>(WHITE);
    grid[7][1] = make_unique<Knight>(WHITE);
    grid[7][2] = make_unique<Bishop>(WHITE);
    grid[7][3] = make_unique<Queen>(WHITE);
    grid[7][4] = make_unique<King>(WHITE);
    grid[7][5] = make_unique<Bishop>(WHITE);
    grid[7][6] = make_unique<Knight>(WHITE);
    grid[7][7] = make_unique<Rook>(WHITE);
    for (int c = 0; c < 8; ++c) grid[6][c] = make_unique<Pawn>(WHITE);

    generateHashKey(WHITE);
}

Position Board::findKing(Color kingColor) const {
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) {
            IMoveable* piece = getPiece({r, c});
            if (piece && piece->getColor() == kingColor && piece->type() == PT_KING) return {r, c};
        }
    return {-1, -1};
}

bool Board::isSquareAttackedBy(const Position& pos, Color attackerColor) const {
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            Position currentPos = {r, c};
            IMoveable* piece = getPiece(currentPos);
            if (!piece || piece->getColor() != attackerColor) continue;

            int t = piece->type();
            if (t == PT_PAWN) {
                int dir = (attackerColor == WHITE) ? -1 : 1;
                if (pos.row == r + dir && (pos.col == c - 1 || pos.col == c + 1)) return true;
            } else if (t == PT_KING) {
                if (abs(pos.row - r) <= 1 && abs(pos.col - c) <= 1) return true;
            } else {
                vector<Move> moves = piece->getValidMoves(currentPos, *this);
                for (const auto& m : moves) if (m.to == pos) return true;
            }
        }
    }
    return false;
}

bool Board::isKingInCheck(Color kingColor) const {
    Position kingPos = findKing(kingColor);
    if (kingPos.row == -1) return false;
    Color opp = (kingColor == WHITE) ? BLACK : WHITE;
    return isSquareAttackedBy(kingPos, opp);
}

void Board::makeMove(const Move& move) {
    int oldCastleRights = (canWhiteCastleKingside() << 3) | (canWhiteCastleQueenside() << 2) | (canBlackCastleKingside() << 1) | canBlackCastleQueenside();
    if (enPassantTargetSquare.col != -1) hashKey ^= Zobrist::enPassantKeys[enPassantTargetSquare.col];

    IMoveable* piece = getPiece(move.from);
    IMoveable* capturedPiece = getPiece(move.to);
    Color pieceColor = piece->getColor();

    // Remove captured piece from hash
    if (capturedPiece) {
        hashKey ^= Zobrist::pieceKeys[capturedPiece->getColor()][capturedPiece->type()][move.to.row * 8 + move.to.col];
    }
    // Move piece in hash
    hashKey ^= Zobrist::pieceKeys[pieceColor][piece->type()][move.from.row * 8 + move.from.col];
    hashKey ^= Zobrist::pieceKeys[pieceColor][piece->type()][move.to.row * 8 + move.to.col];

    Position oldEnPassantTarget = enPassantTargetSquare;
    enPassantTargetSquare = {-1, -1};

    // Castling: move rook + update hash for rook
    if (piece->type() == PT_KING && abs(move.to.col - move.from.col) == 2) {
        if (move.to.col == 6) { // king-side
            grid[move.from.row][5] = std::move(grid[move.from.row][7]);
            IMoveable* rook = getPiece({move.from.row, 5});
            hashKey ^= Zobrist::pieceKeys[pieceColor][rook->type()][move.from.row * 8 + 7];
            hashKey ^= Zobrist::pieceKeys[pieceColor][rook->type()][move.from.row * 8 + 5];
        } else { // queen-side
            grid[move.from.row][3] = std::move(grid[move.from.row][0]);
            IMoveable* rook = getPiece({move.from.row, 3});
            hashKey ^= Zobrist::pieceKeys[pieceColor][rook->type()][move.from.row * 8 + 0];
            hashKey ^= Zobrist::pieceKeys[pieceColor][rook->type()][move.from.row * 8 + 3];
        }
    }

    // En-passant capture
    if (piece->type() == PT_PAWN) {
        if (move.to == oldEnPassantTarget) {
            int capturedPawnRow = (pieceColor == WHITE) ? move.to.row + 1 : move.to.row - 1;
            IMoveable* cap = getPiece({capturedPawnRow, move.to.col});
            if (cap) {
                hashKey ^= Zobrist::pieceKeys[cap->getColor()][cap->type()][capturedPawnRow * 8 + move.to.col];
                grid[capturedPawnRow][move.to.col] = nullptr;
            }
        }
        if (abs(move.from.row - move.to.row) == 2) {
            enPassantTargetSquare = { (move.from.row + move.to.row) / 2, move.from.col };
            hashKey ^= Zobrist::enPassantKeys[enPassantTargetSquare.col];
        }
    }

    // Update castle rights by piece movement/capture
    if (move.from.row == 7 && move.from.col == 4) whiteKingMoved = true;
    if (move.from.row == 0 && move.from.col == 4) blackKingMoved = true;
    if (move.from.row == 7 && move.from.col == 0) whiteRookAMoved = true;
    if (move.from.row == 7 && move.from.col == 7) whiteRookHMoved = true;
    if (move.from.row == 0 && move.from.col == 0) blackRookAMoved = true;
    if (move.from.row == 0 && move.from.col == 7) blackRookHMoved = true;

    if (move.to.row == 7 && move.to.col == 0) whiteRookAMoved = true;
    if (move.to.row == 7 && move.to.col == 7) whiteRookHMoved = true;
    if (move.to.row == 0 && move.to.col == 0) blackRookAMoved = true;
    if (move.to.row == 0 && move.to.col == 7) blackRookHMoved = true;

    // Physically move the piece
    grid[move.to.row][move.to.col] = std::move(grid[move.from.row][move.from.col]);

    // Promotion
    if (move.promotionPiece != ' ') {
        IMoveable* moved = getPiece(move.to); // pawn currently there
        hashKey ^= Zobrist::pieceKeys[pieceColor][moved->type()][move.to.row * 8 + move.to.col]; // remove pawn
        unique_ptr<IMoveable> newPiece;
        switch (move.promotionPiece) {
            case 'q': newPiece = make_unique<Queen>(pieceColor); break;
            case 'r': newPiece = make_unique<Rook>(pieceColor);  break;
            case 'b': newPiece = make_unique<Bishop>(pieceColor);break;
            case 'n': newPiece = make_unique<Knight>(pieceColor);break;
            default:  newPiece = make_unique<Queen>(pieceColor); break;
        }
        hashKey ^= Zobrist::pieceKeys[pieceColor][newPiece->type()][move.to.row * 8 + move.to.col]; // add new piece
        setPiece(move.to, std::move(newPiece));
    }

    int newCastleRights = (canWhiteCastleKingside() << 3) | (canWhiteCastleQueenside() << 2) | (canBlackCastleKingside() << 1) | canBlackCastleQueenside();
    if (oldCastleRights != newCastleRights) {
        hashKey ^= Zobrist::castleKeys[oldCastleRights];
        hashKey ^= Zobrist::castleKeys[newCastleRights];
    }

    // Flip side to move
    hashKey ^= Zobrist::sideKey;
}

vector<Move> Board::getAllLegalMoves(Color playerColor, bool capturesOnly) {
    vector<Move> legalMoves;
    legalMoves.reserve(256);

    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            IMoveable* piece = getPiece({r, c});
            if (!piece || piece->getColor() != playerColor) continue;

            vector<Move> moves = piece->getValidMoves({r, c}, *this);
            for (const auto& move : moves) {
                // Correct capture-only filtering (includes en-passant & promos)
                bool isCapture = (getPiece(move.to) != nullptr);
                bool isEP = (piece->type() == PT_PAWN && move.to == getEnPassantTarget());
                bool isPromo = (move.promotionPiece != ' ');
                if (capturesOnly && !(isCapture || isEP || isPromo)) continue;

                Board tempBoard = *this;
                tempBoard.makeMove(move);
                if (!tempBoard.isKingInCheck(playerColor)) legalMoves.push_back(move);
            }
        }
    }
    return legalMoves;
}

class Game {
private:
    Board board;
    Color currentPlayer;
    unique_ptr<Engine> engine;
    unsigned int depthLimit = 5; // Default depth limit for engine search

    Move parseMove(const string& moveStr) {
        if (moveStr.length() < 4 || moveStr.length() > 5)
            return { { -1, -1 }, { -1, -1 } };
        int fromCol = moveStr[0] - 'a';
        int fromRow = 8 - (moveStr[1] - '0');
        int toCol   = moveStr[2] - 'a';
        int toRow   = 8 - (moveStr[3] - '0');
        char promotion = ' ';
        if (moveStr.length() == 5) promotion = (char)tolower((unsigned char)moveStr[4]);
        return { { fromRow, fromCol }, { toRow, toCol }, promotion };
    }

    bool hasLegalMoves(Color playerColor) { 
        return !board.getAllLegalMoves(playerColor, false).empty(); 
    }

    bool isCheckmate(Color kingColor) { 
        return board.isKingInCheck(kingColor) && !hasLegalMoves(kingColor); 
    }

    bool isStalemate(Color kingColor) { 
        return !board.isKingInCheck(kingColor) && !hasLegalMoves(kingColor); 
    }

    bool isDrawByInsufficientMaterial() const {
        int whiteKnights = 0, blackKnights = 0;
        int whiteBishops = 0, blackBishops = 0;
        int bishopSquareColor = -1;

        for (int r = 0; r < 8; ++r) {
            for (int c = 0; c < 8; ++c) {
                IMoveable* piece = board.getPiece({r, c});
                if (!piece || piece->type() == PT_KING) continue;

                int t = piece->type();
                if (t == PT_QUEEN || t == PT_ROOK || t == PT_PAWN) return false;

                if (t == PT_KNIGHT) {
                    if (piece->getColor() == WHITE) whiteKnights++; else blackKnights++;
                } else if (t == PT_BISHOP) {
                    if (piece->getColor() == WHITE) whiteBishops++; else blackBishops++;
                    int currentSquareColor = (r + c) % 2;
                    if (bishopSquareColor == -1) bishopSquareColor = currentSquareColor;
                    else if (bishopSquareColor != currentSquareColor) return false;
                }
            }
        }

        if (whiteKnights + blackKnights > 1) 
            return false;
        if (whiteKnights > 0 && (whiteBishops > 0 || blackBishops > 0)) 
            return false;
        if (blackKnights > 0 && (whiteBishops > 0 || blackBishops > 0)) 
            return false;

        return true;
    }

public:
    Game() : currentPlayer(WHITE) {
        Zobrist::init();
        board.setupBoard();
        engine = make_unique<Engine>(*this);
    }

    const Board& getBoard() const { return board; }
    Board& getBoardForEngine() { return board; }

    void run();
};

enum TTFlag { 
    EXACT, 
    LOWERBOUND, 
    UPPERBOUND 
};

struct TTEntry {
    uint64_t key = 0;
    int depth = 0;
    double score = 0.0;
    TTFlag flag = EXACT;
    Move bestMove = {{-1,-1},{-1,-1}};
};

class TranspositionTable {
    private:
        vector<TTEntry> table;
        size_t size;

    public:
        TranspositionTable(size_t mbSize) {
            size = max<size_t>(1, (mbSize * 1024ULL * 1024ULL) / sizeof(TTEntry));
            table.resize(size);
        }

        void store(uint64_t key, int depth, double score, TTFlag flag, Move bestMove) {
            size_t index = key % size;
            if (table[index].key == 0 || depth >= table[index].depth) {
                table[index] = {key, depth, score, flag, bestMove};
            }
        }

        bool probe(uint64_t key, int depth, double& score, TTFlag& flag, Move& bestMove) {
            size_t index = key % size;
            if (table[index].key == key && table[index].depth >= depth) {
                score = table[index].score;
                flag = table[index].flag;
                bestMove = table[index].bestMove;
                return true;
            }
            return false;
        }
};

class Engine {
    private:
        Game& game;
        TranspositionTable tt;

        // Piece values (pawns)
        static constexpr double pawnValue   = 1.0;
        static constexpr double knightValue = 3.2;
        static constexpr double bishopValue = 3.3;
        static constexpr double rookValue   = 5.0;
        static constexpr double queenValue  = 9.0;
        static constexpr double kingValue   = 200.0;
        static constexpr double doubledPawnPenalty  = -0.35;
        static constexpr double isolatedPawnPenalty = -0.20;
        const double passedPawnBonus[8] = { 0.0, 0.2, 0.4, 0.75, 1.25, 2.0, 3.0, 4.5 }; // Bonus increases as pawn advances


        // PSTs
        const double pawnTable[8][8] = {
            {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
            {0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5},
            {0.1, 0.1, 0.2, 0.3, 0.3, 0.2, 0.1, 0.1},
            {0.05, 0.05, 0.1, 0.25, 0.25, 0.1, 0.05, 0.05},
            {0.0, 0.0, 0.0, 0.2, 0.2, 0.0, 0.0, 0.0},
            {0.05, -0.05, -0.1, 0.0, 0.0, -0.1, -0.05, 0.05},
            {0.05, 0.1, 0.1, -0.2, -0.2, 0.1, 0.1, 0.05},
            {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
        };
        const double knightTable[8][8] = {
            {-0.5, -0.4, -0.3, -0.3, -0.3, -0.3, -0.4, -0.5},
            {-0.4, -0.2, 0.0, 0.0, 0.0, 0.0, -0.2, -0.4},
            {-0.3, 0.0, 0.1, 0.15, 0.15, 0.1, 0.0, -0.3},
            {-0.3, 0.05, 0.15, 0.2, 0.2, 0.15, 0.05, -0.3},
            {-0.3, 0.0, 0.15, 0.2, 0.2, 0.15, 0.0, -0.3},
            {-0.3, 0.05, 0.1, 0.15, 0.15, 0.1, 0.05, -0.3},
            {-0.4, -0.2, 0.0, 0.05, 0.05, 0.0, -0.2, -0.4},
            {-0.5, -0.4, -0.3, -0.3, -0.3, -0.3, -0.4, -0.5}
        };
        const double bishopTable[8][8] = {
            {-0.2, -0.1, -0.1, -0.1, -0.1, -0.1, -0.1, -0.2},
            {-0.1, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.1},
            {-0.1, 0.0, 0.05, 0.1, 0.1, 0.05, 0.0, -0.1},
            {-0.1, 0.05, 0.05, 0.1, 0.1, 0.05, 0.05, -0.1},
            {-0.1, 0.0, 0.1, 0.1, 0.1, 0.1, 0.0, -0.1},
            {-0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, -0.1},
            {-0.1, 0.05, 0.0, 0.0, 0.0, 0.0, 0.05, -0.1},
            {-0.2, -0.1, -0.1, -0.1, -0.1, -0.1, -0.1, -0.2}
        };
        const double rookTable[8][8] = {
            {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
            {0.05, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.05},
            {-0.05, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.05},
            {-0.05, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.05},
            {-0.05, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.05},
            {-0.05, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.05},
            {-0.05, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.05},
            {0.0, 0.0, 0.0, 0.05, 0.05, 0.0, 0.0, 0.0}
        };
        const double kingTableMidgame[8][8] = {
            {-0.3, -0.4, -0.4, -0.5, -0.5, -0.4, -0.4, -0.3},
            {-0.3, -0.4, -0.4, -0.5, -0.5, -0.4, -0.4, -0.3},
            {-0.3, -0.4, -0.4, -0.5, -0.5, -0.4, -0.4, -0.3},
            {-0.3, -0.4, -0.4, -0.5, -0.5, -0.4, -0.4, -0.3},
            {-0.2, -0.3, -0.3, -0.4, -0.4, -0.3, -0.3, -0.2},
            {-0.1, -0.2, -0.2, -0.2, -0.2, -0.2, -0.2, -0.1},
            {0.2, 0.2, 0.0, 0.0, 0.0, 0.0, 0.2, 0.2},
            {0.2, 0.3, 0.1, 0.0, 0.0, 0.1, 0.3, 0.2}
        };
        const double kingTableEndgame[8][8] = {
            {-0.5, -0.4, -0.3, -0.2, -0.2, -0.3, -0.4, -0.5},
            {-0.3, -0.2, -0.1, 0.0, 0.0, -0.1, -0.2, -0.3},
            {-0.3, -0.1, 0.2, 0.3, 0.3, 0.2, -0.1, -0.3},
            {-0.3, -0.1, 0.3, 0.4, 0.4, 0.3, -0.1, -0.3},
            {-0.3, -0.1, 0.3, 0.4, 0.4, 0.3, -0.1, -0.3},
            {-0.3, -0.1, 0.2, 0.3, 0.3, 0.2, -0.1, -0.3},
            {-0.3, -0.3, 0.0, 0.0, 0.0, 0.0, -0.3, -0.3},
            {-0.5, -0.3, -0.3, -0.3, -0.3, -0.3, -0.3, -0.5}
        };

        static int cpVal(int typeIdx) {
            static const int v[6] = {100, 320, 330, 500, 900, 20000};
            return v[typeIdx];
        }

        double getPieceValue(IMoveable* piece, int r, int c, bool isEndgame) const {
            if (!piece) return 0.0;
            int t = piece->type();
            Color col = piece->getColor();
            double base = 0.0, pst = 0.0;

            switch (t) {
                case PT_PAWN:   base = pawnValue;   pst = (col==WHITE) ? pawnTable[r][c] : pawnTable[7-r][c]; break;
                case PT_KNIGHT: base = knightValue; pst = (col==WHITE) ? knightTable[r][c] : knightTable[7-r][c]; break;
                case PT_BISHOP: base = bishopValue; pst = (col==WHITE) ? bishopTable[r][c] : bishopTable[7-r][c]; break;
                case PT_ROOK:   base = rookValue;   pst = (col==WHITE) ? rookTable[r][c] : rookTable[7-r][c]; break;
                case PT_QUEEN:  base = queenValue;  break;
                case PT_KING:
                    base = kingValue;
                    pst = (isEndgame ? ((col==WHITE) ? kingTableEndgame[r][c] : kingTableEndgame[7-r][c])
                                    : ((col==WHITE) ? kingTableMidgame[r][c] : kingTableMidgame[7-r][c]));
                    break;
            }
            return base + pst;
        }

        double evaluatePosition(Board& board) {
            double score = 0.0;
            int materialCount = 0;

            // NEW: Arrays to track pawns on each file for structure analysis
            int whitePawnsPerFile[8] = {0};
            int blackPawnsPerFile[8] = {0};

            for (int r = 0; r < 8; ++r) {
                for (int c = 0; c < 8; ++c) {
                    IMoveable* piece = board.getPiece({r, c});
                    if (!piece) continue;

                    if (piece->type() != PT_KING) {
                        materialCount++;
                    }

                    // NEW: Count pawns on each file
                    if (piece->type() == PT_PAWN) {
                        if (piece->getColor() == WHITE) {
                            whitePawnsPerFile[c]++;
                        } else {
                            blackPawnsPerFile[c]++;
                        }
                    }

                    double value = getPieceValue(piece, r, c, materialCount < 12);
                    score += (piece->getColor() == WHITE) ? value : -value;
                }
            }
            
            // NEW: Pawn structure evaluation
            double pawnStructureScore = 0.0;
            for (int c = 0; c < 8; ++c) {
                // 1. Doubled Pawns Penalty
                if (whitePawnsPerFile[c] > 1) pawnStructureScore += (whitePawnsPerFile[c] - 1) * doubledPawnPenalty;
                if (blackPawnsPerFile[c] > 1) pawnStructureScore -= (blackPawnsPerFile[c] - 1) * doubledPawnPenalty;

                // 2. Isolated Pawns Penalty
                bool leftEmptyW = (c == 0) || (whitePawnsPerFile[c - 1] == 0);
                bool rightEmptyW = (c == 7) || (whitePawnsPerFile[c + 1] == 0);
                if (whitePawnsPerFile[c] > 0 && leftEmptyW && rightEmptyW) {
                    pawnStructureScore += isolatedPawnPenalty;
                }

                bool leftEmptyB = (c == 0) || (blackPawnsPerFile[c - 1] == 0);
                bool rightEmptyB = (c == 7) || (blackPawnsPerFile[c + 1] == 0);
                if (blackPawnsPerFile[c] > 0 && leftEmptyB && rightEmptyB) {
                    pawnStructureScore -= isolatedPawnPenalty;
                }
            }
            
            // 3. Passed Pawns Bonus
            for (int r = 0; r < 8; ++r) {
                for (int c = 0; c < 8; ++c) {
                    IMoveable* piece = board.getPiece({r, c});
                    if (!piece || piece->type() != PT_PAWN) continue;

                    bool isPassed = true;
                    if (piece->getColor() == WHITE) {
                        // Check files in front for enemy pawns
                        for (int lookAheadRow = r - 1; lookAheadRow >= 0; --lookAheadRow) {
                            if (c > 0 && board.getPiece({lookAheadRow, c - 1}) && board.getPiece({lookAheadRow, c - 1})->type() == PT_PAWN && board.getPiece({lookAheadRow, c - 1})->getColor() == BLACK) 
                                isPassed = false;
                            if (board.getPiece({lookAheadRow, c}) && board.getPiece({lookAheadRow, c})->type() == PT_PAWN && board.getPiece({lookAheadRow, c})->getColor() == BLACK) 
                                isPassed = false;
                            if (c < 7 && board.getPiece({lookAheadRow, c + 1}) && board.getPiece({lookAheadRow, c + 1})->type() == PT_PAWN && board.getPiece({lookAheadRow, c + 1})->getColor() == BLACK) 
                                isPassed = false;
                            if (!isPassed) 
                                break;
                        }
                        if (isPassed) pawnStructureScore += passedPawnBonus[7 - r];
                    } else { // Black Pawn
                        for (int lookAheadRow = r + 1; lookAheadRow <= 7; ++lookAheadRow) {
                            if (c > 0 && board.getPiece({lookAheadRow, c - 1}) && board.getPiece({lookAheadRow, c - 1})->type() == PT_PAWN && board.getPiece({lookAheadRow, c - 1})->getColor() == WHITE) 
                                isPassed = false;
                            if (board.getPiece({lookAheadRow, c}) && board.getPiece({lookAheadRow, c})->type() == PT_PAWN && board.getPiece({lookAheadRow, c})->getColor() == WHITE) 
                                isPassed = false;
                            if (c < 7 && board.getPiece({lookAheadRow, c + 1}) && board.getPiece({lookAheadRow, c + 1})->type() == PT_PAWN && board.getPiece({lookAheadRow, c + 1})->getColor() == WHITE) 
                                isPassed = false;
                            if (!isPassed) 
                                break;
                        }
                        if (isPassed) pawnStructureScore -= passedPawnBonus[r];
                    }
                }
            }
            
            score += pawnStructureScore;

            // crude castling availability bonus
            if (!board.canWhiteCastleKingside() && !board.canWhiteCastleQueenside()) score -= 0.2;
            if (!board.canBlackCastleKingside() && !board.canBlackCastleQueenside()) score += 0.2;
            
            return score;
        }

        // Order moves: MVV-LVA-ish + promotions prioritized
        void orderMoves(const Board& b, vector<Move>& moves) const {
            vector<pair<int, Move>> scored;
            scored.reserve(moves.size());
            for (const auto& m : moves) {
                IMoveable* att = b.getPiece(m.from);
                IMoveable* vic = b.getPiece(m.to);
                int attv = att ? cpVal(att->type()) : 0;
                int vicv = vic ? cpVal(vic->type())
                            : ((att && att->type()==PT_PAWN && m.to == b.getEnPassantTarget()) ? cpVal(PT_PAWN) : 0);
                int promoBonus = (m.promotionPiece!=' ') ? 10000 : 0;
                int score = promoBonus + vicv*100 - attv; // big weight on victim
                scored.push_back({score, m});
            }
            stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b){ return a.first > b.first; });
            for (size_t i=0;i<moves.size();++i) moves[i] = scored[i].second;
        }

        double quiescenceSearch(double alpha, double beta, Color playerColor, Board currentBoard) {
            double stand_pat = evaluatePosition(currentBoard);
            if (playerColor == WHITE) {
                if (stand_pat >= beta) return beta;
                if (alpha < stand_pat) alpha = stand_pat;
            } else {
                if (stand_pat <= alpha) return alpha;
                if (beta > stand_pat) beta = stand_pat;
            }

            vector<Move> captures = currentBoard.getAllLegalMoves(playerColor, true);
            orderMoves(currentBoard, captures);

            for (const auto& move : captures) {
                Board tempBoard(currentBoard);
                tempBoard.makeMove(move);
                double score = quiescenceSearch(alpha, beta, (playerColor == WHITE) ? BLACK : WHITE, tempBoard);

                if (playerColor == WHITE) {
                    if (score >= beta) return beta;
                    if (score > alpha) alpha = score;
                } else {
                    if (score <= alpha) return alpha;
                    if (score < beta) beta = score;
                }
            }
            return (playerColor == WHITE) ? alpha : beta;
        }

        double minimax(int depth, Color playerColor, double alpha, double beta, Board currentBoard) {
            uint64_t key = currentBoard.getHashKey();
            double storedScore; TTFlag storedFlag; Move storedMove;

            if (tt.probe(key, depth, storedScore, storedFlag, storedMove)) {
                if (storedFlag == EXACT) return storedScore;
                if (storedFlag == LOWERBOUND && storedScore >= beta) return beta;
                if (storedFlag == UPPERBOUND && storedScore <= alpha) return alpha;
            }

            if (depth == 0) return quiescenceSearch(alpha, beta, playerColor, currentBoard);

            vector<Move> moves = currentBoard.getAllLegalMoves(playerColor, false);
            if (moves.empty()) {
                if (currentBoard.isKingInCheck(playerColor)) return (playerColor == WHITE) ? -10000.0 : 10000.0;
                return 0.0;
            }

            orderMoves(currentBoard, moves);

            Move bestMove = moves[0];
            double bestValue = (playerColor == WHITE) ? -1e9 : 1e9;
            TTFlag flag = UPPERBOUND;

            for (const auto& move : moves) {
                Board tempBoard(currentBoard);
                tempBoard.makeMove(move);
                double eval = minimax(depth - 1, (playerColor == WHITE) ? BLACK : WHITE, alpha, beta, tempBoard);

                if (playerColor == WHITE) {
                    if (eval > bestValue) { 
                        bestValue = eval; bestMove = move; 
                    }
                    if (bestValue > alpha) { 
                        alpha = bestValue; flag = EXACT; 
                    }
                    if (alpha >= beta) { 
                        tt.store(key, depth, beta, LOWERBOUND, bestMove); 
                        return beta; 
                    }
                } else {
                    if (eval < bestValue) { 
                        bestValue = eval; bestMove = move; 
                    }
                    if (bestValue < beta)  { 
                        beta = bestValue;  flag = EXACT; 
                    }
                    if (alpha >= beta) { 
                        tt.store(key, depth, alpha, UPPERBOUND, bestMove); 
                        return alpha; 
                    }
                }
            }
            tt.store(key, depth, bestValue, flag, bestMove);
            return bestValue;
        }

    public:
        Engine(Game& g) : game(g), tt(64) {} // 64 MB TT

        Move findBestMove(Color playerColor, int maxDepth) {
            Board& initialBoard = game.getBoardForEngine();
            vector<Move> moves = initialBoard.getAllLegalMoves(playerColor, false);
            if (moves.empty()) return {{-1,-1},{-1,-1}, ' '};

            Move bestMove = moves[0];
            double bestValue = (playerColor == WHITE) ? -1e9 : 1e9;

            // Iterative deepening: loop from depth=1 up to maxDepth
            for (int depth = 1; depth <= maxDepth; depth++) {
                orderMoves(initialBoard, moves);

                unsigned hw = std::thread::hardware_concurrency();
                if (hw == 0) hw = 2;
                unsigned maxThreads = std::max(1u, hw);

                double currentBest = (playerColor == WHITE) ? -1e9 : 1e9;
                Move currentBestMove = bestMove; // carry forward last best

                // Small trees or single core → sequential search
                if (moves.size() <= 2 || maxThreads == 1) {
                    for (const auto& m : moves) {
                        Board temp(initialBoard);
                        temp.makeMove(m);
                        double v = minimax(depth - 1,
                                        (playerColor == WHITE) ? BLACK : WHITE,
                                        -1e9, 1e9,
                                        temp);

                        if ((playerColor == WHITE && v > currentBest) ||
                            (playerColor == BLACK && v < currentBest)) {
                            currentBest = v;
                            currentBestMove = m;
                        }
                    }
                } else {
                    // Parallel root move search
                    size_t idx = 0;
                    vector<future<pair<double, Move>>> futures;
                    auto launch = [&](const Move& m) {
                        return std::async(std::launch::async,
                            [this, m, &initialBoard, playerColor, depth]() -> pair<double,Move> {
                                Board temp(initialBoard);
                                temp.makeMove(m);
                                double v = minimax(depth - 1,
                                                (playerColor == WHITE) ? BLACK : WHITE,
                                                -1e9, 1e9,
                                                temp);
                                return {v, m};
                            });
                    };

                    while (idx < moves.size() || !futures.empty()) {
                        while (idx < moves.size() && futures.size() < maxThreads) {
                            futures.push_back( launch(moves[idx++]) );
                        }
                        auto res = futures.back().get();
                        futures.pop_back();

                        double v = res.first;
                        const Move& m = res.second;
                        if ((playerColor == WHITE && v > currentBest) ||
                            (playerColor == BLACK && v < currentBest)) {
                            currentBest = v;
                            currentBestMove = m;
                        }
                    }
                }

                // Update the best move/value after this depth
                bestValue = currentBest;
                bestMove  = currentBestMove;
            }

            return bestMove;
        }
};

void Game::run() {
    while(true) {
        board.display();

        if (isCheckmate(currentPlayer)) {
            cout << (currentPlayer == WHITE ? "Black" : "White") << " wins by checkmate!\n";
            break;
        }
        if (isStalemate(currentPlayer)) {
            cout << "The game is a draw by stalemate.\n";
            break;
        }
        if (isDrawByInsufficientMaterial()) {
            cout << "The game is a draw by insufficient material.\n";
            break;
        }

        if (currentPlayer == WHITE) {
            cout << "White's turn. Enter your move (e.g., e2e4 or e7e8q): ";
            string moveStr;
            if (!(cin >> moveStr)) 
                break;
            if (moveStr == "exit") 
                break;

            Move move = parseMove(moveStr);
            IMoveable* piece = board.getPiece(move.from);
            if (piece && piece->type() == PT_PAWN) {
                int promotionRank = (piece->getColor() == WHITE) ? 0 : 7;
                if (move.to.row == promotionRank && move.promotionPiece == ' ') {
                    cout << "Promote to (Q, R, B, N): ";
                    char p; cin >> p;
                    move.promotionPiece = (char)tolower((unsigned char)p);
                }
            }

            vector<Move> legalMoves = board.getAllLegalMoves(currentPlayer, false);
            bool isLegal = false;
            for(const auto& legalMove : legalMoves) {
                if (legalMove.from == move.from && legalMove.to == move.to) {
                    if (legalMove.promotionPiece == ' ' || legalMove.promotionPiece == move.promotionPiece) {
                        isLegal = true;
                        move.promotionPiece = (legalMove.promotionPiece==' ')? move.promotionPiece : legalMove.promotionPiece;
                        break;
                    }
                }
            }

            if (isLegal) {
                board.makeMove(move);
                currentPlayer = BLACK;
            } else {
                cout << "Invalid or illegal move.\n";
            }
        } else {
            cout << "Black's turn (AI is thinking...)\n";
            Move bestMove = engine->findBestMove(BLACK, depthLimit);
            if(bestMove.from.row != -1) {
                board.makeMove(bestMove);
                currentPlayer = WHITE;
            } else {
                cout << "AI has no moves.\n";
            }
        }
    }
}

int main() {
    Game game;
    game.run();
    return 0;
}
