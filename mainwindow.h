#pragma once

#include "tr4131.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QPointF>
#include <QTimer>
#include <QTranslator>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QMenu;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QwtPlot;
class QwtPlotCurve;
class QwtPlotGrid;
class QwtPlotMarker;
class QwtPlotZoomer;
class SpectrogramView;
class Spectrogram3DView;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildInterface();
    void createMenus();
    void installLanguage(const QString &code);
    void changeLanguage(const QString &code);
    void showAbout();
    void checkForUpdates();
    void checkPrivateRelease();
    void finishUpdateCheck(const QByteArray &data, const QString &error = {});
    void editConnection();
    void savePreferences();
    void setDarkTheme(bool dark);
    void updatePlotAppearance();
    QWidget *createMeasurementPanel();
    QWidget *createSettingsPanel();
    QWidget *createPlotPanel();
    QFormLayout *addControlTab(QTabWidget *tabs, const QString &title);
    void createFrequencyTab(QTabWidget *tabs);
    void createLevelTab(QTabWidget *tabs);
    void createSweepTab(QTabWidget *tabs);
    void createMarkerTab(QTabWidget *tabs);
    void createMemoryTab(QTabWidget *tabs);
    QPushButton *deviceButton(const QString &label, const QByteArrayList &commands,
                              DeviceOperation operation = DeviceOperation::Acquire,
                              const QString &confirmation = {});
    GpibAddress selectedAddress() const;
    bool connectionSelected() const;
    void showConnectionStatus();
    QString connectionText(const GpibAddress &target) const;
    void startOperation(DeviceOperation operation, const QByteArrayList &commands = {},
                        const QString &label = {}, bool automatic = false);
    void finishRead();
    QByteArrayList pendingSettings() const;
    void applyFrequency(double centerMHz, double spanMHz, std::optional<double> rbwMHz = std::nullopt);
    void applyZoomFrequency();
    void applySettings();
    void syncSettings();
    void showSweep(Sweep sweep, bool keepZoom = false);
    void updateControls();
    void updateRefreshTimer();
    void updateTraces();
    void moveMeasurementCursor(const QPointF &position);
    void updateMeasurementCursor();
    void showPeak();
    void savePlot();
    void saveCsv();
    void setStatus(const QString &message, bool error = false);

    QMenu *languageMenu = nullptr;
    QAction *updateAction = nullptr;
    bool updateCheckInProgress = false;
    QAction *saveAction = nullptr;
    QAction *csvAction = nullptr;
    QAction *connectionSettingsAction = nullptr;
    int gpibBoard = 0;
    int gpibAddress = 3;
    bool useElektor = false;
    QString elektorPort;
    bool darkTheme = true;
    QDoubleSpinBox *refreshInterval = nullptr;
    QCheckBox *autoRefresh = nullptr;
    QTabWidget *deviceTabs = nullptr;
    QTabWidget *plotViews = nullptr;
    SpectrogramView *spectrogram = nullptr;
    Spectrogram3DView *spectrogram3D = nullptr;
    QPushButton *acquireButton = nullptr;
    QPushButton *applyButton = nullptr;
    QPushButton *discardButton = nullptr;
    QPushButton *singleSweepButton = nullptr;
    QPushButton *fullSpanButton = nullptr;
    QPushButton *zoomFrequencyButton = nullptr;
    QPushButton *peakButton = nullptr;
    QPushButton *zoomButton = nullptr;
    QPushButton *saveButton = nullptr;
    QDoubleSpinBox *centerInput = nullptr;
    QComboBox *spanInput = nullptr;
    QComboBox *rbwInput = nullptr;
    QDoubleSpinBox *referenceInput = nullptr;
    QComboBox *unitInput = nullptr;
    QComboBox *scaleInput = nullptr;
    QComboBox *attenuationInput = nullptr;
    QComboBox *sweepTimeInput = nullptr;
    QComboBox *videoInput = nullptr;
    QComboBox *triggerInput = nullptr;
    QComboBox *slotInput = nullptr;
    QList<QWidget *> parameterInputs;
    QList<QPushButton *> deviceButtons;
    QList<QPushButton *> busButtons;
    QLabel *detailsLabel = nullptr;
    QLabel *markerLabel = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *plotNote = nullptr;
    QLabel *cursorReadout = nullptr;
    QCheckBox *writeCheck = nullptr;
    QCheckBox *traceCheck = nullptr;
    QwtPlot *plot = nullptr;
    QwtPlotGrid *plotGrid = nullptr;
    QwtPlotCurve *writeCurve = nullptr;
    QwtPlotCurve *traceCurve = nullptr;
    QwtPlotMarker *peakMarker = nullptr;
    QwtPlotMarker *instrumentMarker = nullptr;
    QwtPlotMarker *measurementCursor = nullptr;
    QwtPlotZoomer *zoomer = nullptr;

    QFutureWatcher<SweepResult> reader;
    QTimer refreshTimer;
    int refreshRetryDelayMs = 0;
    QTranslator appTranslator;
    QTranslator qtTranslator;
    QString language;
    Sweep lastSweep;
    std::optional<QPointF> plotPress;
    std::optional<QPointF> cursorPosition;
    GpibAddress measuredAddress;
    DeviceOperation activeOperation = DeviceOperation::Acquire;
    QString activeLabel;
    bool settingsCurrent = false;
    bool reading = false;
    bool automaticRead = false;
    bool writeOnlyRead = false;
    bool syncing = false;
};
