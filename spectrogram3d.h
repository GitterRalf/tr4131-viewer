#pragma once

#include <QFrame>
#include <QPixmap>

class QLabel;
class QPushButton;
class QSpinBox;
class SpectrogramView;
class SurfaceCanvas;

class Spectrogram3DView final : public QFrame
{
    Q_OBJECT

public:
    explicit Spectrogram3DView(SpectrogramView &history, QWidget *parent = nullptr);

    void setAppearance(const QPalette &palette, const QColor &canvasColor);
    void retranslate();
    QPixmap grabPlot();
    int commitDepth();

private:
    void updateHistory();

    SpectrogramView &history;
    QLabel *depthLabel;
    QLabel *summary;
    QLabel *instructions;
    QSpinBox *depthEditor;
    QPushButton *clearButton;
    QPushButton *resetButton;
    SurfaceCanvas *canvas;
};
