#include "NewGameDialog.h"

#include <QComboBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QRandomGenerator>

NewGameDialog::NewGameDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("New Game"));

    colorBox_ = new QComboBox(this);
    colorBox_->addItem(tr("White"));
    colorBox_->addItem(tr("Black"));
    colorBox_->addItem(tr("Random"));

    // --- Engine strength: fixed depth OR time per move ---
    depthRadio_ = new QRadioButton(tr("Fixed depth (plies)"), this);
    auto* timeRadio = new QRadioButton(tr("Time per move (ms)"), this);
    depthRadio_->setChecked(true);

    depthSpin_ = new QSpinBox(this);
    depthSpin_->setRange(1, 64);
    depthSpin_->setValue(8);

    timeSpin_ = new QSpinBox(this);
    timeSpin_->setRange(10, 600000);
    timeSpin_->setSingleStep(100);
    timeSpin_->setValue(1000);
    timeSpin_->setEnabled(false);

    connect(depthRadio_, &QRadioButton::toggled, this, [this](bool on) {
        depthSpin_->setEnabled(on);
        timeSpin_->setEnabled(!on);
    });

    auto* strengthBox = new QGroupBox(tr("Engine strength"), this);
    auto* sl = new QFormLayout(strengthBox);
    sl->addRow(depthRadio_, depthSpin_);
    sl->addRow(timeRadio, timeSpin_);

    auto* form = new QFormLayout;
    form->addRow(tr("Play as:"), colorBox_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(strengthBox);
    root->addWidget(buttons);
}

GuiBoard::Color NewGameDialog::humanColor() const {
    switch (colorBox_->currentIndex()) {
        case 0: return GuiBoard::Color::White;
        case 1: return GuiBoard::Color::Black;
        default:
            return (QRandomGenerator::global()->bounded(2) == 0)
                       ? GuiBoard::Color::White
                       : GuiBoard::Color::Black;
    }
}

bool NewGameDialog::useMovetime() const { return !depthRadio_->isChecked(); }
int  NewGameDialog::depth() const       { return depthSpin_->value(); }
int  NewGameDialog::movetimeMs() const  { return timeSpin_->value(); }
