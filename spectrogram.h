#pragma once

#include "tr4131.h"

#include <QFrame>
#include <QPixmap>
#include <QPointF>

#include <deque>
#include <optional>

class QLabel;
class QPushButton;
class QShowEvent;
class QSpinBox;
class QwtMatrixRasterData;
class QwtLinearColorMap;
class QwtPlot;
class QwtPlotMarker;
class QwtPlotSpectrogram;

QwtLinearColorMap *spectrogramColorMap();

struct SpectrogramSnapshot
{
    const std::deque<QVector<double>> &sweeps;
    double start;
    double stop;
    double levelMin;
    double levelMax;
    int depth;
    bool frequency;
    QString unit;
};

class SpectrogramView final : public QFrame
{
    Q_OBJECT

public:
    explicit SpectrogramView(int depth, QWidget *parent = nullptr);

    void addSweep(const Sweep &sweep, const GpibAddress &address);
    void setAppearance(const QPalette &palette, const QColor &canvasColor);
    void retranslate();
    int depth() const;
    int commitDepth();
    bool hasHistory() const;
    QPixmap grabPlot();
    SpectrogramSnapshot snapshot() const;
    void setDepth(int depth);
    void clearHistory();

signals:
    void historyAvailabilityChanged();
    void historyChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    struct Grid
    {
        GpibAddress address;
        double start;
        double stop;
        qsizetype samples;
        bool frequency;
        QString unit;

        bool operator==(const Grid &other) const
        {
            return address == other.address && start == other.start && stop == other.stop
                && samples == other.samples && frequency == other.frequency && unit == other.unit;
        }
    };

    void trimHistory();
    void updateView();
    void updateRaster();
    void moveCursor(const QPointF &position);
    void updateCursor();

    std::optional<Grid> grid;
    std::deque<QVector<double>> history;
    std::optional<QPointF> canvasPress;
    std::optional<QPointF> cursorPosition;
    double levelMin = -80;
    double levelMax = 0;
    bool rasterDirty = true;

    QLabel *sourceLabel;
    QLabel *depthLabel;
    QLabel *summary;
    QLabel *cursorReadout;
    QSpinBox *depthEditor;
    QPushButton *clearButton;
    QwtPlot *plot;
    QwtPlotSpectrogram *raster;
    QwtMatrixRasterData *data;
    QwtPlotMarker *cursorOutline;
    QwtPlotMarker *cursor;
};
