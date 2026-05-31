#include "UciEngine.h"

#include <QThread>

UciEngine::UciEngine(QObject* parent) : QObject(parent) {
    proc_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&proc_, &QProcess::readyReadStandardOutput, this, &UciEngine::onReadyRead);
    connect(&proc_, &QProcess::errorOccurred, this, &UciEngine::onErrorOccurred);
}

UciEngine::~UciEngine() {
    stop();
}

bool UciEngine::start(const QString& enginePath) {
    stop();
    proc_.start(enginePath, QStringList());
    if (!proc_.waitForStarted(3000)) {
        emit engineError(QStringLiteral("Failed to start engine: %1").arg(enginePath));
        return false;
    }
    send(QStringLiteral("uci"));

    // Use the hardware: run Lazy SMP on all but one core (leave one for the GUI
    // and OS). Engines that don't know the Threads option just ignore it.
    const int cores = QThread::idealThreadCount();
    const int threads = cores > 1 ? cores - 1 : 1;
    send(QStringLiteral("setoption name Threads value %1").arg(threads));

    send(QStringLiteral("isready"));
    return true;
}

void UciEngine::stop() {
    if (proc_.state() != QProcess::NotRunning) {
        send(QStringLiteral("quit"));
        if (!proc_.waitForFinished(1000))
            proc_.kill();
    }
    buffer_.clear();
}

bool UciEngine::isRunning() const {
    return proc_.state() != QProcess::NotRunning;
}

void UciEngine::newGame() {
    send(QStringLiteral("ucinewgame"));
}

void UciEngine::searchFromStart(const QStringList& moves, int depth, int movetimeMs) {
    QString pos = QStringLiteral("position startpos");
    if (!moves.isEmpty())
        pos += QStringLiteral(" moves ") + moves.join(QLatin1Char(' '));
    send(pos);

    if (movetimeMs > 0)
        send(QStringLiteral("go movetime %1").arg(movetimeMs));
    else
        send(QStringLiteral("go depth %1").arg(depth > 0 ? depth : 8));
}

void UciEngine::sendStop() {
    if (isRunning())
        send(QStringLiteral("stop"));
}

void UciEngine::send(const QString& command) {
    if (proc_.state() == QProcess::NotRunning) return;
    proc_.write(command.toUtf8() + '\n');
}

void UciEngine::onReadyRead() {
    buffer_ += QString::fromUtf8(proc_.readAllStandardOutput());
    int nl;
    while ((nl = buffer_.indexOf(QLatin1Char('\n'))) != -1) {
        QString line = buffer_.left(nl).trimmed();
        buffer_.remove(0, nl + 1);
        if (line.isEmpty()) continue;

        if (line == QLatin1String("uciok") || line == QLatin1String("readyok")) {
            emit ready();
        } else if (line.startsWith(QLatin1String("bestmove"))) {
            const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 2)
                emit bestMove(parts[1]);
        } else if (line.startsWith(QLatin1String("info"))) {
            emit infoLine(line);
        }
    }
}

void UciEngine::onErrorOccurred(QProcess::ProcessError) {
    emit engineError(proc_.errorString());
}
