#pragma once
// =============================================================================
// BoardWidget - paints the chessboard + pieces and turns clicks into move
// requests. It renders a GuiBoard but enforces no rules; it only restricts the
// first click to pieces of the human's color while input is enabled.
// Click a source square, then a destination -> emits moveRequested().
// =============================================================================

#include <QWidget>
#include "GuiBoard.h"

class BoardWidget : public QWidget {
    Q_OBJECT
public:
    explicit BoardWidget(QWidget* parent = nullptr);

    void setBoard(const GuiBoard* board) { board_ = board; update(); }
    void setOrientation(GuiBoard::Color bottom) { bottom_ = bottom; update(); }
    void setHumanColor(GuiBoard::Color c) { humanColor_ = c; }
    void setInputEnabled(bool on) { inputEnabled_ = on; if (!on) clearSelection(); update(); }
    void setLastMove(int fromFile, int fromRank, int toFile, int toRank);
    void clearLastMove();

    QSize sizeHint() const override { return QSize(560, 560); }
    int heightForWidth(int w) const override { return w; }

signals:
    void moveRequested(int fromFile, int fromRank, int toFile, int toRank);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    void clearSelection() { selFile_ = selRank_ = -1; }
    // Convert a pixel point to a board square (accounts for orientation).
    bool pointToSquare(const QPoint& p, int& file, int& rank) const;
    QRect squareRect(int file, int rank) const;

    const GuiBoard* board_ = nullptr;
    GuiBoard::Color bottom_ = GuiBoard::Color::White;
    GuiBoard::Color humanColor_ = GuiBoard::Color::White;
    bool inputEnabled_ = false;

    int selFile_ = -1, selRank_ = -1;          // currently selected source
    int lastFromFile_ = -1, lastFromRank_ = -1; // last-move highlight
    int lastToFile_ = -1, lastToRank_ = -1;
};
