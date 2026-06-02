#include "BoardWidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QSvgRenderer>

BoardWidget::BoardWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 320);
}

QSvgRenderer* BoardWidget::rendererFor(char piece) {
    auto it = renderers_.find(piece);
    if (it != renderers_.end()) return it.value();

    // ":/pieces/wK.svg" .. ":/pieces/bP.svg" (w/b prefix + upper piece letter).
    const QChar c = QChar(piece).toUpper();
    const QString path = QStringLiteral(":/pieces/%1%2.svg")
                             .arg(GuiBoard::isWhite(piece) ? 'w' : 'b').arg(c);
    auto* r = new QSvgRenderer(path, this);
    QSvgRenderer* result = r->isValid() ? r : nullptr;
    if (!result) r->deleteLater();
    renderers_.insert(piece, result);          // cache the result (incl. nullptr)
    return result;
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
                    QSvgRenderer* svg = rendererFor(piece);
                    if (svg) {
                        // Render the SVG sprite centred with a small margin so it
                        // doesn't touch the square edges.
                        const qreal m = cell * 0.06;
                        svg->render(&g, QRectF(r).adjusted(m, m, -m, -m));
                    } else {
                        // Fallback (sprite missing): the old Unicode glyph with a
                        // contrasting outline so it reads on any square.
                        const bool white = GuiBoard::isWhite(piece);
                        const QString sym = GuiBoard::glyph(QChar(piece).toLower().toLatin1());
                        const QColor fill    = white ? QColor(0xF6, 0xF6, 0xF4)
                                                     : QColor(0x1C, 0x1C, 0x1C);
                        const QColor outline = white ? QColor(0x10, 0x10, 0x10)
                                                     : QColor(0xD0, 0xD0, 0xD0);
                        const int o = qMax(1, cell / 28);
                        g.setPen(outline);
                        for (int dx = -1; dx <= 1; ++dx)
                            for (int dy = -1; dy <= 1; ++dy)
                                if (dx || dy)
                                    g.drawText(r.translated(dx * o, dy * o),
                                               Qt::AlignCenter, sym);
                        g.setPen(fill);
                        g.drawText(r, Qt::AlignCenter, sym);
                    }
                }
            }
        }
    }

    // Legal-move markers for the selected piece, on top of everything: a centred
    // dot on empty targets, a ring around occupied ones (captures).
    if (board_ && !legalDests_.isEmpty()) {
        g.setRenderHint(QPainter::Antialiasing, true);
        for (const QPoint& d : legalDests_) {
            const QRectF sq = squareRect(d.x(), d.y());
            const bool occupied = board_->at(d.x(), d.y()) != '.';
            if (occupied) {
                QPen pen(QColor(0, 0, 0, 70));
                pen.setWidthF(qMax<qreal>(2.0, cell / 12.0));
                g.setPen(pen);
                g.setBrush(Qt::NoBrush);
                const qreal inset = cell * 0.08;
                g.drawEllipse(sq.adjusted(inset, inset, -inset, -inset));
            } else {
                g.setPen(Qt::NoPen);
                g.setBrush(QColor(0, 0, 0, 45));
                const qreal dia = cell * 0.30;
                g.drawEllipse(sq.center(), dia / 2.0, dia / 2.0);
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
            legalDests_ = board_->legalDestinations(file, rank);
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
