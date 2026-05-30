#include "MainWindow.h"

#include "BoardWidget.h"
#include "UciEngine.h"
#include "NewGameDialog.h"

#include <QApplication>
#include <QMenuBar>
#include <QMenu>
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

    board_.reset();
    moves_.clear();
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

    if (gameActive_ && engine_->isRunning() && engineToMove()) {
        boardView_->setInputEnabled(false);
        requestEngineMove();
    }
    // No engine: stay in free play (input remains enabled for the other side).
    updateStatus();
}

void MainWindow::requestEngineMove() {
    engine_->searchFromStart(moves_, useMovetime_ ? 0 : depth_,
                             useMovetime_ ? movetimeMs_ : 0);
}

void MainWindow::onBestMove(const QString& uci) {
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
    boardView_->setInputEnabled(true);
    log(tr("< %1").arg(uci));
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
