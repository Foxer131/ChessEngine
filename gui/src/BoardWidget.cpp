#include "BoardWidget.h"

#include <QPainter>
#include <QMouseEvent>

BoardWidget::BoardWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 320);
}

void BoardWidget::setLastMove(int fromFile, int fromRank, int toFile, int toRank) {
    lastFromFile_ = fromFile; lastFromRank_ = fromRank;
    lastToFile_ = toFile;     lastToRank_ = toRank;
    update();
}

void BoardWidget::clearLastMove() {
    lastFromFile_ = lastFromRank_ = lastToFile_ = lastToRank_ = -1;
    update();
}

QRect BoardWidget::squareRect(int file, int rank) const {
    const int side = qMin(width(), height());
    const int cell = side / 8;
    const int ox = (width() - cell * 8) / 2;
    const int oy = (height() - cell * 8) / 2;
    // Orientation: the human's color sits at the bottom.
    int col = (bottom_ == GuiBoard::Color::White) ? file : 7 - file;
    int row = (bottom_ == GuiBoard::Color::White) ? 7 - rank : rank;
    return QRect(ox + col * cell, oy + row * cell, cell, cell);
}

bool BoardWidget::pointToSquare(const QPoint& p, int& file, int& rank) const {
    const int side = qMin(width(), height());
    const int cell = side / 8;
    if (cell <= 0) return false;
    const int ox = (width() - cell * 8) / 2;
    const int oy = (height() - cell * 8) / 2;
    int col = (p.x() - ox) / cell;
    int row = (p.y() - oy) / cell;
    if (col < 0 || col > 7 || row < 0 || row > 7) return false;
    file = (bottom_ == GuiBoard::Color::White) ? col : 7 - col;
    rank = (bottom_ == GuiBoard::Color::White) ? 7 - row : row;
    return true;
}

void BoardWidget::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);

    const QColor light(0xf0, 0xd9, 0xb5);
    const QColor dark(0xb5, 0x88, 0x63);
    const QColor sel(0x6f, 0xb0, 0x4f, 180);
    const QColor last(0xcd, 0xd2, 0x6a, 160);

    const int side = qMin(width(), height());
    const int cell = side / 8;
    QFont f = g.font();
    f.setPixelSize(static_cast<int>(cell * 0.72));
    g.setFont(f);

    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            QRect r = squareRect(file, rank);
            bool lightSq = ((file + rank) % 2) != 0;
            g.fillRect(r, lightSq ? light : dark);

            if (file == selFile_ && rank == selRank_)
                g.fillRect(r, sel);
            else if ((file == lastFromFile_ && rank == lastFromRank_) ||
                     (file == lastToFile_ && rank == lastToRank_))
                g.fillRect(r, last);

            if (board_) {
                char piece = board_->at(file, rank);
                if (piece != '.') {
                    g.setPen(GuiBoard::isWhite(piece) ? Qt::white : Qt::black);
                    g.drawText(r, Qt::AlignCenter, GuiBoard::glyph(piece));
                }
            }
        }
    }
}

void BoardWidget::mousePressEvent(QMouseEvent* e) {
    if (!inputEnabled_ || !board_) return;
    int file, rank;
    if (!pointToSquare(e->pos(), file, rank)) return;

    if (selFile_ < 0) {
        // First click: only select a piece of the human's color.
        char p = board_->at(file, rank);
        if (p != '.' && GuiBoard::colorOf(p) == humanColor_) {
            selFile_ = file; selRank_ = rank;
            update();
        }
        return;
    }

    // Second click: clicking the same square cancels; otherwise request a move.
    if (file == selFile_ && rank == selRank_) {
        clearSelection();
        update();
        return;
    }
    int ff = selFile_, fr = selRank_;
    clearSelection();
    update();
    emit moveRequested(ff, fr, file, rank);
}
