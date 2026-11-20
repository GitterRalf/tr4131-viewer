#include "mainwindow.h"
#include "spectrogram.h"
#include "spectrogram3d.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QPalette>
#include <QSettings>
#include <QSignalBlocker>
#include <QPen>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QStack>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <qwt_plot.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_plot_marker.h>
#include <qwt_plot_zoomer.h>
#include <qwt_scale_widget.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {

constexpr int firstRefreshRetryMs = 1000;
constexpr int maxRefreshRetryMs = 8000;

struct SettingOption
{
    double mhz;
    const char *command;
};

constexpr std::array spans {
    SettingOption{4000, "SP4GZ"}, SettingOption{2000, "SP2GZ"}, SettingOption{1000, "SP1GZ"},
    SettingOption{500, "SP500MZ"}, SettingOption{200, "SP200MZ"}, SettingOption{100, "SP100MZ"},
    SettingOption{50, "SP50MZ"}, SettingOption{20, "SP20MZ"}, SettingOption{10, "SP10MZ"},
    SettingOption{5, "SP5MZ"}, SettingOption{2, "SP2MZ"}, SettingOption{1, "SP1MZ"},
    SettingOption{0.5, "SP500KZ"}, SettingOption{0.2, "SP200KZ"}, SettingOption{0.1, "SP100KZ"},
    SettingOption{0.05, "SP50KZ"}, SettingOption{0, "ZS"}
};
constexpr std::array bandwidths {
    SettingOption{1, "RB1MZ"}, SettingOption{0.3, "RB300KZ"}, SettingOption{0.1, "RB100KZ"},
    SettingOption{0.03, "RB30KZ"}, SettingOption{0.01, "RB10KZ"},
    SettingOption{0.003, "RB3KZ"}, SettingOption{0.001, "RB1KZ"}
};
constexpr std::array videoFilters {
    SettingOption{1, "VF1MZ"}, SettingOption{0.3, "VF300KZ"}, SettingOption{0.1, "VF100KZ"},
    SettingOption{0.01, "VF10KZ"}, SettingOption{0.001, "VF1KZ"}, SettingOption{0.0001, "VF100HZ"},
    SettingOption{0.00001, "VF10HZ"}, SettingOption{0.000001, "VF1HZ"}
};
constexpr std::array sweepTimes {
    SettingOption{0.005, "ST5MS"}, SettingOption{0.01, "ST10MS"}, SettingOption{0.02, "ST20MS"},
    SettingOption{0.05, "ST50MS"}, SettingOption{0.1, "ST100MS"}, SettingOption{0.2, "ST200MS"},
    SettingOption{0.5, "ST500MS"}, SettingOption{1, "ST1S"}, SettingOption{2, "ST2S"},
    SettingOption{5, "ST5S"}, SettingOption{10, "ST10S"}, SettingOption{20, "ST20S"},
    SettingOption{50, "ST50S"}, SettingOption{100, "ST100S"}
};
constexpr int commandRole = Qt::UserRole + 1;

QString numeric(double value, int precision = 8)
{
    return QLocale().toString(value, 'g', precision);
}

QString frequency(double hz)
{
    if (std::abs(hz) >= 1e9) return numeric(hz / 1e9) + QStringLiteral(" GHz");
    if (std::abs(hz) >= 1e6) return numeric(hz / 1e6) + QStringLiteral(" MHz");
    if (std::abs(hz) >= 1e3) return numeric(hz / 1e3) + QStringLiteral(" kHz");
    return numeric(hz) + QStringLiteral(" Hz");
}

QString defaultExportName(const QString &extension)
{
    const auto timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    return QStringLiteral("TR4131_%1.%2").arg(timestamp, extension);
}

QString sweepTime(double seconds)
{
    return seconds < 1 ? numeric(seconds * 1000) + MainWindow::tr(" ms/Div")
                       : numeric(seconds) + MainWindow::tr(" s/Div");
}

void addChoice(QComboBox *box, const QString &label, double value, const QByteArray &command)
{
    box->addItem(label, value);
    box->setItemData(box->count() - 1, command, commandRole);
}

template<std::size_t Size, typename Label>
void fillOptions(QComboBox *box, const std::array<SettingOption, Size> &options, Label label)
{
    for (const auto &option : options) {
        addChoice(box, label(option.mhz), option.mhz, option.command);
    }
}

QString frequencyChoice(double mhz)
{
    return mhz ? frequency(mhz * 1e6) : MainWindow::tr("Zero Span");
}

void selectValue(QComboBox *box, double value, const QString &label)
{
    // The temporary entry preserves readback values specific to a device revision.
    if (box->count() && !box->itemData(box->count() - 1, commandRole).isValid()) {
        box->removeItem(box->count() - 1);
    }

    const int index = box->findData(value);
    if (index < 0) {
        box->addItem(label, value);
        box->setCurrentIndex(box->count() - 1);
    } else {
        box->setCurrentIndex(index);
    }
}

void appendChoice(QByteArrayList &commands, const QComboBox *box, double current)
{
    const auto command = box->currentData(commandRole).toByteArray();
    if (!command.isEmpty() && box->currentData().toDouble() != current) {
        commands.append(command);
    }
}

QByteArray decimalCommand(double value, int decimals)
{
    auto text = QByteArray::number(value, 'f', decimals);
    while (text.endsWith('0') && text.contains('.')) text.chop(1);
    if (text.endsWith('.')) text.chop(1);
    return text;
}

QIcon globeIcon(bool dark)
{
    QPixmap icon(24, 24);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(dark ? "#d4e3f5" : "#27425a"), 1.35));
    painter.drawEllipse(QRectF(3, 3, 18, 18));
    painter.drawEllipse(QRectF(8, 3, 8, 18));
    painter.drawLine(QPointF(3, 12), QPointF(21, 12));
    painter.drawLine(QPointF(5, 7.5), QPointF(19, 7.5));
    painter.drawLine(QPointF(5, 16.5), QPointF(19, 16.5));
    return QIcon(icon);
}

QWidget *buttonPair(QPushButton *left, QPushButton *right)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(left, 1);
    layout->addWidget(right, 1);
    return row;
}

bool isPlotDrag(const QPointF &first, const QPointF &last)
{
    return (last - first).manhattanLength() >= QApplication::startDragDistance();
}

class SpectrumZoomer final : public QwtPlotZoomer
{
public:
    using QwtPlotZoomer::QwtPlotZoomer;

protected:
    bool accept(QPolygon &selection) const override
    {
        return selection.size() >= 2 && isPlotDrag(selection.first(), selection.last())
            && QwtPlotZoomer::accept(selection);
    }
};

QVector<QPointF> plotPoints(const Sweep &sweep, const QVector<double> &levels)
{
    QVector<QPointF> points;
    points.reserve(levels.size());
    const double start = sweep.xStart();
    const double step = (sweep.xStop() - start) / std::max<qsizetype>(levels.size() - 1, 1);
    for (qsizetype i = 0; i < levels.size(); ++i) {
        points.append(QPointF(start + i * step, levels[i]));
    }
    return points;
}

QLabel *quietLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("quiet"));
    label->setWordWrap(true);
    return label;
}

} // namespace

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("TR4131 Viewer"));
    setWindowIcon(QIcon(QStringLiteral(":/device-icon.ico")));
    resize(1250, 780);
    setMinimumSize(920, 650);
    const QSettings preferences;
    gpibBoard = std::clamp(preferences.value(QStringLiteral("gpib/board"), 0).toInt(), 0, 255);
    gpibAddress = std::clamp(preferences.value(QStringLiteral("gpib/address"), 3).toInt(), 0, 30);
    useElektor = preferences.value(QStringLiteral("gpib/elektor"), false).toBool();
    elektorPort = preferences.value(QStringLiteral("gpib/serial")).toString();
    const bool dark = preferences.value(QStringLiteral("appearance/dark"), true).toBool();
    setDarkTheme(dark);

    refreshTimer.setSingleShot(true);
    refreshTimer.setTimerType(Qt::PreciseTimer);
    connect(&refreshTimer, &QTimer::timeout, this, [this] {
        startOperation(DeviceOperation::Acquire, {}, {}, true);
    });
    connect(&reader, &QFutureWatcher<SweepResult>::finished, this, &MainWindow::finishRead);
    installLanguage(preferences.value(QStringLiteral("language"), QStringLiteral("de")).toString());
    buildInterface();
}

void MainWindow::buildInterface()
{
    deviceButtons.clear();
    busButtons.clear();
    parameterInputs.clear();

    auto *workspace = new QWidget;
    workspace->setObjectName(QStringLiteral("workspace"));
    setCentralWidget(workspace);
    auto *outer = new QVBoxLayout(workspace);
    outer->setContentsMargins(20, 16, 20, 16);
    outer->setSpacing(14);

    auto *header = new QLabel(QStringLiteral("Advantest TR4131"));
    header->setObjectName(QStringLiteral("title"));
    outer->addWidget(header);

    auto *content = new QHBoxLayout;
    content->setSpacing(14);
    plotViews = new QTabWidget;
    plotViews->setObjectName(QStringLiteral("plotViews"));
    plotViews->addTab(createPlotPanel(), tr("Spektrum"));
    if (!spectrogram) {
        spectrogram = new SpectrogramView(QSettings().value(QStringLiteral("spectrogram/depth"), 120).toInt());
        connect(spectrogram, &SpectrogramView::historyAvailabilityChanged, this, &MainWindow::updateControls);
    } else {
        spectrogram->retranslate();
    }
    plotViews->addTab(spectrogram, tr("Spektrogramm"));
    if (!spectrogram3D) {
        spectrogram3D = new Spectrogram3DView(*spectrogram);
    } else {
        spectrogram3D->retranslate();
    }
    plotViews->addTab(spectrogram3D, tr("3D-Spektrogramm"));
    content->addWidget(plotViews, 1);
    updatePlotAppearance();
    outer->addLayout(content, 1);

    auto *sidebar = new QWidget;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 4, 0);
    sidebarLayout->setSpacing(10);
    sidebarLayout->addWidget(createMeasurementPanel());
    sidebarLayout->addWidget(createSettingsPanel());
    sidebarLayout->addStretch();
    auto *scrollArea = new QScrollArea;
    scrollArea->setWidget(sidebar);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFixedWidth(366);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    content->addWidget(scrollArea);

    statusLabel = new QLabel;
    statusLabel->setWordWrap(true);
    outer->addWidget(statusLabel);

    createMenus();
    connect(plotViews, &QTabWidget::currentChanged, this, &MainWindow::updateControls);
    connect(acquireButton, &QPushButton::clicked, this, [this] { startOperation(DeviceOperation::Acquire); });
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::applySettings);
    connect(discardButton, &QPushButton::clicked, this, [this] {
        syncSettings();
        setStatus(tr("Eingaben verworfen. Die zuletzt gelesenen Gerätewerte werden angezeigt."));
    });
    connect(autoRefresh, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!enabled && refreshRetryDelayMs > 0) {
            refreshRetryDelayMs = 0;
            if (!reading) {
                setStatus(tr("Automatischer Abruf gestoppt."));
            }
        }
        updateControls();
    });
    connect(refreshInterval, &QDoubleSpinBox::valueChanged, this, [this] {
        refreshTimer.stop();
        updateControls();
    });
    showConnectionStatus();
    updateControls();
}

void MainWindow::createMenus()
{
    auto *bar = new QMenuBar;
    bar->setNativeMenuBar(false);
    if (auto *previous = qobject_cast<QMenuBar *>(menuWidget())) {
        if (auto *corner = previous->cornerWidget(Qt::TopRightCorner)) {
            previous->setCornerWidget(nullptr, Qt::TopRightCorner);
            corner->hide();
            corner->deleteLater();
        }
        previous->hide();
        previous->deleteLater();
    }
    setMenuBar(bar);

    auto *file = bar->addMenu(tr("&Datei"));
    csvAction = file->addAction(tr("Daten als CSV speichern …"), this, &MainWindow::saveCsv);
    saveAction = file->addAction(tr("PNG speichern …"), this, &MainWindow::savePlot);
    file->addSeparator();
    file->addAction(tr("&Beenden"), QKeySequence::Quit, this, &QWidget::close);

    auto *settings = bar->addMenu(tr("&Einstellungen"));
    settings->setObjectName(QStringLiteral("settingsMenu"));
    connectionSettingsAction = settings->addAction(tr("Verbindung …"), this, &MainWindow::editConnection);
    connectionSettingsAction->setObjectName(QStringLiteral("connectionSettings"));
    auto *appearance = settings->addMenu(tr("Darstellung"));
    auto *themes = new QActionGroup(appearance);
    for (bool dark : {false, true}) {
        auto *action = appearance->addAction(dark ? tr("Dunkel") : tr("Hell"));
        action->setObjectName(dark ? QStringLiteral("darkTheme") : QStringLiteral("lightTheme"));
        action->setCheckable(true);
        action->setChecked(dark == darkTheme);
        themes->addAction(action);
        connect(action, &QAction::triggered, this, [this, dark] { setDarkTheme(dark); });
    }
    settings->addSeparator();
    auto *save = settings->addAction(tr("Einstellungen speichern"), QKeySequence::Save,
                                     this, &MainWindow::savePreferences);
    save->setObjectName(QStringLiteral("saveSettings"));

    auto *right = new QMenuBar(bar);
    right->setNativeMenuBar(false);
    languageMenu = new QMenu(right);
    languageMenu->setTitle(tr("Sprache"));
    languageMenu->setIcon(globeIcon(darkTheme));
    right->addMenu(languageMenu);
    languageMenu->menuAction()->setText({});
    languageMenu->menuAction()->setToolTip(tr("Sprache wählen"));
    auto *languages = new QActionGroup(languageMenu);
    languages->setExclusive(true);
    const std::array choices{
        std::pair{QStringLiteral("de"), QStringLiteral("Deutsch")},
        std::pair{QStringLiteral("en"), QStringLiteral("English")},
        std::pair{QStringLiteral("fr"), QStringLiteral("Français")}
    };
    for (const auto &[code, label] : choices) {
        auto *action = languageMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(code == language);
        languages->addAction(action);
        connect(action, &QAction::triggered, this, [this, code] { changeLanguage(code); });
    }

    auto *about = right->addMenu(tr("Über"));
#ifdef TR4131_QT_COMMUNITY
    about->addAction(tr("Über Qt"), this, [this] {
        QMessageBox::aboutQt(this, tr("Über Qt"));
    });
#endif
    about->addAction(tr("Über"), this, &MainWindow::showAbout);
    about->addSeparator();
    updateAction = about->addAction(tr("Nach Updates suchen"), this, &MainWindow::checkForUpdates);
    updateAction->setObjectName(QStringLiteral("checkForUpdates"));
    updateAction->setEnabled(!updateCheckInProgress);
    bar->setCornerWidget(right, Qt::TopRightCorner);
}

void MainWindow::showAbout()
{
    QMessageBox dialog(this);
    dialog.setWindowTitle(tr("Über"));
    dialog.setTextFormat(Qt::RichText);
    dialog.setText(QStringLiteral("<b>TR4131 Viewer</b><br>%1<br><br><b>Ralf Tralow</b>")
                       .arg(tr("Version %1").arg(QCoreApplication::applicationVersion())));
    dialog.setStandardButtons(QMessageBox::Ok);

    QMovie logo(QStringLiteral(":/branding/logo_g.gif"));
    if (logo.isValid()) {
        connect(&logo, &QMovie::frameChanged, &dialog, [&] {
            dialog.setIconPixmap(logo.currentPixmap());
        });
        logo.start();
    } else {
        dialog.setIconPixmap(windowIcon().pixmap(120));
    }

    dialog.exec();
}

void MainWindow::installLanguage(const QString &code)
{
    language = code == QStringLiteral("en") || code == QStringLiteral("fr") ? code : QStringLiteral("de");
    const QLocale locale(language);
    QLocale::setDefault(locale);
    setLocale(locale);

    qApp->removeTranslator(&appTranslator);
    qApp->removeTranslator(&qtTranslator);
    if (qtTranslator.load(QStringLiteral(":/qt-i18n/qtbase_") + language)) {
        qApp->installTranslator(&qtTranslator);
    }
    if (appTranslator.load(QStringLiteral(":/i18n/tr4131_") + language)) {
        qApp->installTranslator(&appTranslator);
    }
}

void MainWindow::changeLanguage(const QString &code)
{
    if (code == language || reading) return;
    refreshTimer.stop();

    const bool refreshEnabled = autoRefresh->isChecked();
    const double interval = refreshInterval->value();
    const bool showWrite = writeCheck->isChecked();
    const bool showTrace = traceCheck->isChecked();
    const int previousTab = deviceTabs->currentIndex();
    const int previousPlotView = plotViews->currentIndex();
    const int previousSlot = slotInput->currentIndex();
    const bool showTracePeak = peakMarker->isVisible();
    const auto zoomStack = zoomer->zoomStack();
    const int zoomIndex = static_cast<int>(zoomer->zoomRectIndex());

    QList<QVariant> inputValues;
    for (auto *input : parameterInputs) {
        if (auto *choice = qobject_cast<QComboBox *>(input)) inputValues.append(choice->currentData());
        else inputValues.append(qobject_cast<QDoubleSpinBox *>(input)->value());
    }

    installLanguage(code);
    QSettings().setValue(QStringLiteral("language"), language);

    syncing = true;
    plotViews->removeTab(plotViews->indexOf(spectrogram3D));
    spectrogram3D->setParent(this);
    plotViews->removeTab(plotViews->indexOf(spectrogram));
    spectrogram->setParent(this);
    takeCentralWidget()->deleteLater();
    buildInterface();
    plotViews->setCurrentIndex(previousPlotView);
    refreshInterval->setValue(interval);
    deviceTabs->setCurrentIndex(previousTab);
    slotInput->setCurrentIndex(previousSlot);
    writeCheck->setChecked(showWrite);
    traceCheck->setChecked(showTrace);

    if (!lastSweep.write.isEmpty()) {
        showSweep(lastSweep);
        zoomer->setZoomStack(zoomStack, zoomIndex);
        if (showTracePeak) showPeak();
    }
    syncing = true;
    for (qsizetype index = 0; index < parameterInputs.size(); ++index) {
        if (auto *choice = qobject_cast<QComboBox *>(parameterInputs.at(index))) {
            const int item = choice->findData(inputValues.at(index));
            if (item >= 0) choice->setCurrentIndex(item);
        } else {
            qobject_cast<QDoubleSpinBox *>(parameterInputs.at(index))->setValue(inputValues.at(index).toDouble());
        }
    }
    autoRefresh->setChecked(refreshEnabled);
    syncing = false;
    showConnectionStatus();
    updateControls();
}

MainWindow::~MainWindow()
{
    refreshTimer.stop();
    reader.waitForFinished();
    qApp->removeTranslator(&appTranslator);
    qApp->removeTranslator(&qtTranslator);
}

QWidget *MainWindow::createMeasurementPanel()
{
    auto *group = new QGroupBox(tr("Messung"));
    auto *layout = new QVBoxLayout(group);

    acquireButton = new QPushButton(tr("Messung abrufen"));
    acquireButton->setObjectName(QStringLiteral("primary"));
    acquireButton->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    auto *remote = deviceButton(tr("Remote"), {}, DeviceOperation::Remote);
    auto *local = deviceButton(tr("Local"), {}, DeviceOperation::Local);
    busButtons = {remote, local};
    auto *actions = new QHBoxLayout;
    actions->addWidget(acquireButton, 1);
    actions->addWidget(remote);
    actions->addWidget(local);
    layout->addLayout(actions);

    auto *refreshOptions = new QHBoxLayout;
    autoRefresh = new QCheckBox(tr("Automatisch abrufen"));
    autoRefresh->setObjectName(QStringLiteral("autoRefresh"));
    refreshOptions->addWidget(autoRefresh, 1);
    refreshInterval = new QDoubleSpinBox;
    refreshInterval->setObjectName(QStringLiteral("refreshInterval"));
    refreshInterval->setAccessibleName(tr("Abrufintervall"));
    refreshInterval->setDecimals(1);
    refreshInterval->setRange(0.5, 60.0);
    refreshInterval->setSingleStep(0.5);
    refreshInterval->setKeyboardTracking(false);
    refreshInterval->setSuffix(QStringLiteral(" s"));
    refreshInterval->setValue(QSettings().value(QStringLiteral("refresh/interval"), 2.0).toDouble());
    refreshOptions->addWidget(refreshInterval);
    layout->addLayout(refreshOptions);
    return group;
}

QWidget *MainWindow::createSettingsPanel()
{
    auto *group = new QGroupBox(tr("Gerätebedienung"));
    auto *layout = new QVBoxLayout(group);
    deviceTabs = new QTabWidget;
    auto *tabs = deviceTabs;
    tabs->setObjectName(QStringLiteral("deviceTabs"));
    createFrequencyTab(tabs);
    createLevelTab(tabs);
    createSweepTab(tabs);
    createMarkerTab(tabs);
    createMemoryTab(tabs);
    applyButton = new QPushButton(tr("Übernehmen"));
    applyButton->setObjectName(QStringLiteral("applySettings"));
    discardButton = new QPushButton(tr("Verwerfen"));
    layout->addWidget(buttonPair(applyButton, discardButton));
    layout->addWidget(tabs);

    detailsLabel = quietLabel(tr("Messung abrufen, um die aktuellen Gerätewerte zu laden."));
    layout->addWidget(detailsLabel);

    const QList<QComboBox *> options{
        spanInput, rbwInput, unitInput, scaleInput, attenuationInput, sweepTimeInput,
        videoInput, triggerInput
    };
    for (auto *box : options) {
        parameterInputs.append(box);
        connect(box, &QComboBox::currentIndexChanged, this, &MainWindow::updateControls);
    }
    for (auto *input : {centerInput, referenceInput}) {
        parameterInputs.append(input);
        connect(input, &QDoubleSpinBox::valueChanged, this, &MainWindow::updateControls);
    }
    return group;
}

QFormLayout *MainWindow::addControlTab(QTabWidget *tabs, const QString &title)
{
    auto *page = new QWidget;
    auto *layout = new QFormLayout(page);
    layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->setFormAlignment(Qt::AlignTop);
    layout->setContentsMargins(10, 10, 10, 10);
    tabs->addTab(page, title);
    return layout;
}

void MainWindow::createFrequencyTab(QTabWidget *tabs)
{
    auto *layout = addControlTab(tabs, tr("Frequenz"));

    centerInput = new QDoubleSpinBox;
    centerInput->setDecimals(6);
    centerInput->setRange(0, 3500);
    centerInput->setSuffix(QStringLiteral(" MHz"));
    layout->addRow(tr("Mittenfrequenz"), centerInput);
    spanInput = new QComboBox;
    fillOptions(spanInput, spans, frequencyChoice);
    layout->addRow(tr("Span"), spanInput);
    rbwInput = new QComboBox;
    rbwInput->setObjectName(QStringLiteral("rbwInput"));
    fillOptions(rbwInput, bandwidths, frequencyChoice);
    layout->addRow(QStringLiteral("RBW"), rbwInput);

    fullSpanButton = new QPushButton(tr("Full Span"));
    fullSpanButton->setToolTip(tr("4 GHz Span, 2 GHz Mittenfrequenz und 1 MHz RBW direkt am Gerät einstellen."));
    connect(fullSpanButton, &QPushButton::clicked, this, [this] {
        applyFrequency(2000.0, 4000.0, 1.0);
    });

    zoomFrequencyButton = new QPushButton(tr("Zoom → Frequenz"));
    zoomFrequencyButton->setToolTip(tr("Mittenfrequenz und Span des Zoomausschnitts direkt am Gerät einstellen."));
    connect(zoomFrequencyButton, &QPushButton::clicked, this, &MainWindow::applyZoomFrequency);
    layout->addRow(buttonPair(fullSpanButton, zoomFrequencyButton));

    layout->addRow(deviceButton(tr("RBW-Automatik"), {"BA"}));

    layout->addRow(tr("Feinabstimmung"), buttonPair(
        deviceButton(QStringLiteral("−"), {"CF", "FD"}), deviceButton(QStringLiteral("+"), {"CF", "FU"})));
    layout->addRow(buttonPair(deviceButton(QStringLiteral("CF CAL"), {"FL"}),
                              deviceButton(QStringLiteral("ZERO CAL"), {"SHFL"})));
}

void MainWindow::createLevelTab(QTabWidget *tabs)
{
    auto *layout = addControlTab(tabs, tr("Pegel"));

    scaleInput = new QComboBox;
    addChoice(scaleInput, tr("10 dB/Div"), 0, "L1");
    addChoice(scaleInput, tr("2 dB/Div"), 1, "L2");
    addChoice(scaleInput, tr("Quasi-Peak · 5 dB/Div"), 2, "L3");
    addChoice(scaleInput, tr("Linear"), 3, "LN");
    layout->addRow(tr("Skala"), scaleInput);

    unitInput = new QComboBox;
    addChoice(unitInput, QStringLiteral("dBm"), 0, "DM");
    addChoice(unitInput, QStringLiteral("dBµV"), 1, "DU");
    addChoice(unitInput, QStringLiteral("dBµV/m (A)"), 2, "D1");
    addChoice(unitInput, QStringLiteral("dBµV/m (B)"), 3, "D2");
    addChoice(unitInput, QStringLiteral("dBµV/m (C)"), 4, "D3");
    layout->addRow(tr("Einheit"), unitInput);

    referenceInput = new QDoubleSpinBox;
    referenceInput->setObjectName(QStringLiteral("referenceInput"));
    referenceInput->setDecimals(2);
    referenceInput->setRange(-150, 250);
    referenceInput->setSingleStep(0.25);
    layout->addRow(tr("Referenzpegel"), referenceInput);
    layout->addRow(tr("Pegelschritt"), buttonPair(
        deviceButton(QStringLiteral("−"), {"LD"}), deviceButton(QStringLiteral("+"), {"LU"})));

    attenuationInput = new QComboBox;
    for (int db = 0; db <= 50; db += 10) {
        addChoice(attenuationInput, tr("%1 dB").arg(db), db, "A" + QByteArray::number(db / 10));
    }
    layout->addRow(tr("Mindestdämpfung"), attenuationInput);
}

void MainWindow::createSweepTab(QTabWidget *tabs)
{
    auto *layout = addControlTab(tabs, tr("Sweep"));
    triggerInput = new QComboBox;
    addChoice(triggerInput, tr("Free Run"), 0, "FR");
    addChoice(triggerInput, tr("Line"), 1, "LI");
    addChoice(triggerInput, tr("Video"), 2, "VT");
    addChoice(triggerInput, tr("Single"), 3, "SI");
    layout->addRow(tr("Trigger"), triggerInput);

    sweepTimeInput = new QComboBox;
    fillOptions(sweepTimeInput, sweepTimes, sweepTime);
    layout->addRow(tr("Sweep-Zeit"), sweepTimeInput);
    videoInput = new QComboBox;
    fillOptions(videoInput, videoFilters, frequencyChoice);
    layout->addRow(tr("Videofilter"), videoInput);
    singleSweepButton = deviceButton(tr("Single-Sweep starten / zurücksetzen"),
                                     {"SR"}, DeviceOperation::CommandOnly);
    layout->addRow(singleSweepButton);
    layout->addRow(buttonPair(deviceButton(tr("Sample"), {"SHTR"}),
                              deviceButton(tr("Positiv-Peak"), {"SHTD"})));
    layout->addRow(deviceButton(tr("Normal-Detektor (Geräteoption)"), {"SHVD"}));
}

void MainWindow::createMarkerTab(QTabWidget *tabs)
{
    auto *layout = addControlTab(tabs, tr("Marker"));
    const auto withMarker = DeviceOperation::AcquireMarker;
    layout->addRow(buttonPair(deviceButton(tr("Marker ein"), {"M1"}, withMarker),
                              deviceButton(tr("Marker aus"), {"M0"})));
    layout->addRow(buttonPair(deviceButton(tr("Geräte-Peak"), {"M4"}, withMarker),
                              deviceButton(tr("Marker → CF"), {"M3"}, withMarker)));
    layout->addRow(deviceButton(tr("Peak suchen und zentrieren"), {"M4", "M3"}, withMarker));
    layout->addRow(tr("Markerschritt"), buttonPair(
        deviceButton(QStringLiteral("−"), {"M1", "FD"}, withMarker),
        deviceButton(QStringLiteral("+"), {"M1", "FU"}, withMarker)));
    layout->addRow(buttonPair(deviceButton(tr("Signal-Track"), {"SHM4"}, withMarker),
                              deviceButton(tr("Noise/Hz"), {"SHBA"}, DeviceOperation::CommandOnly)));
    layout->addRow(buttonPair(deviceButton(tr("Normieren"), {"SHRB"}),
                              deviceButton(tr("Pegellinie"), {"SHWD"})));
    markerLabel = quietLabel(tr("Kein Gerätemarker erfasst."));
    layout->addRow(markerLabel);
}

void MainWindow::createMemoryTab(QTabWidget *tabs)
{
    auto *layout = addControlTab(tabs, tr("Speicher"));
    layout->addRow(buttonPair(deviceButton(QStringLiteral("WRITE"), {"WR"}),
                              deviceButton(QStringLiteral("MAX HOLD"), {"MA"})));
    layout->addRow(buttonPair(deviceButton(QStringLiteral("STORE"), {"SE"}),
                              deviceButton(QStringLiteral("VIEW"), {"VW"})));

    slotInput = new QComboBox;
    slotInput->addItem(QStringLiteral("1 · WRITE"), QByteArray("WR"));
    slotInput->addItem(QStringLiteral("2 · STORE"), QByteArray("SE"));
    slotInput->addItem(QStringLiteral("3 · VIEW"), QByteArray("VW"));
    layout->addRow(tr("Gerätespeicher"), slotInput);
    auto *recall = new QPushButton(tr("Laden"));
    auto *save = new QPushButton(tr("Speichern"));
    deviceButtons.append(recall);
    deviceButtons.append(save);
    connect(recall, &QPushButton::clicked, this, [this] {
        startOperation(DeviceOperation::Acquire, {"RC " + slotInput->currentData().toByteArray()},
                       tr("Gerätespeicher laden"));
    });
    connect(save, &QPushButton::clicked, this, [this] {
        const auto slot = slotInput->currentText();
        const auto question = tr("Gerätespeicher %1 mit der aktuellen Einstellung überschreiben?").arg(slot);
        if (QMessageBox::question(this, tr("Gerätespeicher"), question) == QMessageBox::Yes) {
            startOperation(DeviceOperation::CommandOnly, {"SV " + slotInput->currentData().toByteArray()},
                           tr("In Gerätespeicher %1 speichern").arg(slot));
        }
    });
    layout->addRow(buttonPair(recall, save));
    layout->addRow(deviceButton(tr("Gerätegrundeinstellung"), {"IP"}, DeviceOperation::Acquire,
        tr("Den TR4131 auf die Gerätegrundeinstellung zurücksetzen?")));
}

QPushButton *MainWindow::deviceButton(const QString &label, const QByteArrayList &commands,
                                      DeviceOperation operation, const QString &confirmation)
{
    auto *button = new QPushButton(label);
    deviceButtons.append(button);
    connect(button, &QPushButton::clicked, this, [this, label, commands, operation, confirmation] {
        if (!confirmation.isEmpty()
            && QMessageBox::question(this, label, confirmation) != QMessageBox::Yes) {
            return;
        }
        startOperation(operation, commands, label);
    });
    return button;
}

QWidget *MainWindow::createPlotPanel()
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("plotPanel"));
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 12, 14, 10);
    layout->setSpacing(9);

    auto *toolbar = new QHBoxLayout;
    writeCheck = new QCheckBox(QStringLiteral("WRITE"));
    writeCheck->setObjectName(QStringLiteral("writeTrace"));
    writeCheck->setChecked(true);
    traceCheck = new QCheckBox(QStringLiteral("TRACE A"));
    traceCheck->setObjectName(QStringLiteral("traceA"));
    traceCheck->setChecked(true);
    toolbar->addWidget(writeCheck);
    toolbar->addWidget(traceCheck);
    toolbar->addStretch();
    peakButton = new QPushButton(tr("Kurven-Peak"));
    zoomButton = new QPushButton(tr("Zoom zurück"));
    saveButton = new QPushButton(tr("PNG speichern"));
    toolbar->addWidget(peakButton);
    toolbar->addWidget(zoomButton);
    toolbar->addWidget(saveButton);
    layout->addLayout(toolbar);

    plot = new QwtPlot;
    plot->setObjectName(QStringLiteral("spectrum"));
    plot->setAutoFillBackground(true);
    if (auto *canvas = qobject_cast<QwtPlotCanvas *>(plot->canvas())) {
        canvas->setFrameStyle(QFrame::NoFrame);
    }
    plot->setAxisTitle(QwtAxis::XBottom, tr("Frequenz [MHz]"));
    plot->setAxisTitle(QwtAxis::YLeft, tr("Pegel [dBm]"));
    plot->setAxisScale(QwtAxis::XBottom, 0, 4000, 400);
    plot->setAxisScale(QwtAxis::YLeft, -80, 0, 10);
    plot->setAxisMaxMajor(QwtAxis::XBottom, 10);
    plot->setAxisMaxMajor(QwtAxis::YLeft, 10);

    plotGrid = new QwtPlotGrid;
    plotGrid->attach(plot);

    writeCurve = new QwtPlotCurve(QStringLiteral("WRITE"));
    writeCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
    writeCurve->attach(plot);
    traceCurve = new QwtPlotCurve(QStringLiteral("TRACE A"));
    traceCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
    traceCurve->attach(plot);

    peakMarker = new QwtPlotMarker;
    peakMarker->setLineStyle(QwtPlotMarker::Cross);
    peakMarker->setVisible(false);
    peakMarker->attach(plot);
    instrumentMarker = new QwtPlotMarker;
    instrumentMarker->setLineStyle(QwtPlotMarker::VLine);
    instrumentMarker->setVisible(false);
    instrumentMarker->attach(plot);

    measurementCursor = new QwtPlotMarker;
    measurementCursor->setLineStyle(QwtPlotMarker::Cross);
    measurementCursor->setVisible(false);
    measurementCursor->attach(plot);

    zoomer = new SpectrumZoomer(plot->canvas());
    zoomer->setTrackerMode(QwtPicker::AlwaysOff);
    plot->canvas()->setMouseTracking(true);
    plot->canvas()->installEventFilter(this);
    plotPress.reset();

    plotNote = quietLabel(tr("Messung abrufen, um die WRITE-Kurve anzuzeigen."));
    cursorReadout = new QLabel;
    cursorReadout->setObjectName(QStringLiteral("cursorReadout"));
    cursorReadout->hide();
    auto *footer = new QHBoxLayout;
    footer->addWidget(plotNote, 1);
    footer->addWidget(cursorReadout);
    layout->addWidget(plot, 1);
    layout->addLayout(footer);
    connect(writeCheck, &QCheckBox::toggled, this, [this] { updateTraces(); updateControls(); });
    connect(traceCheck, &QCheckBox::toggled, this, &MainWindow::updateTraces);
    connect(peakButton, &QPushButton::clicked, this, &MainWindow::showPeak);
    connect(zoomButton, &QPushButton::clicked, this, [this] { zoomer->zoom(0); });
    connect(zoomer, &QwtPlotZoomer::zoomed, this, &MainWindow::updateControls);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::savePlot);
    return frame;
}

void MainWindow::setDarkTheme(bool dark)
{
    darkTheme = dark;
    QPalette palette;
    const auto setColor = [&](QPalette::ColorRole role, const char *darkColor, const char *lightColor) {
        palette.setColor(role, QColor(dark ? darkColor : lightColor));
    };
    setColor(QPalette::Window, "#10151e", "#edf3f8");
    setColor(QPalette::WindowText, "#e8f0fb", "#1c3044");
    setColor(QPalette::Base, "#171f2b", "#ffffff");
    setColor(QPalette::AlternateBase, "#192331", "#e7eef5");
    setColor(QPalette::Text, "#e8f0fb", "#1c3044");
    setColor(QPalette::Button, "#222e3e", "#e7eef5");
    setColor(QPalette::ButtonText, "#e8f0fb", "#1c3044");
    setColor(QPalette::Highlight, "#33536d", "#cbe3f7");
    setColor(QPalette::HighlightedText, "#e8f0fb", "#17334e");
    for (const auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText}) {
        palette.setColor(QPalette::Disabled, role, QColor(dark ? "#8896a9" : "#586d80"));
    }
    qApp->setPalette(palette);

    QFile theme(QStringLiteral(":/theme.qss"));
    QString style;
    if (theme.open(QIODevice::ReadOnly)) {
        style = QString::fromUtf8(theme.readAll());
    }
    if (!dark) {
        QFile light(QStringLiteral(":/theme-light.qss"));
        if (light.open(QIODevice::ReadOnly)) {
            style += QString::fromUtf8(light.readAll());
        }
    }
    qApp->setStyleSheet(style);

    if (languageMenu) {
        languageMenu->setIcon(globeIcon(dark));
    }
    if (plot) {
        updatePlotAppearance();
    }
}

void MainWindow::updatePlotAppearance()
{
    const auto color = [this](const char *dark, const char *light) {
        return QColor(darkTheme ? dark : light);
    };
    QPalette palette = qApp->palette();
    palette.setColor(QPalette::Window, color("#171f2b", "#f9fbfd"));
    palette.setColor(QPalette::WindowText, color("#d4e3f5", "#253f56"));
    palette.setColor(QPalette::Text, color("#d4e3f5", "#253f56"));
    plot->setPalette(palette);
    for (const auto axis : {QwtAxis::XBottom, QwtAxis::YLeft}) {
        plot->axisWidget(axis)->setPalette(palette);
    }
    plot->setCanvasBackground(color("#101925", "#ffffff"));
    plotGrid->setMajorPen(color("#32465a", "#b0c2d0"), 1, Qt::DotLine);
    writeCurve->setPen(color("#62c7ff", "#006ea8"), 1.5);
    traceCurve->setPen(color("#ffc478", "#a55113"), 1.3);
    peakMarker->setLinePen(color("#c2d9f0", "#496177"), 1, Qt::DashLine);
    instrumentMarker->setLinePen(color("#ffc478", "#a55113"), 1, Qt::DashLine);
    measurementCursor->setLinePen(color("#75e1ac", "#087048"), 1, Qt::DotLine);
    zoomer->setRubberBandPen(QPen(color("#a5ddff", "#185c89")));
    plot->replot();
    spectrogram->setAppearance(palette, color("#101925", "#ffffff"));
    spectrogram3D->setAppearance(palette, color("#101925", "#ffffff"));
}

void MainWindow::editConnection()
{
    refreshTimer.stop();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Verbindung"));
    dialog.setMinimumWidth(390);
    auto *form = new QFormLayout(&dialog);
    form->setContentsMargins(18, 16, 18, 16);
    form->setSpacing(10);

    auto *adapter = new QComboBox;
    adapter->setObjectName(QStringLiteral("adapter"));
    adapter->addItems({QStringLiteral("GPIB · NI-488.2"), tr("Elektor USB-GPIB (COM)")});
    adapter->setCurrentIndex(useElektor ? 1 : 0);
    form->addRow(tr("Schnittstelle"), adapter);

    auto *board = new QSpinBox;
    board->setObjectName(QStringLiteral("gpibBoard"));
    board->setRange(0, 255);
    board->setValue(gpibBoard);

    auto *port = new QComboBox;
    port->setObjectName(QStringLiteral("serialPort"));
    port->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    port->setMinimumContentsLength(12);
    port->setAccessibleName(tr("COM-Port"));
    auto *refresh = new QToolButton;
    refresh->setText(QStringLiteral("↻"));
    refresh->setToolTip(tr("COM-Ports aktualisieren"));
    refresh->setAccessibleName(refresh->toolTip());
    auto *serialPanel = new QWidget;
    auto *serialLayout = new QHBoxLayout(serialPanel);
    serialLayout->setContentsMargins(0, 0, 0, 0);
    serialLayout->addWidget(port, 1);
    serialLayout->addWidget(refresh);

    const auto fillPorts = [this, port](const QString &preferred) {
        port->clear();
        port->addItem(tr("COM-Port wählen"), QString());
        for (const auto &info : QSerialPortInfo::availablePorts()) {
            const auto description = info.description();
            const auto caption = description.isEmpty() ? info.portName()
                : QStringLiteral("%1 · %2").arg(info.portName(), description);
            port->addItem(caption, info.portName());
        }
        if (port->count() == 1) {
            port->setItemText(0, tr("Keine COM-Ports gefunden"));
        }
        if (!preferred.isEmpty() && port->findData(preferred) < 0) {
            port->addItem(preferred, preferred);
        }
        port->setCurrentIndex(port->findData(preferred));
    };
    fillPorts(elektorPort);
    connect(refresh, &QToolButton::clicked, &dialog, [port, fillPorts] {
        fillPorts(port->currentData().toString());
    });

    auto *details = new QStackedWidget;
    details->addWidget(board);
    details->addWidget(serialPanel);
    auto *interfaceLabel = new QLabel;
    form->addRow(interfaceLabel, details);
    const auto updateInterface = [=](int index) {
        details->setCurrentIndex(index);
        interfaceLabel->setText(index == 0 ? tr("GPIB-Bus") : tr("COM-Port"));
        interfaceLabel->setBuddy(index == 0 ? static_cast<QWidget *>(board) : port);
    };
    updateInterface(adapter->currentIndex());
    connect(adapter, &QComboBox::currentIndexChanged, &dialog, updateInterface);

    auto *address = new QSpinBox;
    address->setObjectName(QStringLiteral("gpibAddress"));
    address->setRange(0, 30);
    address->setValue(gpibAddress);
    form->addRow(tr("Geräteadresse"), address);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Übernehmen"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        const auto previous = selectedAddress();
        useElektor = adapter->currentIndex() == 1;
        gpibBoard = board->value();
        gpibAddress = address->value();
        elektorPort = port->currentData().toString();
        if (selectedAddress() != previous) {
            settingsCurrent = false;
            refreshRetryDelayMs = 0;
        }
        showConnectionStatus();
    }
    updateControls();
}

void MainWindow::savePreferences()
{
    QSettings preferences;
    preferences.setValue(QStringLiteral("gpib/board"), gpibBoard);
    preferences.setValue(QStringLiteral("gpib/address"), gpibAddress);
    preferences.setValue(QStringLiteral("gpib/elektor"), useElektor);
    preferences.setValue(QStringLiteral("gpib/serial"), elektorPort);
    preferences.setValue(QStringLiteral("appearance/dark"), darkTheme);
    refreshInterval->interpretText();
    preferences.setValue(QStringLiteral("refresh/interval"), refreshInterval->value());
    const auto depth = plotViews->currentWidget() == spectrogram3D
        ? spectrogram3D->commitDepth() : spectrogram->commitDepth();
    preferences.setValue(QStringLiteral("spectrogram/depth"), depth);
    preferences.sync();
    if (preferences.status() == QSettings::NoError) {
        setStatus(tr("Einstellungen gespeichert."));
    } else {
        setStatus(tr("Die Einstellungen konnten nicht gespeichert werden."), true);
    }
}

GpibAddress MainWindow::selectedAddress() const
{
    return useElektor ? GpibAddress{0, gpibAddress, elektorPort} : GpibAddress{gpibBoard, gpibAddress, {}};
}

bool MainWindow::connectionSelected() const
{
    return !useElektor || !elektorPort.isEmpty();
}

QString MainWindow::connectionText(const GpibAddress &target) const
{
    return target.elektorPort.isEmpty()
        ? tr("GPIB %1 · Adresse %2").arg(target.board).arg(target.primary)
        : tr("Elektor %1 · Adresse %2").arg(target.elektorPort).arg(target.primary);
}

void MainWindow::showConnectionStatus()
{
    if (!connectionSelected()) {
        setStatus(tr("COM-Port des Elektor-GPIB-Adapters auswählen."));
    } else {
        setStatus(tr("Bereit · %1").arg(connectionText(selectedAddress())));
    }
}

void MainWindow::startOperation(DeviceOperation operation, const QByteArrayList &commands,
                                const QString &label, bool automatic)
{
    if (reading || !connectionSelected() || (automatic && !acquireButton->isEnabled())) return;

    const auto target = selectedAddress();
    writeOnlyRead = automatic && operation == DeviceOperation::Acquire && commands.isEmpty()
        && (plotViews->currentWidget() == spectrogram || plotViews->currentWidget() == spectrogram3D)
        && settingsCurrent && target == measuredAddress && !lastSweep.write.isEmpty();
    const bool suppressMarker = commands.contains("M0") || commands.contains("IP");
    if (!writeOnlyRead && operation == DeviceOperation::Acquire && !suppressMarker
        && target == measuredAddress && lastSweep.marker) {
        operation = DeviceOperation::AcquireMarker;
    }
    activeOperation = operation;
    automaticRead = automatic;
    activeLabel = automatic ? tr("Automatischer Abruf") : label;
    reading = true;
    updateControls();
    setStatus(activeLabel.isEmpty() ? tr("Messung wird abgerufen …")
                                   : tr("%1 wird ausgeführt …").arg(activeLabel));
    if (writeOnlyRead) {
        reader.setFuture(QtConcurrent::run([target, sweep = lastSweep]() mutable {
            return readTr4131Write(target, std::move(sweep));
        }));
    } else {
        reader.setFuture(QtConcurrent::run([target, operation, commands] {
            return operateTr4131(target, operation, commands);
        }));
    }
}

void MainWindow::finishRead()
{
    auto result = reader.result();
    reading = false;
    if (!result.error.isEmpty()) {
        settingsCurrent = false;
        if (automaticRead && autoRefresh->isChecked()) {
            refreshRetryDelayMs = std::clamp(refreshRetryDelayMs * 2, firstRefreshRetryMs, maxRefreshRetryMs);
            setStatus(tr("%1 · Automatischer Abruf wird wiederholt.").arg(result.error), true);
        } else {
            refreshRetryDelayMs = 0;
            const QSignalBlocker block(autoRefresh);
            autoRefresh->setChecked(false);
            setStatus(result.error, true);
        }
        updateControls();
        return;
    }

    refreshRetryDelayMs = 0;
    if (activeOperation == DeviceOperation::Acquire || activeOperation == DeviceOperation::AcquireMarker) {
        const auto target = selectedAddress();
        const auto points = result.sweep.write.size();
        if (writeOnlyRead) {
            spectrogram->addSweep(result.sweep, target);
        } else {
            const bool keepZoom = automaticRead && target == measuredAddress;
            measuredAddress = target;
            settingsCurrent = true;
            showSweep(std::move(result.sweep), keepZoom);
            spectrogram->addSweep(lastSweep, target);
        }
        const auto summary = tr("%1 · %2 Punkte empfangen").arg(connectionText(target)).arg(points);
        if (!result.markerError.isEmpty()) {
            setStatus(summary + QStringLiteral(" · ") + result.markerError, true);
        } else {
            setStatus(activeLabel.isEmpty() ? summary : activeLabel + QStringLiteral(" · ") + summary);
        }
    } else {
        if (activeOperation == DeviceOperation::Local) {
            settingsCurrent = false;
        }
        setStatus(tr("%1 · GPIB-Befehl gesendet.").arg(activeLabel));
    }
    updateControls();
}

QByteArrayList MainWindow::pendingSettings() const
{
    const auto &s = lastSweep;
    QByteArrayList commands;
    appendChoice(commands, spanInput, s.spanHz / 1e6);
    if (std::abs(centerInput->value() - s.centerHz / 1e6) > 0.0000005) {
        commands.append("CF " + decimalCommand(centerInput->value(), 6) + "MZ");
    }
    appendChoice(commands, rbwInput, s.resolutionHz / 1e6);
    appendChoice(commands, scaleInput, s.scaleMode);
    const int targetScale = scaleInput->currentData().toInt();
    if (targetScale != 3) appendChoice(commands, unitInput, s.levelUnitMode);

    const auto code = s.referenceCode == "LV" ? QByteArray("V") : s.referenceCode;
    const auto tolerance = code == "V" ? 0.5e-9 : 0.005;
    if (targetScale == s.scaleMode && (targetScale == 3 || unitInput->currentData().toInt() == s.levelUnitMode)
        && (code == "DM" || code == "DU" || code == "V")
        && std::abs(referenceInput->value() - s.reference) >= tolerance) {
        commands.append("RL" + decimalCommand(referenceInput->value(), referenceInput->decimals()) + code);
    }
    appendChoice(commands, attenuationInput, s.minimumAttenuationDb);
    appendChoice(commands, videoInput, s.videoHz / 1e6);
    appendChoice(commands, sweepTimeInput, s.sweepSecondsPerDiv);
    appendChoice(commands, triggerInput, s.triggerMode);
    return commands;
}

void MainWindow::applyFrequency(double centerMHz, double spanMHz, std::optional<double> rbwMHz)
{
    syncing = true;
    centerInput->setValue(centerMHz);
    spanInput->setCurrentIndex(spanInput->findData(spanMHz));
    if (rbwMHz) rbwInput->setCurrentIndex(rbwInput->findData(*rbwMHz));
    syncing = false;
    updateControls();
    applySettings();
}

void MainWindow::applyZoomFrequency()
{
    const auto range = zoomer->zoomRect().normalized();
    const double mhzPrecision = 1e6; // The center-frequency input has six MHz decimals.
    const double middle = std::round(range.center().x() * mhzPrecision) / mhzPrecision;
    const double centerMHz = std::clamp(middle, centerInput->minimum(), centerInput->maximum());
    const double requiredSpan = 2 * std::max(centerMHz - range.left(), range.right() - centerMHz);

    // The device spans are listed from largest to smallest, starting with 4 GHz.
    const auto span = std::find_if(spans.rbegin(), spans.rend(), [requiredSpan](const auto &candidate) {
        return candidate.mhz > 0 && candidate.mhz + 1e-9 >= requiredSpan;
    });
    if (span == spans.rend()) {
        setStatus(tr("Der gezoomte Bereich lässt sich mit den verfügbaren Spans nicht abdecken."), true);
        return;
    }

    applyFrequency(centerMHz, span->mhz);
}

void MainWindow::applySettings()
{
    const auto commands = pendingSettings();
    if (commands.isEmpty()) {
        setStatus(tr("Die Vorgaben entsprechen bereits den zuletzt gelesenen Gerätewerten."));
        return;
    }
    if (commands.contains("A0") && QMessageBox::question(this, tr("Eingangsdämpfung auf 0 dB"),
            tr("Bei 0 dB ist der Eingangsmischer weniger geschützt. "
                           "Die Einstellung nur bei geeignetem Eingangspegel übernehmen. Fortfahren?")) != QMessageBox::Yes) {
        return;
    }
    startOperation(DeviceOperation::Acquire, commands, tr("Einstellungen übernehmen"));
}

void MainWindow::syncSettings()
{
    syncing = true;
    const auto &s = lastSweep;
    centerInput->setValue(s.centerHz / 1e6);
    selectValue(spanInput, s.spanHz / 1e6, frequencyChoice(s.spanHz / 1e6));
    selectValue(rbwInput, s.resolutionHz / 1e6, frequency(s.resolutionHz));
    selectValue(videoInput, s.videoHz / 1e6, frequency(s.videoHz));
    selectValue(sweepTimeInput, s.sweepSecondsPerDiv, sweepTime(s.sweepSecondsPerDiv));
    selectValue(scaleInput, s.scaleMode, tr("Geräteskala"));
    const auto currentUnit = s.scaleMode == 3 ? tr("V · Linear") : s.unit;
    selectValue(unitInput, s.scaleMode == 3 ? -1 : s.levelUnitMode, currentUnit);
    selectValue(triggerInput, s.triggerMode, tr("Gerätemodus"));
    selectValue(attenuationInput, s.minimumAttenuationDb, tr("%1 dB").arg(s.minimumAttenuationDb));

    referenceInput->setDecimals(s.scaleMode == 3 ? 9 : 2);
    referenceInput->setRange(s.scaleMode == 3 ? 1e-9 : std::min(-150.0, s.reference),
                             s.scaleMode == 3 ? std::max(10.0, s.reference) : std::max(250.0, s.reference));
    referenceInput->setSingleStep(s.scaleMode == 3 ? 0.001 : 0.25);
    referenceInput->setSuffix(QStringLiteral(" ") + s.unit);
    referenceInput->setValue(s.reference);
    syncing = false;
    updateControls();
}

void MainWindow::showSweep(Sweep sweep, bool keepZoom)
{
    keepZoom = keepZoom && !lastSweep.write.isEmpty()
        && sweep.xStart() == lastSweep.xStart() && sweep.xStop() == lastSweep.xStop()
        && sweep.yStart() == lastSweep.yStart() && sweep.reference == lastSweep.reference
        && sweep.yStep() == lastSweep.yStep() && sweep.unit == lastSweep.unit;
    if ((lastSweep.spanHz > 0) != (sweep.spanHz > 0)) cursorPosition.reset();
    lastSweep = std::move(sweep);
    const auto &s = lastSweep;
    syncSettings();

    const auto division = s.scaleMode == 3
        ? tr("Linear")
        : tr("%1 dB/Div").arg(numeric(s.division));
    detailsLabel->setText(tr("Ref. %1 %2 · %3 · ATT %4 dB\nSweep %5 · VF %6")
                              .arg(numeric(s.reference), s.unit, division, numeric(s.attenuationDb),
                                   sweepTime(s.sweepSecondsPerDiv), frequency(s.videoHz)));

    plot->setAxisTitle(QwtAxis::XBottom, s.spanHz > 0 ? tr("Frequenz [MHz]")
                                                      : tr("Zeit [ms]"));
    plot->setAxisTitle(QwtAxis::YLeft, tr("Pegel [%1]").arg(s.unit));
    const double minX = s.xStart();
    const double maxX = s.xStop();
    if (!keepZoom) {
        plot->setAxisScale(QwtAxis::XBottom, minX, maxX, (maxX - minX) / 10.0);
        plot->setAxisScale(QwtAxis::YLeft, s.yStart(), s.reference, s.yStep());
    }

    instrumentMarker->setVisible(false);
    markerLabel->setText(tr("Kein Gerätemarker erfasst."));
    if (s.marker) {
        const auto &marker = *s.marker;
        const double x = marker.position * (s.spanHz > 0 ? 1e-6 : 1000);
        const auto position = s.spanHz > 0 ? frequency(marker.position) : numeric(x) + QStringLiteral(" ms");
        markerLabel->setText(QStringLiteral("%1 · %2 %3").arg(position, numeric(marker.level), marker.unit));
        const bool sameScale = marker.unit == s.unit && marker.level >= s.yStart() && marker.level <= s.reference;
        instrumentMarker->setLineStyle(sameScale ? QwtPlotMarker::Cross : QwtPlotMarker::VLine);
        instrumentMarker->setValue(x, marker.level);
        instrumentMarker->setVisible(x >= minX && x <= maxX);
    }

    writeCurve->setSamples(plotPoints(s, s.write));
    traceCurve->setSamples(plotPoints(s, s.traceA));
    updateTraces();
    if (!keepZoom) zoomer->setZoomBase();
}

void MainWindow::updateControls()
{
    if (syncing) return;

    const bool connected = connectionSelected() && !reading;
    const bool canEdit = connected && settingsCurrent && selectedAddress() == measuredAddress;
    const bool dirty = canEdit && !pendingSettings().isEmpty();
    connectionSettingsAction->setEnabled(!reading && !dirty);
    acquireButton->setEnabled(connected && !dirty);
    autoRefresh->setEnabled(connected || autoRefresh->isChecked());
    refreshInterval->setEnabled(connectionSelected());
    languageMenu->menuAction()->setEnabled(!reading);
    for (auto *input : parameterInputs) input->setEnabled(canEdit);

    const int scale = scaleInput->currentData().toInt();
    const int unit = unitInput->currentData().toInt();
    const auto &s = lastSweep;
    unitInput->setEnabled(canEdit && scale != 3);
    referenceInput->setEnabled(canEdit && scale == s.scaleMode
        && (scale == 3 || unit == s.levelUnitMode)
        && (s.referenceCode == "DM" || s.referenceCode == "DU" || s.referenceCode == "LV"));
    applyButton->setEnabled(dirty);
    discardButton->setEnabled(dirty);
    fullSpanButton->setEnabled(canEdit);
    const bool showingSpectrogram = plotViews->currentWidget() == spectrogram
        || plotViews->currentWidget() == spectrogram3D;
    zoomFrequencyButton->setEnabled(!showingSpectrogram && canEdit && s.spanHz > 0 && zoomer->zoomRectIndex() > 0);
    for (auto *button : deviceButtons) button->setEnabled(canEdit && !dirty);
    for (auto *button : busButtons) button->setEnabled(connected && !dirty);
    slotInput->setEnabled(canEdit && !dirty);
    singleSweepButton->setEnabled(canEdit && !dirty && lastSweep.triggerMode == 3);

    const bool hasSweep = !lastSweep.write.isEmpty();
    writeCheck->setEnabled(hasSweep);
    traceCheck->setEnabled(!lastSweep.traceA.isEmpty());
    peakButton->setEnabled(hasSweep && writeCheck->isChecked());
    zoomButton->setEnabled(hasSweep);
    saveButton->setEnabled(hasSweep);
    saveAction->setEnabled(showingSpectrogram ? spectrogram->hasHistory() : hasSweep);
    csvAction->setEnabled(hasSweep);
    zoomer->setEnabled(hasSweep);
    updateRefreshTimer();
}

void MainWindow::updateRefreshTimer()
{
    if (autoRefresh->isChecked() && acquireButton->isEnabled()) {
        if (!refreshTimer.isActive()) {
            const int intervalMs = qRound(refreshInterval->value() * 1000.0);
            refreshTimer.start(std::max(intervalMs, refreshRetryDelayMs));
        }
    } else {
        refreshTimer.stop();
    }
}

void MainWindow::updateTraces()
{
    writeCurve->setVisible(writeCheck->isChecked() && !lastSweep.write.isEmpty());
    traceCurve->setVisible(traceCheck->isChecked() && !lastSweep.traceA.isEmpty());
    peakMarker->setVisible(false);
    if (!lastSweep.write.isEmpty()) {
        const auto note = lastSweep.traceA.isEmpty() ? tr("TRACE A ist leer.  ") : QString();
        plotNote->setText(note + tr("Klicken für Messcursor. Ziehen zum Zoomen."));
    }
    updateMeasurementCursor();
    plot->replot();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (plot && watched == plot->canvas()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick: {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton && mouse->modifiers() == Qt::NoModifier) {
                plotPress = mouse->position();
            }
            break;
        }
        case QEvent::MouseMove: {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->buttons() & Qt::LeftButton) {
                if (plotPress && isPlotDrag(*plotPress, mouse->position())) plotPress.reset();
            } else if (cursorPosition && plot->canvas()->rect().contains(mouse->position().toPoint())) {
                moveMeasurementCursor(mouse->position());
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton) break;
            const bool click = plotPress && !isPlotDrag(*plotPress, mouse->position())
                && plot->canvas()->rect().contains(mouse->position().toPoint());
            plotPress.reset();
            if (click && !lastSweep.write.isEmpty()) {
                if (cursorPosition) {
                    cursorPosition.reset();
                    updateMeasurementCursor();
                    plot->replot();
                } else {
                    moveMeasurementCursor(mouse->position());
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::moveMeasurementCursor(const QPointF &position)
{
    cursorPosition = QPointF(plot->invTransform(QwtAxis::XBottom, position.x()),
                             plot->invTransform(QwtAxis::YLeft, position.y()));
    updateMeasurementCursor();
    plot->replot();
}

void MainWindow::updateMeasurementCursor()
{
    const QwtPlotCurve *selected = nullptr;
    QPointF sample;
    const double start = lastSweep.xStart();
    const double width = lastSweep.xStop() - start;
    if (cursorPosition && width > 0) {
        const double fraction = std::clamp((cursorPosition->x() - start) / width, 0.0, 1.0);
        for (const auto *curve : {writeCurve, traceCurve}) {
            if (!curve->isVisible() || curve->dataSize() == 0) continue;
            const auto index = qRound(fraction * (curve->dataSize() - 1));
            const auto candidate = curve->sample(index);
            if (!selected || std::abs(candidate.y() - cursorPosition->y())
                    < std::abs(sample.y() - cursorPosition->y())) {
                selected = curve;
                sample = candidate;
            }
        }
    }

    measurementCursor->setVisible(selected != nullptr);
    cursorReadout->setVisible(selected != nullptr);
    if (!selected) return;

    const auto position = lastSweep.spanHz > 0 ? frequency(sample.x() * 1e6)
                                                : numeric(sample.x()) + QStringLiteral(" ms");
    measurementCursor->setValue(sample.x(), sample.y());
    cursorReadout->setText(tr("Cursor · %1 · %2 · %3 %4")
                              .arg(selected->title().text(), position, numeric(sample.y()), lastSweep.unit));
}

void MainWindow::showPeak()
{
    const auto &s = lastSweep;
    const auto peak = std::max_element(s.write.cbegin(), s.write.cend());
    const double fraction = double(peak - s.write.cbegin()) / double(s.write.size() - 1);
    const double x = s.xStart() + fraction * (s.xStop() - s.xStart());
    peakMarker->setValue(x, *peak);
    peakMarker->setVisible(true);
    plot->replot();
    const auto position = s.spanHz > 0 ? frequency(x * 1e6) : numeric(x) + QStringLiteral(" ms");
    plotNote->setText(tr("WRITE-Peak  %1  ·  %2 %3").arg(position, numeric(*peak), s.unit));
}

void MainWindow::savePlot()
{
    const bool showingSpectrogram = plotViews->currentWidget() == spectrogram
        || plotViews->currentWidget() == spectrogram3D;
    const bool showing3D = plotViews->currentWidget() == spectrogram3D;
    const auto title = showing3D ? tr("3D-Spektrogramm speichern")
        : showingSpectrogram ? tr("Spektrogramm speichern") : tr("Spektrum speichern");
    const auto name = QFileDialog::getSaveFileName(this, title,
        defaultExportName(QStringLiteral("png")), tr("PNG-Bild (*.png)"));
    if (!name.isEmpty()) {
        const auto screenshot = showing3D ? spectrogram3D->grabPlot()
            : showingSpectrogram ? spectrogram->grabPlot() : plot->grab();
        if (!screenshot.save(name, "PNG")) {
            QMessageBox::warning(this, title, tr("Das PNG-Bild konnte nicht gespeichert werden."));
        }
    }
}

void MainWindow::saveCsv()
{
    // Keep the same sweep even if an automatic refresh finishes inside the save dialog.
    const auto sweep = lastSweep;
    if (sweep.write.isEmpty()) return;

    const auto title = tr("Daten als CSV speichern");
    QFileDialog dialog(this, title);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(tr("CSV-Datei (*.csv)"));
    dialog.setDefaultSuffix(QStringLiteral("csv"));
    dialog.selectFile(defaultExportName(QStringLiteral("csv")));
    if (dialog.exec() != QDialog::Accepted) return;

    QSaveFile file(dialog.selectedFiles().constFirst());
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out.setGenerateByteOrderMark(true);
        QLocale csvLocale = locale();
        csvLocale.setNumberOptions(csvLocale.numberOptions() | QLocale::OmitGroupSeparator);

        const bool frequencyAxis = sweep.spanHz > 0;
        const bool hasTrace = !sweep.traceA.isEmpty();
        const double axisFactor = frequencyAxis ? 1e6 : 1e-3;
        out << (frequencyAxis ? tr("Frequenz [Hz]") : tr("Zeit [s]"))
            << QStringLiteral(";WRITE [%1]").arg(sweep.unit);
        if (hasTrace) out << QStringLiteral(";TRACE A [%1]").arg(sweep.unit);
        out << '\n';

        const auto points = plotPoints(sweep, sweep.write);
        for (qsizetype i = 0; i < points.size(); ++i) {
            const auto &point = points.at(i);
            out << csvLocale.toString(point.x() * axisFactor, 'g', 15)
                << ';' << csvLocale.toString(point.y(), 'g', 15);
            if (hasTrace) {
                out << ';';
                if (i < sweep.traceA.size()) {
                    out << csvLocale.toString(sweep.traceA.at(i), 'g', 15);
                }
            }
            out << '\n';
        }

        out.flush();
        if (out.status() == QTextStream::Ok && file.commit()) return;
        file.cancelWriting();
    }

    QMessageBox::warning(this, title, tr("Die CSV-Datei konnte nicht gespeichert werden. %1").arg(file.errorString()));
}

void MainWindow::setStatus(const QString &message, bool error)
{
    statusLabel->setText(message);
    statusLabel->setObjectName(error ? QStringLiteral("statusError") : QStringLiteral("status"));
    statusLabel->style()->unpolish(statusLabel);
    statusLabel->style()->polish(statusLabel);
}
