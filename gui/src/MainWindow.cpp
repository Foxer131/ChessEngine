#include "MainWindow.h"

#include "BoardWidget.h"
#include "UciEngine.h"
#include "NewGameDialog.h"

#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QActionGroup>
#include <QStatusBar>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QWidget>
#include <QHBoxLayout>
#include <QSettings>
#include <QFileInfo>
#include <QDir>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("Chess Engine"));

    boardView_ = new BoardWidget(this);
    boardView_->setBoard(&board_);
    connect(boardView_, &BoardWidget::moveRequested, this, &MainWindow::onMoveRequested);

    logView_ = new QPlainTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setMaximumWidth(280);
    logView_->setPlaceholderText(tr("Engine output / move log"));

    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    layout->addWidget(boardView_, /*stretch*/ 1);
    layout->addWidget(logView_);
    setCentralWidget(central);

    engine_ = new UciEngine(this);
    connect(engine_, &UciEngine::bestMove,    this, &MainWindow::onBestMove);
    connect(engine_, &UciEngine::infoLine,    this, &MainWindow::onInfoLine);
    connect(engine_, &UciEngine::engineError, this, &MainWindow::onEngineError);
    // Re-apply the chosen eval whenever the engine (re)starts (it defaults to HCE,
    // so only NNUE needs sending). `ready` fires on uciok/readyok.
    connect(engine_, &UciEngine::ready, this, [this] {
        if (useNnue_) engine_->setOption(QStringLiteral("Eval"), QStringLiteral("NNUE"));
    });

    setupMenu();
    tryAutoLoadEngine();
    updateStatus();
    resize(880, 600);
}

void MainWindow::setupMenu() {
    auto* gameMenu = menuBar()->addMenu(tr("&Game"));
    gameMenu->addAction(tr("&New Game..."), QKeySequence::New, this, &MainWindow::newGame);
    gameMenu->addAction(tr("Set &Engine..."), this, &MainWindow::chooseEngine);
    gameMenu->addSeparator();
    gameMenu->addAction(tr("E&xit"), this, &QWidget::close);

    // Evaluation selector: HCE (hand-crafted) vs NNUE (neural net). Mutually
    // exclusive radio items - a manual override. New Game also auto-picks per mode
    // (depth-limited -> NNUE, which is stronger at equal depth; timed -> HCE, which
    // is stronger on the clock until NNUE inference is faster).
    auto* evalMenu = menuBar()->addMenu(tr("E&valuation"));
    auto* group = new QActionGroup(this);
    group->setExclusive(true);
    actHce_  = evalMenu->addAction(tr("&HCE (hand-crafted)"));
    actNnue_ = evalMenu->addAction(tr("&NNUE (neural net)"));
    actHce_->setCheckable(true);
    actNnue_->setCheckable(true);
    group->addAction(actHce_);
    group->addAction(actNnue_);
    actHce_->setChecked(!useNnue_);
    actNnue_->setChecked(useNnue_);
    connect(actHce_,  &QAction::triggered, this, [this] { setEval(false); });
    connect(actNnue_, &QAction::triggered, this, [this] { setEval(true);  });
}

void MainWindow::setEval(bool useNnue) {
    useNnue_ = useNnue;
    if (actHce_)  actHce_->setChecked(!useNnue);   // keep the menu checkmark in sync
    if (actNnue_) actNnue_->setChecked(useNnue);
    engine_->setOption(QStringLiteral("Eval"), useNnue ? QStringLiteral("NNUE")
                                                       : QStringLiteral("HCE"));
    log(useNnue ? tr("Evaluation: NNUE (neural net).")
                : tr("Evaluation: HCE (hand-crafted)."));
}

void MainWindow::tryAutoLoadEngine() {
    QSettings s;
    QString saved = s.value(QStringLiteral("enginePath")).toString();

    // Prefer a saved path; otherwise look for "engine[.exe]" beside the GUI.
    QStringList candidates;
    if (!saved.isEmpty()) candidates << saved;
    const QString dir = QApplication::applicationDirPath();
    candidates << dir + QStringLiteral("/engine.exe")
               << dir + QStringLiteral("/engine");

    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            enginePath_ = path;
            engine_->start(enginePath_);
            log(tr("Engine: %1").arg(QDir::toNativeSeparators(path)));
            return;
        }
    }
    log(tr("No engine loaded. Game > Set Engine... to choose one. "
           "Until then you can move both sides (free play)."));
}

void MainWindow::chooseEngine() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Select UCI engine executable"),
        QApplication::applicationDirPath(),
#ifdef Q_OS_WIN
        tr("Executables (*.exe);;All files (*)"));
#else
        tr("All files (*)"));
#endif
    if (path.isEmpty()) return;
    enginePath_ = path;
    QSettings().setValue(QStringLiteral("enginePath"), path);
    if (engine_->start(path))
        log(tr("Engine: %1").arg(QDir::toNativeSeparators(path)));
}

void MainWindow::newGame() {
    NewGameDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    humanColor_  = dlg.humanColor();
    useMovetime_ = dlg.useMovetime();
    depth_       = dlg.depth();
    movetimeMs_  = dlg.movetimeMs();

    // New game id: any bestmove still in flight from the previous game now belongs
    // to an old id and will be ignored by onBestMove. Stop the old search too.
    ++gameId_;
    if (engineThinking_) engine_->sendStop();
    engineThinking_ = false;

    if (engine_->isRunning()) engine_->newGame();   // clears the engine's TT

    // NNUE is stronger in every mode now (SPRT: +237 Elo vs HCE at 8+0.08, +350 at
    // fixed nodes), so use it by default. The Evaluation menu can still pick HCE.
    setEval(/*useNnue=*/useNnue_);

    board_.reset();
    moves_.clear();
    posCount_.clear();
    ++posCount_[board_.key()];   // the start position counts as one occurrence
    gameActive_ = true;

    boardView_->setOrientation(humanColor_);
    boardView_->setHumanColor(humanColor_);
    boardView_->clearLastMove();
    boardView_->setBoard(&board_);

    log(tr("New game. You play %1.")
            .arg(humanColor_ == GuiBoard::Color::White ? tr("White") : tr("Black")));

    if (engineToMove() && engine_->isRunning()) {
        boardView_->setInputEnabled(false);
        requestEngineMove();
    } else {
        boardView_->setInputEnabled(true);
    }
    updateStatus();
}

void MainWindow::onMoveRequested(int fromFile, int fromRank, int toFile, int toRank) {
    if (!gameActive_ && engine_->isRunning()) return;

    QChar promo;
    if (board_.isPromotion(fromFile, fromRank, toFile, toRank)) {
        const QStringList opts{tr("Queen"), tr("Rook"), tr("Bishop"), tr("Knight")};
        bool ok = false;
        QString choice = QInputDialog::getItem(this, tr("Promotion"),
                                               tr("Promote to:"), opts, 0, false, &ok);
        if (!ok) return;
        promo = QChar(QLatin1Char("qrbn"[opts.indexOf(choice)]));
    }

    QString uci = GuiBoard::toUci(fromFile, fromRank, toFile, toRank, promo);
    if (!board_.applyUci(uci)) return;

    moves_ << uci;
    boardView_->setLastMove(fromFile, fromRank, toFile, toRank);
    boardView_->setBoard(&board_);
    log(tr("> %1").arg(uci));
    if (recordPositionAndCheckDraw()) return;

    if (gameActive_ && engine_->isRunning() && engineToMove()) {
        boardView_->setInputEnabled(false);
        requestEngineMove();
    }
    // No engine: stay in free play (input remains enabled for the other side).
    updateStatus();
}

void MainWindow::requestEngineMove() {
    engineThinking_ = true;
    searchGameId_   = gameId_;   // tag this search with the current game
    engine_->searchFromStart(moves_, useMovetime_ ? 0 : depth_,
                             useMovetime_ ? movetimeMs_ : 0);
}

bool MainWindow::recordPositionAndCheckDraw() {
    int count = ++posCount_[board_.key()];
    QString reason;
    if (count >= 3)                          reason = tr("Draw by threefold repetition.");
    else if (board_.halfmoveClock() >= 100)  reason = tr("Draw by the fifty-move rule.");
    if (reason.isEmpty()) return false;

    gameActive_ = false;
    boardView_->setInputEnabled(false);
    log(reason);
    QMessageBox::information(this, tr("Game over"), reason);
    updateStatus();
    return true;
}

void MainWindow::onBestMove(const QString& uci) {
    // Ignore a bestmove that belongs to a previous game (the id moved on) or that
    // arrives when we aren't waiting for one (e.g. after the game already ended).
    if (searchGameId_ != gameId_ || !engineThinking_)
        return;
    engineThinking_ = false;

    if (uci == QLatin1String("(none)") || uci == QLatin1String("0000")) {
        gameActive_ = false;
        boardView_->setInputEnabled(false);
        QMessageBox::information(this, tr("Game over"),
                                 tr("The engine reports no legal moves."));
        updateStatus();
        return;
    }

    int fromF = uci[0].toLatin1() - 'a', fromR = uci[1].toLatin1() - '1';
    int toF = uci[2].toLatin1() - 'a', toR = uci[3].toLatin1() - '1';
    if (!board_.applyUci(uci)) {
        log(tr("! could not apply engine move: %1").arg(uci));
        return;
    }
    moves_ << uci;
    boardView_->setLastMove(fromF, fromR, toF, toR);
    boardView_->setBoard(&board_);
    log(tr("< %1").arg(uci));
    if (recordPositionAndCheckDraw()) return;
    boardView_->setInputEnabled(true);
    updateStatus();
}

void MainWindow::onInfoLine(const QString& line) {
    // Surface depth/score/pv lines; skip noisy currmove spam.
    if (line.contains(QLatin1String(" score ")) || line.contains(QLatin1String(" pv ")))
        log(line);
}

void MainWindow::onEngineError(const QString& message) {
    log(tr("[engine error] %1").arg(message));
}

void MainWindow::log(const QString& s) {
    if (logView_) logView_->appendPlainText(s);
}

void MainWindow::updateStatus() {
    QString who = (board_.sideToMove() == GuiBoard::Color::White) ? tr("White") : tr("Black");
    QString eng = engine_->isRunning() ? tr("engine ready") : tr("no engine");
    statusBar()->showMessage(tr("%1 to move  |  %2").arg(who, eng));
}
