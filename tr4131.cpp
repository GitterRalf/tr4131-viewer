#include "tr4131.h"
#include "gpibconnection.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLibrary>
#include <QRegularExpression>
#include <QThread>

#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <windows.h>

namespace {

constexpr qsizetype sampleCount = 701;
constexpr unsigned gpibError = 0x8000;
constexpr unsigned gpibEnd = 0x2000;
constexpr int eosConfig = 0x25;
constexpr int lfReadTermination = 0x140a;
constexpr int timeout3Seconds = 12;

void fail(const QString &message)
{
    throw std::runtime_error(message.toUtf8().constData());
}

class NiGpibSession final : public GpibConnection
{
public:
    explicit NiGpibSession(GpibAddress target)
        : address(target)
    {
        wchar_t systemDirectory[MAX_PATH] = {};
        if (!GetSystemDirectoryW(systemDirectory, MAX_PATH)) {
            fail(QCoreApplication::translate("Tr4131", "Das Windows-Systemverzeichnis konnte nicht ermittelt werden."));
        }

        library.setFileName(QDir(QString::fromWCharArray(systemDirectory)).filePath(
            QStringLiteral("ni4882.dll")));
        if (!library.load()) {
            fail(QCoreApplication::translate("Tr4131", "NI-488.2 konnte nicht geladen werden. %1").arg(library.errorString()));
        }

        const auto open = resolve<int (WINAPI *)(int, int, int, int, int, int)>("ibdev");
        const auto listen = resolve<unsigned (WINAPI *)(int, int, int, short *)>("ibln");
        status = resolve<unsigned (WINAPI *)()>("ThreadIbsta");
        error = resolve<unsigned (WINAPI *)()>("ThreadIberr");
        byteCount = resolve<unsigned (WINAPI *)()>("ThreadIbcnt");
        configure = resolve<unsigned (WINAPI *)(int, int, int)>("ibconfig");
        write = resolve<unsigned (WINAPI *)(int, const void *, size_t)>("ibwrt");
        read = resolve<unsigned (WINAPI *)(int, void *, size_t)>("ibrd");
        offline = resolve<unsigned (WINAPI *)(int, int)>("ibonl");
        setLocal = resolve<unsigned (WINAPI *)(int)>("ibloc");
        enableRemote = resolve<void (WINAPI *)(int, const unsigned short *)>("EnableRemote");
        serialPoll = resolve<unsigned (WINAPI *)(int, char *)>("ibrsp");

        descriptor = open(address.board, address.primary, 0, timeout3Seconds, 1, 0);
        if (descriptor < 0) {
            const auto state = status();
            fail(QCoreApplication::translate("Tr4131", "GPIB %1, Adresse %2 konnte nicht geöffnet werden (ibsta 0x%3, iberr %4).")
                     .arg(address.board).arg(address.primary).arg(state, 0, 16).arg(error()));
        }

        short present = 0;
        try {
            check(listen(descriptor, address.primary, 0, &present), QCoreApplication::translate("Tr4131", "Gerätesuche"));
            if (!present) {
                fail(QCoreApplication::translate("Tr4131", "Auf GPIB %1, Adresse %2 antwortet kein Gerät.")
                         .arg(address.board).arg(address.primary));
            }
        } catch (...) {
            offline(descriptor, 0);
            throw;
        }
    }

    ~NiGpibSession() override { offline(descriptor, 0); }
    NiGpibSession(const NiGpibSession &) = delete;
    NiGpibSession &operator=(const NiGpibSession &) = delete;

    void send(const QByteArray &command) override
    {
        sendCommand(command, true);
    }

    void local() override
    {
        check(setLocal(descriptor), QCoreApplication::translate("Tr4131", "Auf Local umschalten"));
    }

    void remote() override
    {
        const std::array<unsigned short, 2> devices{static_cast<unsigned short>(address.primary), 0xffff};
        enableRemote(address.board, devices.data());
        check(status(), QCoreApplication::translate("Tr4131", "Auf Remote umschalten"));
    }

    QByteArray query(const QByteArray &command, qsizetype exactBinaryBytes) override
    {
        return queryReply(command, exactBinaryBytes, true);
    }

    QByteArray readWriteTrace()
    {
        return queryReply("OPTBW", sampleCount * 2, false);
    }

    unsigned statusByte() override
    {
        char reply = 0;
        check(serialPoll(descriptor, &reply), QCoreApplication::translate("Tr4131", "Gerätestatus lesen"));
        return static_cast<unsigned char>(reply);
    }

private:
    void sendCommand(const QByteArray &command, bool paced)
    {
        const QByteArray payload = command + "\r\n";
        check(write(descriptor, payload.constData(), static_cast<size_t>(payload.size())),
              QString::fromLatin1(command) + QCoreApplication::translate("Tr4131", " senden"));
        if (byteCount() != payload.size()) {
            fail(QCoreApplication::translate("Tr4131", "%1 wurde nur teilweise geschrieben.")
                     .arg(QString::fromLatin1(command)));
        }
        if (paced) {
            QThread::msleep(30);
        }
    }

    QByteArray queryReply(const QByteArray &command, qsizetype exactBinaryBytes, bool paced)
    {
        check(configure(descriptor, eosConfig, exactBinaryBytes ? 0 : lfReadTermination),
              QCoreApplication::translate("Tr4131", "GPIB-Abschluss konfigurieren"));
        sendCommand(command, paced);
        QByteArray buffer(2048, '\0');
        const auto state = read(descriptor, buffer.data(), static_cast<size_t>(buffer.size()));
        const auto length = static_cast<qsizetype>(byteCount());
        check(state, QString::fromLatin1(command) + QCoreApplication::translate("Tr4131", " lesen"));
        if (!(state & gpibEnd) || length <= 0 || length > buffer.size()
            || (exactBinaryBytes && length != exactBinaryBytes)) {
            fail(QCoreApplication::translate("Tr4131", "Unvollständige Antwort auf %1 (%2 Bytes).")
                     .arg(QString::fromLatin1(command)).arg(length));
        }
        buffer.resize(length);
        return buffer;
    }

    template<typename Function>
    Function resolve(const char *name)
    {
        const auto symbol = library.resolve(name);
        if (!symbol) {
            fail(QCoreApplication::translate("Tr4131", "NI-488.2 stellt %1 nicht bereit.").arg(QString::fromLatin1(name)));
        }
        return reinterpret_cast<Function>(symbol);
    }

    void check(unsigned state, const QString &operation) const
    {
        if (state & gpibError) {
            fail(QCoreApplication::translate("Tr4131", "%1 fehlgeschlagen (ibsta 0x%2, iberr %3, %4 Bytes).")
                     .arg(operation).arg(state, 0, 16).arg(error()).arg(byteCount()));
        }
    }

    const GpibAddress address;
    QLibrary library;
    int descriptor = -1;
    unsigned (WINAPI *status)() = nullptr;
    unsigned (WINAPI *error)() = nullptr;
    unsigned (WINAPI *byteCount)() = nullptr;
    unsigned (WINAPI *configure)(int, int, int) = nullptr;
    unsigned (WINAPI *write)(int, const void *, size_t) = nullptr;
    unsigned (WINAPI *read)(int, void *, size_t) = nullptr;
    unsigned (WINAPI *offline)(int, int) = nullptr;
    unsigned (WINAPI *setLocal)(int) = nullptr;
    void (WINAPI *enableRemote)(int, const unsigned short *) = nullptr;
    unsigned (WINAPI *serialPoll)(int, char *) = nullptr;
};

void runCommand(GpibConnection &instrument, const QByteArray &command)
{
    // Completion flags from the TR4131 status byte.
    unsigned mask = 0;
    int timeout = 0;
    if (command == "M4") { mask = 0x04; timeout = 7000; }
    if (command == "M3") { mask = 0x02; timeout = 12000; }
    if (command == "FL") { mask = 0x10; timeout = 7000; }
    if (command == "SHFL") { mask = 0x01; timeout = 12000; }

    if (mask) instrument.statusByte();
    instrument.send(command);
    if (!mask) return;

    QElapsedTimer timer;
    timer.start();
    do {
        if (instrument.statusByte() & mask) return;
        QThread::msleep(70);
    } while (!timer.hasExpired(timeout));
    fail(QCoreApplication::translate("Tr4131", "Der TR4131 hat den Abschluss von %1 nicht gemeldet.")
             .arg(QString::fromLatin1(command)));
}

double number(const QByteArray &bytes, const QString &label)
{
    static const QRegularExpression expression(QStringLiteral(
        R"(^(?:[A-Z][A-Z0-9])?\s*([+-]?\s*\d+(?:[.,]\d+)?(?:[Ee][+-]?\d+)?)\s*$)"));
    const auto match = expression.match(QString::fromLatin1(bytes));
    bool ok = false;
    const auto value = match.captured(1).remove(u' ').replace(u',', u'.').toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        fail(QCoreApplication::translate("Tr4131", "Ungültiger Messwert für %1 (%2).")
                 .arg(label, QString::fromLatin1(bytes.trimmed())));
    }
    return value;
}

QString levelUnit(const QByteArray &reply)
{
    const auto prefix = reply.left(2);
    if (prefix == "DM" || prefix == "MM") return QStringLiteral("dBm");
    if (prefix == "DU" || prefix == "MU") return QStringLiteral("dBµV");
    if (prefix == "VM" || prefix == "VN" || prefix == "ME") return QStringLiteral("dBµV/m");
    if (prefix == "DQ" || prefix == "MQ") return QStringLiteral("dBmV");
    if (prefix == "LV" || prefix == "ML") return QStringLiteral("V");
    fail(QCoreApplication::translate("Tr4131", "Unbekannte Pegeleinheit (%1).").arg(QString::fromLatin1(reply.trimmed())));
    return {};
}

QVector<double> decodeTrace(const QByteArray &data, const Sweep &sweep, bool blankIsAbsent)
{
    QVector<double> values;
    values.reserve(sampleCount);
    bool hasData = false;
    const double bottom = sweep.yStart();
    const double step = (sweep.reference - bottom) / (470.0 - 70.0);
    for (qsizetype index = 0; index < data.size(); index += 2) {
        const auto high = static_cast<unsigned char>(data[index]);
        const auto low = static_cast<unsigned char>(data[index + 1]);
        const auto raw = (high << 8) | low;
        if (raw > 511) {
            fail(QCoreApplication::translate("Tr4131", "Kurve enthält einen ungültigen 9-Bit-Messwert (Punkt %1).")
                     .arg(index / 2 + 1));
        }
        hasData |= raw != 0;
        values.append(bottom + (raw - 70.0) * step);
    }
    if (blankIsAbsent && !hasData) {
        values.clear();
    }
    return values;
}

} // namespace

double Sweep::xStart() const
{
    return spanHz > 0 ? (centerHz - spanHz / 2.0) / 1e6 : 0;
}

double Sweep::xStop() const
{
    return spanHz > 0 ? (centerHz + spanHz / 2.0) / 1e6 : sweepSecondsPerDiv * 10000;
}

double Sweep::yStart() const
{
    return scaleMode == 3 ? 0 : reference - division * verticalDivisions;
}

double Sweep::yStep() const
{
    return (reference - yStart()) / verticalDivisions;
}

SweepResult operateTr4131(GpibAddress address, DeviceOperation operation,
                          const QByteArrayList &commands)
{
    try {
        std::unique_ptr<GpibConnection> connection;
        if (address.elektorPort.isEmpty()) {
            connection = std::make_unique<NiGpibSession>(address);
        } else {
            connection = openElektorGpib(address.elektorPort, address.primary);
        }
        auto &instrument = *connection;
        if (operation == DeviceOperation::Remote) {
            instrument.remote();
            return {};
        }
        if (operation == DeviceOperation::Local) {
            instrument.local();
            return {};
        }
        for (const auto &command : commands) {
            runCommand(instrument, command);
        }
        if (operation == DeviceOperation::CommandOnly) return {};
        if (!commands.isEmpty()) QThread::msleep(150);

        instrument.send("HD1");

        Sweep sweep;
        const auto modes = instrument.query("OM", 6);
        sweep.minimumAttenuationDb = static_cast<unsigned char>(modes.at(0)) * 10;
        sweep.scaleMode = static_cast<unsigned char>(modes.at(1));
        sweep.levelUnitMode = static_cast<unsigned char>(modes.at(2));
        sweep.triggerMode = static_cast<unsigned char>(modes.at(4));
        switch (sweep.scaleMode) {
        case 0: sweep.division = 10; break;
        case 1: sweep.division = 2; sweep.verticalDivisions = 10; break;
        case 2: sweep.division = 5; break;
        case 3: sweep.division = 1; sweep.verticalDivisions = 10; break;
        default: fail(QCoreApplication::translate("Tr4131", "Der TR4131 meldet eine unbekannte Pegelskala."));
        }
        sweep.centerHz = number(instrument.query("OPCF"), QCoreApplication::translate("Tr4131", "Mittenfrequenz"));
        sweep.spanHz = number(instrument.query("OPSP"), QCoreApplication::translate("Tr4131", "Span"));
        const auto level = instrument.query("OPRL");
        sweep.reference = number(level, QCoreApplication::translate("Tr4131", "Referenzpegel"));
        sweep.unit = levelUnit(level);
        sweep.referenceCode = level.left(2);
        if (sweep.scaleMode == 3 && sweep.reference <= 0) {
            fail(QCoreApplication::translate("Tr4131", "Das Gerät meldet einen ungültigen linearen Referenzpegel."));
        }
        sweep.resolutionHz = number(instrument.query("OPRB"), QCoreApplication::translate("Tr4131", "RBW"));
        sweep.videoHz = number(instrument.query("OPVF"), QCoreApplication::translate("Tr4131", "Videofilter"));
        sweep.sweepSecondsPerDiv = number(instrument.query("OPST"), QCoreApplication::translate("Tr4131", "Sweep-Zeit"));
        sweep.attenuationDb = number(instrument.query("OPAT"), QCoreApplication::translate("Tr4131", "Dämpfung"));
        if (sweep.spanHz < 0 || (sweep.spanHz == 0 && sweep.sweepSecondsPerDiv <= 0)) {
            fail(QCoreApplication::translate("Tr4131", "Der TR4131 meldet einen ungültigen Frequenz- oder Zeitbereich."));
        }
        sweep.write = decodeTrace(instrument.query("OPTBW", sampleCount * 2), sweep, false);
        sweep.traceA = decodeTrace(instrument.query("OPTBA", sampleCount * 2), sweep, true);
        QString markerError;
        if (operation == DeviceOperation::AcquireMarker) {
            try {
                const double position = number(instrument.query("OPMF"), QCoreApplication::translate("Tr4131", "Markerposition"));
                const auto levelReply = instrument.query("OPML");
                const double markerLevel = number(levelReply, QCoreApplication::translate("Tr4131", "Markerpegel"));
                if (std::abs(markerLevel) < 1000) {
                    sweep.marker = DeviceMarker{position, markerLevel, levelUnit(levelReply)};
                } else {
                    markerError = QCoreApplication::translate("Tr4131", "Das Gerät meldet keinen aktiven Marker.");
                }
            } catch (const std::exception &exception) {
                markerError = QString::fromUtf8(exception.what());
            }
        }
        return {std::move(sweep), {}, markerError};
    } catch (const std::exception &exception) {
        return {{}, QString::fromUtf8(exception.what()), {}};
    }
}

SweepResult readTr4131Write(GpibAddress address, Sweep sweep)
{
    try {
        QByteArray trace;
        if (address.elektorPort.isEmpty()) {
            NiGpibSession instrument(address);
            trace = instrument.readWriteTrace();
        } else {
            auto instrument = openElektorGpib(address.elektorPort, address.primary);
            trace = instrument->query("OPTBW", sampleCount * 2);
        }
        sweep.write = decodeTrace(trace, sweep, false);
        sweep.traceA.clear();
        sweep.marker.reset();
        return {std::move(sweep), {}, {}};
    } catch (const std::exception &exception) {
        return {{}, QString::fromUtf8(exception.what()), {}};
    }
}
