#include "spectrogram.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPalette>
#include <QPen>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <qwt_color_map.h>
#include <qwt_interval.h>
#include <qwt_matrix_raster_data.h>
#include <qwt_plot.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_marker.h>
#include <qwt_plot_spectrogram.h>
#include <qwt_scale_widget.h>

#include <algorithm>
#include <cmath>

QwtLinearColorMap *spectrogramColorMap()
{
    auto *map = new QwtLinearColorMap(QColor("#252d66"), QColor("#fff28e"));
    map->addColorStop(0.25, QColor("#365fa4"));
    map->addColorStop(0.50, QColor("#2495ac"));
    map->addColorStop(0.70, QColor("#4abfa4"));
    map->addColorStop(0.86, QColor("#a9d96f"));
    return map;
}


SpectrogramView::SpectrogramView(int depth, QWidget *parent)
    : QFrame(parent), sourceLabel(new QLabel), depthLabel(new QLabel), summary(new QLabel),
      cursorReadout(new QLabel), depthEditor(new QSpinBox), clearButton(new QPushButton),
      plot(new QwtPlot), raster(new QwtPlotSpectrogram(QStringLiteral("WRITE"))),
      data(new QwtMatrixRasterData), cursorOutline(new QwtPlotMarker), cursor(new QwtPlotMarker)
{
    setObjectName(QStringLiteral("plotPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 10);
    layout->setSpacing(9);

    auto *toolbar = new QHBoxLayout;
    sourceLabel->setText(QStringLiteral("WRITE"));
    sourceLabel->setObjectName(QStringLiteral("spectrogramSource"));
    toolbar->addWidget(sourceLabel);
    toolbar->addStretch();
    depthEditor->setObjectName(QStringLiteral("spectrogramDepth"));
    depthEditor->setRange(1, 1000);
    depthEditor->setKeyboardTracking(false);
    depthEditor->setValue(depth);
    depthLabel->setBuddy(depthEditor);
    toolbar->addWidget(depthLabel);
    toolbar->addWidget(depthEditor);
    clearButton->setObjectName(QStringLiteral("clearSpectrogram"));
    toolbar->addWidget(clearButton);
    layout->addLayout(toolbar);

    plot->setObjectName(QStringLiteral("spectrogram"));
    plot->setAutoFillBackground(true);
    plot->setAxisVisible(QwtAxis::YRight);
    plot->setAxisMaxMajor(QwtAxis::YLeft, 8);
    plot->setAxisMaxMinor(QwtAxis::YLeft, 0);
    plot->setAxisMaxMajor(QwtAxis::YRight, 6);
    auto *scale = plot->axisWidget(QwtAxis::YRight);
    scale->setColorBarEnabled(true);
    scale->setColorBarWidth(14);
    if (auto *canvas = qobject_cast<QwtPlotCanvas *>(plot->canvas())) {
        canvas->setFrameStyle(QFrame::NoFrame);
    }
    raster->setData(data);
    raster->setColorMap(spectrogramColorMap());
    raster->setCachePolicy(QwtPlotRasterItem::PaintCache);
    raster->attach(plot);
    cursorOutline->setLinePen(QPen(QColor("#101925"), 3));
    cursorOutline->setZ(30);
    cursor->setLinePen(QPen(Qt::white, 1));
    cursor->setZ(31);
    for (auto *marker : {cursorOutline, cursor}) {
        marker->setLineStyle(QwtPlotMarker::Cross);
        marker->setVisible(false);
        marker->attach(plot);
    }
    plot->canvas()->setMouseTracking(true);
    plot->canvas()->installEventFilter(this);
    layout->addWidget(plot, 1);

    summary->setObjectName(QStringLiteral("quiet"));
    summary->setWordWrap(true);
    layout->addWidget(summary);
    cursorReadout->setObjectName(QStringLiteral("spectrogramCursorReadout"));
    cursorReadout->setTextFormat(Qt::PlainText);
    cursorReadout->setMinimumHeight(cursorReadout->fontMetrics().height());
    layout->addWidget(cursorReadout);
    connect(clearButton, &QPushButton::clicked, this, &SpectrogramView::clearHistory);
    connect(depthEditor, &QSpinBox::valueChanged, this, [this] {
        trimHistory();
        updateView();
    });
    retranslate();
}

int SpectrogramView::depth() const
{
    return depthEditor->value();
}

int SpectrogramView::commitDepth()
{
    depthEditor->interpretText();
    return depth();
}

SpectrogramSnapshot SpectrogramView::snapshot() const
{
    return {history, grid ? grid->start : 0.0, grid ? grid->stop : 4000.0,
            levelMin, levelMax, depth(), !grid || grid->frequency,
            grid ? grid->unit : QStringLiteral("dBm")};
}

void SpectrogramView::setDepth(int depth)
{
    depthEditor->setValue(depth);
}

bool SpectrogramView::hasHistory() const
{
    return !history.empty();
}

void SpectrogramView::addSweep(const Sweep &sweep, const GpibAddress &address)
{
    const Grid next{address, sweep.xStart(), sweep.xStop(), sweep.write.size(), sweep.spanHz > 0, sweep.unit};
    const double bottom = sweep.yStart();
    const double top = sweep.reference;
    if (next.samples < 2 || !std::isfinite(next.start) || !std::isfinite(next.stop)
        || next.stop <= next.start || !std::isfinite(bottom) || !std::isfinite(top) || top <= bottom) {
        return;
    }

    const bool wasEmpty = history.empty();
    if (grid != next) {
        history.clear();
        cursorPosition.reset();
        grid = next;
    }
    levelMin = bottom;
    levelMax = top;
    history.push_back(sweep.write);
    trimHistory();
    updateView();
    if (wasEmpty) {
        emit historyAvailabilityChanged();
    }
}

void SpectrogramView::trimHistory()
{
    while (history.size() > static_cast<std::size_t>(depth())) {
        history.pop_front();
    }
}

void SpectrogramView::clearHistory()
{
    history.clear();
    cursorPosition.reset();
    updateView();
    emit historyAvailabilityChanged();
}

void SpectrogramView::retranslate()
{
    depthLabel->setText(tr("Verlaufstiefe"));
    depthEditor->setToolTip(tr("Anzahl der Messungen im Spektrogramm"));
    clearButton->setText(tr("Verlauf leeren"));
    plot->setAxisTitle(QwtAxis::YLeft, tr("Messungen zurück"));
    updateView();
}

void SpectrogramView::setAppearance(const QPalette &palette, const QColor &canvasColor)
{
    plot->setPalette(palette);
    for (auto axis : {QwtAxis::XBottom, QwtAxis::YLeft, QwtAxis::YRight}) {
        plot->axisWidget(axis)->setPalette(palette);
    }
    plot->setCanvasBackground(canvasColor);
    if (isVisible()) {
        plot->replot();
    }
}

QPixmap SpectrogramView::grabPlot()
{
    if (rasterDirty) {
        updateRaster();
    }
    return plot->grab();
}

bool SpectrogramView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == plot->canvas()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick: {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton && mouse->modifiers() == Qt::NoModifier) {
                canvasPress = mouse->position();
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton) break;
            const bool click = canvasPress && (mouse->position() - *canvasPress).manhattanLength()
                    < QApplication::startDragDistance()
                && plot->canvas()->rect().contains(mouse->position().toPoint());
            canvasPress.reset();
            if (click && hasHistory()) {
                if (cursorPosition) {
                    cursorPosition.reset();
                    updateCursor();
                    plot->replot();
                } else {
                    moveCursor(mouse->position());
                }
            }
            break;
        }
        case QEvent::MouseMove: {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (cursorPosition && mouse->buttons() == Qt::NoButton
                && plot->canvas()->rect().contains(mouse->position().toPoint())) {
                moveCursor(mouse->position());
            }
            break;
        }
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void SpectrogramView::moveCursor(const QPointF &position)
{
    cursorPosition = QPointF(plot->invTransform(QwtAxis::XBottom, position.x()),
                             plot->invTransform(QwtAxis::YLeft, position.y()));
    updateCursor();
    plot->replot();
}

void SpectrogramView::updateCursor()
{
    const bool validRow = cursorPosition && hasHistory()
        && cursorPosition->y() >= -0.5 && cursorPosition->y() < static_cast<double>(history.size()) - 0.5;
    for (auto *marker : {cursorOutline, cursor}) {
        marker->setVisible(validRow);
    }
    if (!validRow) {
        cursorReadout->setText(cursorPosition ? tr("Cursor · Keine Messung in dieser Zeile.")
            : hasHistory() ? tr("Klicken für Messcursor.") : QString());
        return;
    }

    const auto row = static_cast<std::size_t>(std::floor(cursorPosition->y() + 0.5));
    const double step = (grid->stop - grid->start) / (grid->samples - 1);
    const double x = std::clamp(cursorPosition->x(), grid->start, grid->stop);
    const auto column = std::clamp<qsizetype>(qRound64((x - grid->start) / step), 0, grid->samples - 1);
    const double point = grid->start + column * step;
    const double level = history[history.size() - 1 - row].at(column);
    for (auto *marker : {cursorOutline, cursor}) {
        marker->setValue(point, static_cast<double>(row));
    }

    const auto format = locale();
    cursorReadout->setText(tr("Cursor · %1 %2 · %3 %4 · Messzeile %5")
        .arg(format.toString(point, 'g', 8), grid->frequency ? QStringLiteral("MHz") : QStringLiteral("ms"),
             format.toString(level, 'g', 8), grid->unit).arg(row));
}

void SpectrogramView::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    if (rasterDirty) {
        updateRaster();
    }
}

void SpectrogramView::updateView()
{
    clearButton->setEnabled(hasHistory());
    summary->setText(hasHistory()
        ? tr("%1 / %2 Messungen · Neueste oben").arg(history.size()).arg(depth())
        : tr("Messung abrufen, um den WRITE-Verlauf zu starten."));
    rasterDirty = true;
    if (isVisible()) {
        updateRaster();
    }
    emit historyChanged();
}

void SpectrogramView::updateRaster()
{
    const QwtInterval levels(levelMin, levelMax);
    auto *scale = plot->axisWidget(QwtAxis::YRight);
    if (scale->colorBarInterval() != levels) {
        scale->setColorMap(levels, spectrogramColorMap());
    }
    plot->setAxisScale(QwtAxis::YRight, levelMin, levelMax);
    plot->setAxisTitle(QwtAxis::YRight, tr("Pegel [%1]").arg(grid ? grid->unit : QStringLiteral("dBm")));
    plot->setAxisTitle(QwtAxis::XBottom, !grid || grid->frequency ? tr("Frequenz [MHz]") : tr("Zeit [ms]"));
    plot->setAxisScale(QwtAxis::XBottom, grid ? grid->start : 0, grid ? grid->stop : 4000);
    plot->setAxisScale(QwtAxis::YLeft, depth() - 0.5, -0.5, std::max(1, (depth() + 7) / 8));

    if (hasHistory()) {
        const double step = (grid->stop - grid->start) / (grid->samples - 1);
        data->setInterval(Qt::XAxis, QwtInterval(grid->start - step / 2, grid->stop + step / 2));
        data->setInterval(Qt::YAxis, QwtInterval(-0.5, static_cast<double>(history.size()) - 0.5));
        data->setInterval(Qt::ZAxis, levels);
        QVector<double> matrix;
        matrix.reserve(static_cast<qsizetype>(history.size()) * grid->samples);
        for (auto row = history.crbegin(); row != history.crend(); ++row) {
            matrix.append(*row);
        }
        data->setValueMatrix(matrix, static_cast<int>(grid->samples));
    } else {
        data->setValueMatrix({}, 1);
    }
    raster->setVisible(hasHistory());
    raster->invalidateCache();
    updateCursor();
    plot->replot();
    rasterDirty = false;
}
