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
#include <cctype>
#include <stack>

using namespace std;

class IMoveable;
class Board;
class Engine;

enum Color { WHITE = 0, BLACK = 1, NONE = 2 };

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
    bool operator==(const Position& other) const { 
        return row == other.row && col == other.col; 
    }
};

struct Move {
    Position from;
    Position to;
    char promotionPiece = ' ';
};

struct BoardState {
    Move move;
    unique_ptr<IMoveable> capturedPiece;
    Position enPassantTargetSquare;
    bool whiteKingMoved, blackKingMoved;
    bool whiteRookAMoved, whiteRookHMoved;
    bool blackRookAMoved, blackRookHMoved;
    uint64_t hashKey;
};

class IMoveable {
public:
    virtual ~IMoveable() = default;
    virtual vector<Move> getValidMoves(const Position& position, const Board& board) const = 0;
    virtual Color getColor() const = 0;
    virtual char getSymbol() const = 0;
    virtual unique_ptr<IMoveable> clone() const = 0;
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
        mt19937_64 gen(12345);
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
        stack<BoardState> history;

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
        void unmakeMove();
        Position findKing(Color kingColor) const;
        bool isKingInCheck(Color kingColor) const;
        bool isSquareAttackedBy(const Position& pos, Color attackerColor) const;
        vector<Move> getAllLegalMoves(Color playerColor, bool capturesOnly);

        bool canWhiteCastleKingside()  const { 
            return !whiteKingMoved && !whiteRookHMoved; 
        }

        bool canWhiteCastleQueenside() const { 
            return !whiteKingMoved && !whiteRookAMoved; 
        }

        bool canBlackCastleKingside()  const { 
            return !blackKingMoved && !blackRookHMoved; 
        }

        bool canBlackCastleQueenside() const { 
            return !blackKingMoved && !blackRookAMoved; 
        }

        Position getEnPassantTarget() const { 
            return enPassantTargetSquare; 
        }

        uint64_t getHashKey() const { 
            return hashKey; 
        }

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

        if ((color == WHITE && board.canWhiteCastleKingside()) || (color == BLACK && board.canBlackCastleKingside())) {
            if (board.getPiece({position.row, 5}) == nullptr && board.getPiece({position.row, 6}) == nullptr) {
                if (!board.isSquareAttackedBy({position.row, 5}, opp) && !board.isSquareAttackedBy({position.row, 6}, opp)) {
                    validMoves.push_back({position, {position.row, 6}});
                }
            }
        }
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
    grid[0][0] = make_unique<Rook>(BLACK);
    grid[0][1] = make_unique<Knight>(BLACK);
    grid[0][2] = make_unique<Bishop>(BLACK);
    grid[0][3] = make_unique<Queen>(BLACK);
    grid[0][4] = make_unique<King>(BLACK);
    grid[0][5] = make_unique<Bishop>(BLACK);
    grid[0][6] = make_unique<Knight>(BLACK);
    grid[0][7] = make_unique<Rook>(BLACK);
    for (int c = 0; c < 8; ++c) grid[1][c] = make_unique<Pawn>(BLACK);

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
            if (!piece || piece->getColor() != attackerColor) 
                continue;

            int t = piece->type();
            if (t == PT_PAWN) {
                int dir = (attackerColor == WHITE) ? -1 : 1;
                if (pos.row == r + dir && (pos.col == c - 1 || pos.col == c + 1)) 
                    return true;
            } else if (t == PT_KING) {
                if (abs(pos.row - r) <= 1 && abs(pos.col - c) <= 1) 
                    return true;
            } else {
                vector<Move> moves = piece->getValidMoves(currentPos, *this);
                for (const auto& m : moves) 
                    if (m.to == pos) 
                        return true;
            }
        }
    }
    return false;
}

bool Board::isKingInCheck(Color kingColor) const {
    Position kingPos = findKing(kingColor);
    if (kingPos.row == -1) 
        return true; // Should not happen in a legal game
    return isSquareAttackedBy(kingPos, (kingColor == WHITE) ? BLACK : WHITE);
}

void Board::makeMove(const Move& move) {
    BoardState state;
    state.move = move;
    state.hashKey = this->hashKey;
    state.capturedPiece = nullptr; 
    state.enPassantTargetSquare = this->enPassantTargetSquare;
    state.whiteKingMoved = this->whiteKingMoved;
    state.blackKingMoved = this->blackKingMoved;
    state.whiteRookAMoved = this->whiteRookAMoved;
    state.whiteRookHMoved = this->whiteRookHMoved;
    state.blackRookAMoved = this->blackRookAMoved;
    state.blackRookHMoved = this->blackRookHMoved;
    history.push(std::move(state)); 

    const Position from = move.from;
    const Position to = move.to;
    IMoveable* piece = getPiece(from);
    Color pieceColor = piece->getColor();

    uint64_t newHash = this->hashKey;

    if (piece->type() == PT_KING) {
        if (pieceColor == WHITE) whiteKingMoved = true; 
        else blackKingMoved = true;
    }
    if (from.row == 7 && from.col == 0) whiteRookAMoved = true;
    if (from.row == 7 && from.col == 7) whiteRookHMoved = true;
    if (from.row == 0 && from.col == 0) blackRookAMoved = true;
    if (from.row == 0 && from.col == 7) blackRookHMoved = true;

    IMoveable* capturedPiece = getPiece(to);
    if (capturedPiece) {
        history.top().capturedPiece = std::move(grid[to.row][to.col]);
        newHash ^= Zobrist::pieceKeys[capturedPiece->getColor()][capturedPiece->type()][to.row * 8 + to.col];
    } else if (piece->type() == PT_PAWN && to == this->enPassantTargetSquare) {
        int capturedPawnRow = (pieceColor == WHITE) ? to.row + 1 : to.row - 1;
        history.top().capturedPiece = std::move(grid[capturedPawnRow][to.col]);
        newHash ^= Zobrist::pieceKeys[history.top().capturedPiece->getColor()][PT_PAWN][capturedPawnRow * 8 + to.col];
    }

    newHash ^= Zobrist::pieceKeys[pieceColor][piece->type()][from.row * 8 + from.col];
    newHash ^= Zobrist::pieceKeys[pieceColor][piece->type()][to.row * 8 + to.col];

    if (this->enPassantTargetSquare.col != -1) newHash ^= Zobrist::enPassantKeys[this->enPassantTargetSquare.col];
    enPassantTargetSquare = {-1, -1}; 
    if (piece->type() == PT_PAWN && abs(from.row - to.row) == 2) {
        enPassantTargetSquare = {(from.row + to.row) / 2, from.col};
        newHash ^= Zobrist::enPassantKeys[enPassantTargetSquare.col];
    }
    
    int oldCastleRights = (history.top().whiteKingMoved == false && history.top().whiteRookHMoved == false) << 3 |
                          (history.top().whiteKingMoved == false && history.top().whiteRookAMoved == false) << 2 |
                          (history.top().blackKingMoved == false && history.top().blackRookHMoved == false) << 1 |
                          (history.top().blackKingMoved == false && history.top().blackRookAMoved == false);
    int newCastleRights = (canWhiteCastleKingside() << 3) | (canWhiteCastleQueenside() << 2) | (canBlackCastleKingside() << 1) | canBlackCastleQueenside();
    if(oldCastleRights != newCastleRights) {
        newHash ^= Zobrist::castleKeys[oldCastleRights];
        newHash ^= Zobrist::castleKeys[newCastleRights];
    }

    grid[to.row][to.col] = std::move(grid[from.row][from.col]);

    // Handle castling
    if (piece->type() == PT_KING && abs(to.col - from.col) == 2) {
        if (to.col == 6) { // Kingside
            grid[from.row][5] = std::move(grid[from.row][7]);
            newHash ^= Zobrist::pieceKeys[pieceColor][PT_ROOK][from.row * 8 + 7];
            newHash ^= Zobrist::pieceKeys[pieceColor][PT_ROOK][from.row * 8 + 5];
        } else { // Queenside
            grid[from.row][3] = std::move(grid[from.row][0]);
            newHash ^= Zobrist::pieceKeys[pieceColor][PT_ROOK][from.row * 8 + 0];
            newHash ^= Zobrist::pieceKeys[pieceColor][PT_ROOK][from.row * 8 + 3];
        }
    }

    // Handle promotion
    if (move.promotionPiece != ' ') {
        newHash ^= Zobrist::pieceKeys[pieceColor][PT_PAWN][to.row * 8 + to.col]; // Remove pawn
        unique_ptr<IMoveable> newPiece;
        switch (move.promotionPiece) {
            case 'q': newPiece = make_unique<Queen>(pieceColor); break;
            case 'r': newPiece = make_unique<Rook>(pieceColor); break;
            case 'b': newPiece = make_unique<Bishop>(pieceColor); break;
            case 'n': newPiece = make_unique<Knight>(pieceColor); break;
        }
        newHash ^= Zobrist::pieceKeys[pieceColor][newPiece->type()][to.row * 8 + to.col];
        setPiece(to, std::move(newPiece));
    }

    // Hash update
    this->hashKey = newHash ^ Zobrist::sideKey;
}

void Board::unmakeMove() {
    if (history.empty()) 
        return;

    BoardState state = std::move(history.top());
    history.pop();
    Move move = state.move;
    const Position from = move.from;
    const Position to = move.to;

    this->hashKey = state.hashKey;
    this->enPassantTargetSquare = state.enPassantTargetSquare;
    this->whiteKingMoved = state.whiteKingMoved;
    this->blackKingMoved = state.blackKingMoved;
    this->whiteRookAMoved = state.whiteRookAMoved;
    this->whiteRookHMoved = state.whiteRookHMoved;
    this->blackRookAMoved = state.blackRookAMoved;
    this->blackRookHMoved = state.blackRookHMoved;

    // Unpromotion and restore piece
    if (move.promotionPiece != ' ') {
        grid[from.row][from.col] = make_unique<Pawn>(getPiece(to)->getColor());
        grid[to.row][to.col] = nullptr;
    } else {
        grid[from.row][from.col] = std::move(grid[to.row][to.col]);
    }

    // Handles castling reversal
    IMoveable* pieceAtFrom = getPiece(from);
    if (pieceAtFrom && pieceAtFrom->type() == PT_KING && abs(to.col - from.col) == 2) {
        if (to.col == 6) { // Kingside
            grid[from.row][7] = std::move(grid[from.row][5]);
        } else { // Queenside
            grid[from.row][0] = std::move(grid[from.row][3]);
        }
    }

    // Restore captured piece
    if (state.capturedPiece) {
        // En-passant capture reversal
        if (pieceAtFrom && pieceAtFrom->type() == PT_PAWN && to == state.enPassantTargetSquare) {
            int capturedPawnRow = (pieceAtFrom->getColor() == WHITE) ? to.row + 1 : to.row - 1;
            grid[capturedPawnRow][to.col] = std::move(state.capturedPiece);
            grid[to.row][to.col] = nullptr; // The 'to' square for EP is empty
        } else {
            grid[to.row][to.col] = std::move(state.capturedPiece);
        }
    }
}


vector<Move> Board::getAllLegalMoves(Color playerColor, bool capturesOnly) {
    vector<Move> legalMoves;
    legalMoves.reserve(256);

    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            IMoveable* piece = getPiece({r, c});
            if (!piece || piece->getColor() != playerColor) 
                continue;

            vector<Move> moves = piece->getValidMoves({r, c}, *this);
            for (const auto& move : moves) {
                if (capturesOnly) {
                    bool isCapture = getPiece(move.to) != nullptr || (piece->type() == PT_PAWN && move.to == getEnPassantTarget());
                    if (!isCapture) 
                        continue;
                }

                makeMove(move);
                if (!isKingInCheck(playerColor)) {
                    legalMoves.push_back(move);
                }
                unmakeMove();
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
        int moveCount = 0;

        Move parseMove(const string& moveStr) {
            if (moveStr.length() < 4 || moveStr.length() > 5)
                return { { -1, -1 }, { -1, -1 } };
            int fromCol = moveStr[0] - 'a';
            int fromRow = 8 - (moveStr[1] - '0');
            int toCol   = moveStr[2] - 'a';
            int toRow   = 8 - (moveStr[3] - '0');
            char promotion = ' ';
            if (moveStr.length() == 5) 
                promotion = (char)tolower((unsigned char)moveStr[4]);
            return { 
                { fromRow, fromCol }, { toRow, toCol }, promotion 
            };
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
                    if (!piece || piece->type() == PT_KING) 
                        continue;

                    int t = piece->type();
                    if (t == PT_QUEEN || t == PT_ROOK || t == PT_PAWN) 
                        return false;

                    if (t == PT_KNIGHT) {
                        if (piece->getColor() == WHITE) whiteKnights++; 
                        else blackKnights++;
                    } else if (t == PT_BISHOP) {
                        if (piece->getColor() == WHITE) whiteBishops++; 
                        else blackBishops++;
                        int currentSquareColor = (r + c) % 2;
                        if (bishopSquareColor == -1) 
                            bishopSquareColor = currentSquareColor;
                        else if (bishopSquareColor != currentSquareColor) 
                            return false;
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
        static constexpr double pawnValue   = 1.0;
        static constexpr double knightValue = 3.2;
        static constexpr double bishopValue = 3.3;
        static constexpr double rookValue   = 5.0;
        static constexpr double queenValue  = 9.0;
        static constexpr double kingValue   = 200.0;
        static constexpr double doubledPawnPenalty  = -0.35;
        static constexpr double isolatedPawnPenalty = -0.20;
        const double passedPawnBonus[8] = { 0.0, 0.2, 0.4, 0.75, 1.25, 2.0, 3.0, 4.5 };
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
                case PT_PAWN:   
                    base = pawnValue;   
                    pst = (col==WHITE) ? pawnTable[r][c] : pawnTable[7-r][c]; 
                    break;
                case PT_KNIGHT: 
                    base = knightValue; 
                    pst = (col==WHITE) ? knightTable[r][c] : knightTable[7-r][c]; 
                    break;
                case PT_BISHOP: 
                    base = bishopValue; 
                    pst = (col==WHITE) ? bishopTable[r][c] : bishopTable[7-r][c]; 
                    break;
                case PT_ROOK:   
                    base = rookValue;   
                    pst = (col==WHITE) ? rookTable[r][c] : rookTable[7-r][c]; 
                    break;
                case PT_QUEEN:  
                    base = queenValue;  break;
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
            int whitePawnsPerFile[8] = {0};
            int blackPawnsPerFile[8] = {0};

            for (int r = 0; r < 8; ++r) {
                for (int c = 0; c < 8; ++c) {
                    IMoveable* piece = board.getPiece({r, c});
                    if (!piece) 
                        continue;

                    if (piece->type() != PT_KING) 
                        materialCount++;
                    if (piece->type() == PT_PAWN) {
                        if (piece->getColor() == WHITE) 
                            whitePawnsPerFile[c]++;
                        else 
                            blackPawnsPerFile[c]++;
                    }
                    double value = getPieceValue(piece, r, c, materialCount < 12);
                    score += (piece->getColor() == WHITE) ? value : -value;
                }
            }
            
            double pawnStructureScore = 0.0;
            for (int c = 0; c < 8; ++c) {
                if (whitePawnsPerFile[c] > 1) pawnStructureScore += (whitePawnsPerFile[c] - 1) * doubledPawnPenalty;
                if (blackPawnsPerFile[c] > 1) pawnStructureScore -= (blackPawnsPerFile[c] - 1) * doubledPawnPenalty;

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
            
            for (int r = 0; r < 8; ++r) {
                for (int c = 0; c < 8; ++c) {
                    IMoveable* piece = board.getPiece({r, c});
                    if (!piece || piece->type() != PT_PAWN) continue;

                    bool isPassed = true;
                    if (piece->getColor() == WHITE) {
                        for (int lookAheadRow = r - 1; lookAheadRow >= 0; --lookAheadRow) {
                            if ((c > 0 && board.getPiece({lookAheadRow, c - 1}) && board.getPiece({lookAheadRow, c - 1})->type() == PT_PAWN && board.getPiece({lookAheadRow, c - 1})->getColor() == BLACK) ||
                                (board.getPiece({lookAheadRow, c}) && board.getPiece({lookAheadRow, c})->type() == PT_PAWN && board.getPiece({lookAheadRow, c})->getColor() == BLACK) ||
                                (c < 7 && board.getPiece({lookAheadRow, c + 1}) && board.getPiece({lookAheadRow, c + 1})->type() == PT_PAWN && board.getPiece({lookAheadRow, c + 1})->getColor() == BLACK)) {
                                isPassed = false;
                                break;
                            }
                        }
                        if (isPassed) pawnStructureScore += passedPawnBonus[7 - r];
                    } else {
                        for (int lookAheadRow = r + 1; lookAheadRow <= 7; ++lookAheadRow) {
                            if ((c > 0 && board.getPiece({lookAheadRow, c - 1}) && board.getPiece({lookAheadRow, c - 1})->type() == PT_PAWN && board.getPiece({lookAheadRow, c - 1})->getColor() == WHITE) ||
                                (board.getPiece({lookAheadRow, c}) && board.getPiece({lookAheadRow, c})->type() == PT_PAWN && board.getPiece({lookAheadRow, c})->getColor() == WHITE) ||
                                (c < 7 && board.getPiece({lookAheadRow, c + 1}) && board.getPiece({lookAheadRow, c + 1})->type() == PT_PAWN && board.getPiece({lookAheadRow, c + 1})->getColor() == WHITE)) {
                                isPassed = false;
                                break;
                            }
                        }
                        if (isPassed) pawnStructureScore -= passedPawnBonus[r];
                    }
                }
            }
            
            score += pawnStructureScore;
            if (!board.canWhiteCastleKingside() && !board.canWhiteCastleQueenside()) score -= 0.2;
            if (!board.canBlackCastleKingside() && !board.canBlackCastleQueenside()) score += 0.2;
            
            return score;
        }

        void orderMoves(Board& b, vector<Move>& moves) const {
            vector<pair<int, Move>> scored;
            scored.reserve(moves.size());
            for (const auto& m : moves) {
                IMoveable* att = b.getPiece(m.from);
                IMoveable* vic = b.getPiece(m.to);
                int attv = att ? cpVal(att->type()) : 0;
                int vicv = vic ? cpVal(vic->type())
                            : ((att && att->type()==PT_PAWN && m.to == b.getEnPassantTarget()) ? cpVal(PT_PAWN) : 0);
                int promoBonus = (m.promotionPiece!=' ') ? 10000 : 0;
                int score = promoBonus + vicv*100 - attv;
                scored.push_back({score, m});
            }
            stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b){ return a.first > b.first; });
            for (size_t i=0;i<moves.size();++i) 
                moves[i] = scored[i].second;
        }

        double quiescenceSearch(double alpha, double beta, Color playerColor, Board& currentBoard) {
            double stand_pat = evaluatePosition(currentBoard);
            if (playerColor == WHITE) {
                if (stand_pat >= beta) 
                    return beta;
                if (alpha < stand_pat) 
                    alpha = stand_pat;
            } else {
                if (stand_pat <= alpha) 
                    return alpha;
                if (beta > stand_pat) 
                    beta = stand_pat;
            }

            vector<Move> captures = currentBoard.getAllLegalMoves(playerColor, true);
            orderMoves(currentBoard, captures);

            for (const auto& move : captures) {
                currentBoard.makeMove(move);
                double score = quiescenceSearch(alpha, beta, (playerColor == WHITE) ? BLACK : WHITE, currentBoard);
                currentBoard.unmakeMove();

                if (playerColor == WHITE) {
                    if (score >= beta) 
                        return beta;
                    if (score > alpha) 
                        alpha = score;
                } else {
                    if (score <= alpha) 
                        return alpha;
                    if (score < beta)  
                        beta = score;
                }
            }
            return (playerColor == WHITE) ? alpha : beta;
        }

        double minimax(int depth, Color playerColor, double alpha, double beta, Board& currentBoard) {
            uint64_t key = currentBoard.getHashKey();
            double storedScore; TTFlag storedFlag; Move storedMove;

            if (tt.probe(key, depth, storedScore, storedFlag, storedMove)) {
                if (storedFlag == EXACT) return storedScore;
                if (storedFlag == LOWERBOUND && storedScore >= beta) return beta;
                if (storedFlag == UPPERBOUND && storedScore <= alpha) return alpha;
            }

            if (depth == 0) 
                return quiescenceSearch(alpha, beta, playerColor, currentBoard);

            vector<Move> moves = currentBoard.getAllLegalMoves(playerColor, false);
            if (moves.empty()) {
                if (currentBoard.isKingInCheck(playerColor)) 
                    return (playerColor == WHITE) ? -10000.0 : 10000.0;
                return 0.0;
            }

            orderMoves(currentBoard, moves);

            Move bestMove = moves[0];
            double bestValue = (playerColor == WHITE) ? -1e9 : 1e9;
            TTFlag flag = UPPERBOUND;

            for (const auto& move : moves) {
                currentBoard.makeMove(move);
                double eval = minimax(depth - 1, (playerColor == WHITE) ? BLACK : WHITE, alpha, beta, currentBoard);
                currentBoard.unmakeMove();

                if (playerColor == WHITE) {
                    if (eval > bestValue) { 
                        bestValue = eval; 
                        bestMove = move; 
                    }
                    if (bestValue > alpha) { 
                        alpha = bestValue; 
                        flag = EXACT; 
                    }
                    if (alpha >= beta) { 
                        tt.store(key, depth, beta, LOWERBOUND, bestMove); 
                        return beta; 
                    }
                } else {
                    if (eval < bestValue) { 
                        bestValue = eval; 
                        bestMove = move; 
                    }
                    if (bestValue < beta)  { 
                        beta = bestValue;  
                        flag = EXACT; 
                    }
                    if (beta <= alpha) { 
                        tt.store(key, depth, alpha, UPPERBOUND, bestMove); 
                        return alpha; 
                    }
                }
            }
            tt.store(key, depth, bestValue, flag, bestMove);
            return bestValue;
        }

    public:
        Engine(Game& g) : game(g), tt(64) {}

        Move findBestMove(Color playerColor, int maxDepth) {
            Board& initialBoard = game.getBoardForEngine();
            vector<Move> moves = initialBoard.getAllLegalMoves(playerColor, false);
            if (moves.empty()) return {{-1,-1},{-1,-1}, ' '};

            Move bestMove = moves[0];
            double bestValue = (playerColor == WHITE) ? -1e9 : 1e9;

            for (int depth = 1; depth <= maxDepth; depth++) {
                orderMoves(initialBoard, moves);

                double currentBestForDepth = (playerColor == WHITE) ? -1e9 : 1e9;
                Move currentBestMoveForDepth = bestMove; 
                vector<future<pair<double, Move>>> futures;

                // The lambda function that will be executed by each thread
                auto searchTask = [this, playerColor, depth](Move m, const Board& board) -> pair<double, Move> {
                    Board thread_local_board(board); 
                    thread_local_board.makeMove(m);
                    double score = minimax(depth - 1,
                                        (playerColor == WHITE) ? BLACK : WHITE,
                                        -1e9, 1e9,
                                        thread_local_board); 
                    return {score, m};
                };

                // Launch tasks in parallel
                for (const auto& move : moves) {
                    futures.push_back(std::async(std::launch::async, searchTask, move, std::ref(initialBoard)));
                }

                // Collect results
                for (auto& fut : futures) {
                    pair<double, Move> result = fut.get();
                    double v = result.first;
                    const Move& m = result.second;

                    if ((playerColor == WHITE && v > currentBestForDepth) || (playerColor == BLACK && v < currentBestForDepth)) {
                        currentBestForDepth = v;
                        currentBestMoveForDepth = m;
                    }
                }

                bestValue = currentBestForDepth;
                bestMove = currentBestMoveForDepth;
            }
            std::system("cls");
            cout << "Evaluation: " << bestValue << '\n';
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
            if (!(cin >> moveStr) || moveStr == "exit") 
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
                        move.promotionPiece = legalMove.promotionPiece;
                        break;
                    }
                }
            }

            if (isLegal) {
                board.makeMove(move);
                currentPlayer = BLACK;
                std::system("cls");
            } else {
                cout << "Invalid or illegal move.\n";
            }
        } else {
            cout << "Black's turn (AI is thinking...)\n";
            Move bestMove = engine->findBestMove(BLACK, 5);
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
