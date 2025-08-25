# ChessEngine

A command-line chess engine written in C++. It features a sophisticated evaluation function and a multi-threaded search algorithm to play a competitive game of chess.

-----

## Features

This chess engine includes a variety of advanced features to ensure strong gameplay:

  * **Robust Game Logic**: Full implementation of all chess rules, including piece movements, captures, castling, en-passant, and pawn promotion.
  * **Intelligent Search Algorithm**:
      * **Minimax with Alpha-Beta Pruning**: Efficiently searches the game tree to find the optimal move.
      * **Quiescence Search**: Deepens the search in tactical positions to avoid the horizon effect and make more accurate evaluations.
      * **Transposition Table**: Uses **Zobrist hashing** to cache previously evaluated positions, significantly speeding up search times.
      * **Null Move Pruning**: A powerful technique to reduce the search space by assuming the opponent can "pass" their turn.
  * **Advanced Evaluation Function**: The engine's decision-making is guided by a comprehensive evaluation of the board state, considering:
      * **Material Balance**: The standard values of pieces.
      * **Piece-Square Tables**: The strategic value of placing each piece on each square of the board. Different tables are used for the midgame and endgame.
      * **Pawn Structure**: Penalizes weaknesses like doubled and isolated pawns while rewarding strengths like passed pawns.
      * **King Safety**: Evaluates the strength of the pawn shield around the king and the danger of open files.
      * **Mobility & Center Control**: Rewards control over the center of the board and pieces with more available moves.
      * **Piece Coordination**: Includes bonuses for having a bishop pair or a knight pair.
  * **Multi-Threading**: Utilizes multiple CPU cores to parallelize the search for the best move, improving performance.
  * **Game State Detection**: Accurately detects game-ending conditions such as **checkmate**, **stalemate**, and **draw by insufficient material**.

-----

## How to Compile and Run

The engine is contained in a single C++ file and can be compiled with a standard C++ compiler like g++.

### Prerequisites

  * A C++ compiler that supports at least the C++11 standard (e.g., g++).

### Compilation

Open your terminal or command prompt, navigate to the directory containing `chessEngine.cpp`, and run the following command:

```sh
g++ chessEngine.cpp -o chessEngine -std=c++11 -pthread
```

*Note: The `-pthread` flag is necessary to enable the multi-threading features.*

### Running the Game

After successful compilation, you can run the engine with:

```sh
./chessEngine
```

-----

## How to Play

1.  When the game starts, you will be prompted to **choose your color** (e.g., "white" or "black").
2.  The current board state will be displayed in the console.
3.  On your turn, enter your move using **algebraic notation** for the starting and ending squares. For example, to move a pawn from e2 to e4, type `e2e4` and press Enter.
4.  **Pawn Promotion**: If you move a pawn to the final rank, you will be prompted to choose a piece to promote to (q for Queen, r for Rook, b for Bishop, or n for Knight).
5.  The engine will think and make its move, displaying the new board state.
6.  To quit the game at any time, type `exit`.
