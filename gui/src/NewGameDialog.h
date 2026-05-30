#pragma once
// =============================================================================
// NewGameDialog - lets the user choose which color to play and how hard the
// engine should think (fixed search depth OR a time budget per move).
// =============================================================================

#include <QDialog>
#include "GuiBoard.h"

class QComboBox;
class QRadioButton;
class QSpinBox;

class NewGameDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewGameDialog(QWidget* parent = nullptr);

    GuiBoard::Color humanColor() const;  // resolves "Random" to an actual color
    bool useMovetime() const;            // true -> movetimeMs(), false -> depth()
    int depth() const;
    int movetimeMs() const;

private:
    QComboBox*    colorBox_   = nullptr;
    QRadioButton* depthRadio_ = nullptr;
    QSpinBox*     depthSpin_  = nullptr;
    QSpinBox*     timeSpin_   = nullptr;
};
