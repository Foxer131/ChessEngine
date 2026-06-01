#pragma once
// =============================================================================
// MainWindow - ties together the board model, the board widget, and the UCI
// engine. Owns the game flow: human moves go to the board + engine; the engine
// reply comes back asynchronously and is applied to the board.
//
// If no engine is loaded, the window falls back to "free play": the user can
// move both colors, which is handy for exercising the board before the engine
// exists.
// =============================================================================

#include <QMainWindow>
#include <QStringList>
#include <cstdint>
#include <unordered_map>
#include "GuiBoard.h"

class BoardWidget;
class UciEngine;
class QPlainTextEdit;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void newGame();
    void chooseEngine();
    void setEval(bool useNnue);   // switch engine eval: HCE (false) / NNUE (true)
    void onMoveRequested(int fromFile, int fromRank, int toFile, int toRank);
    void onBestMove(const QString& uci);
    void onInfoLine(const QString& line);
    void onEngineError(const QString& message);

private:
    void setupMenu();
    void tryAutoLoadEngine();
    void requestEngineMove();
    // Record the current position and, if it's a draw (threefold / 50-move), end
    // the game. Returns true if the game ended.
    bool recordPositionAndCheckDraw();
    bool engineToMove() const { return board_.sideToMove() != humanColor_; }
    void log(const QString& s);
    void updateStatus();

    GuiBoard       board_;
    BoardWidget*   boardView_ = nullptr;
    UciEngine*     engine_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QAction*    actHce_ = nullptr;       // Evaluation menu radio items (for syncing
    QAction*    actNnue_ = nullptr;      // the checkmark when the mode auto-picks)

    QString     enginePath_;
    QStringList moves_;                 // game moves in UCI long algebraic
    GuiBoard::Color humanColor_ = GuiBoard::Color::White;
    bool useMovetime_ = false;
    int  depth_ = 8;
    int  movetimeMs_ = 1000;
    bool gameActive_ = false;

    bool useNnue_ = false;          // chosen eval: false = HCE (default), true = NNUE
    bool engineThinking_ = false;   // a search request is outstanding
    int  pendingDiscards_ = 0;      // stale bestmoves (from aborted searches) to ignore

    std::unordered_map<std::uint64_t, int> posCount_;  // occurrences per position (threefold)
};
