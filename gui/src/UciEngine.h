#pragma once
// =============================================================================
// UciEngine - drives an external UCI engine process over stdin/stdout.
// The GUI never links the engine; it launches the engine executable and speaks
// the UCI text protocol to it. This is the decoupling boundary of the project.
// =============================================================================

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class UciEngine : public QObject {
    Q_OBJECT
public:
    explicit UciEngine(QObject* parent = nullptr);
    ~UciEngine() override;

    // Launch the engine binary and run the uci/isready handshake.
    bool start(const QString& enginePath);
    void stop();
    bool isRunning() const;

    // Set the position from the start position plus a list of UCI moves, then
    // ask the engine to search. Exactly one of depth/movetimeMs should be > 0.
    void searchFromStart(const QStringList& moves, int depth, int movetimeMs);

    void sendStop();   // tell the engine to stop searching now

signals:
    void ready();                       // engine answered uciok/readyok
    void bestMove(const QString& uci);  // engine returned "bestmove <uci>"
    void infoLine(const QString& line); // raw "info ..." line (depth/score/pv)
    void engineError(const QString& message);

private slots:
    void onReadyRead();
    void onErrorOccurred(QProcess::ProcessError error);

private:
    void send(const QString& command);

    QProcess proc_;
    QString  buffer_;
};
