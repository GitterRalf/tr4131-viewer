#include "gpibconnection.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSerialPort>

#include <stdexcept>

namespace {

constexpr int responseTimeoutMs = 2500;
constexpr int frameGapMs = 60;
constexpr qsizetype maximumReplySize = 2048;

[[noreturn]] void fail(const QString &message)
{
    throw std::runtime_error(message.toUtf8().constData());
}

class ElektorGpib final : public GpibConnection
{
public:
    ElektorGpib(const QString &portName, int address)
        : primaryAddress(address), addressText(QByteArray::number(address))
    {
        serial.setPortName(portName);
        if (!serial.open(QIODevice::ReadWrite)
            || !serial.setBaudRate(38400)
            || !serial.setDataBits(QSerialPort::Data8)
            || !serial.setParity(QSerialPort::NoParity)
            || !serial.setStopBits(QSerialPort::OneStop)
            || !serial.setFlowControl(QSerialPort::NoFlowControl)) {
            fail(QCoreApplication::translate("Elektor", "Elektor an %1 kann nicht geöffnet werden. %2").arg(portName, serial.errorString()));
        }
        serial.clear(QSerialPort::Input);
        writeFrame("I");
        const auto identity = readReply().simplified().toUpper();
        if (!identity.contains("GPIB/USB CONVERTER") && !identity.contains("GPIB-TO-USB CONVERTER")
            && !identity.contains("GPIB TO USB CONVERTER")) {
            fail(QCoreApplication::translate("Elektor", "An %1 wurde kein Elektor-GPIB-Adapter erkannt.").arg(portName));
        }

        // The original adapter uses 100,000 timeout ticks per second.
        adapterCommand("T100000");
    }

    void send(const QByteArray &command) override
    {
        adapterCommand("W" + addressText + ',' + command);
    }

    QByteArray query(const QByteArray &command, qsizetype binaryBytes) override
    {
        writeFrame("R" + addressText + ',' + command);
        auto reply = readReply(binaryBytes);
        rejectAdapterError(reply);
        if (reply.isEmpty()) {
            fail(QCoreApplication::translate("Elektor", "Keine Antwort über Elektor auf %1.").arg(QString::fromLatin1(command)));
        }
        if (binaryBytes && reply.size() != binaryBytes) {
            fail(QCoreApplication::translate("Elektor", "Unvollständige Elektor-Antwort auf %1 (%2 von %3 Bytes).")
                     .arg(QString::fromLatin1(command)).arg(reply.size()).arg(binaryBytes));
        }
        return reply;
    }

    unsigned statusByte() override
    {
        // The firmware's S command reports the SRQ wire. OS reads the analyzer's status byte.
        return static_cast<unsigned char>(query("OS", 1).at(0));
    }

    void local() override { addressedCommand(0x01); }
    void remote() override { addressedCommand(0x11); }

private:
    void writeFrame(const QByteArray &command)
    {
        const auto frame = command + "\r\n";
        if (serial.write(frame) != frame.size()) {
            fail(QCoreApplication::translate("Elektor", "Elektor-Schreibfehler: %1").arg(serial.errorString()));
        }
        QElapsedTimer timer;
        timer.start();
        while (serial.bytesToWrite() > 0) {
            const auto remaining = responseTimeoutMs - timer.elapsed();
            if (remaining <= 0 || !serial.waitForBytesWritten(static_cast<int>(remaining))) {
                fail(QCoreApplication::translate("Elektor", "Elektor-Schreibfehler: %1").arg(serial.errorString()));
            }
        }
    }

    QByteArray readReply(qsizetype binaryBytes = 0, int firstByteTimeout = responseTimeoutMs)
    {
        QByteArray reply;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < responseTimeoutMs) {
            if (serial.bytesAvailable() == 0) {
                const auto waitMs = reply.isEmpty() ? firstByteTimeout : frameGapMs;
                const auto remaining = responseTimeoutMs - timer.elapsed();
                if (remaining <= 0 || !serial.waitForReadyRead(static_cast<int>(qMin<qint64>(waitMs, remaining)))) {
                    if (serial.error() != QSerialPort::TimeoutError && serial.error() != QSerialPort::NoError) {
                        fail(QCoreApplication::translate("Elektor", "Elektor-Lesefehler: %1").arg(serial.errorString()));
                    }
                    break;
                }
            }
            reply += serial.readAll();
            if (reply.size() > maximumReplySize) {
                fail(QCoreApplication::translate("Elektor", "Elektor-Antwort ist zu lang."));
            }
        }

        // Binary responses may be followed by CRLF from the USB firmware.
        // Keep the exact instrument payload, even when its last byte is a CR or LF.
        if (binaryBytes && reply.size() > binaryBytes && reply.sliced(binaryBytes) == "\r\n") {
            reply.truncate(binaryBytes);
        }
        return reply;
    }

    void rejectAdapterError(const QByteArray &reply)
    {
        if (reply.left(5).compare("Error", Qt::CaseInsensitive) == 0) {
            fail(QCoreApplication::translate("Elektor", "Elektor meldet %1.").arg(QString::fromLatin1(reply.trimmed())));
        }
    }

    void adapterCommand(const QByteArray &command)
    {
        writeFrame(command);
        rejectAdapterError(readReply(0, 35));
    }

    void addressedCommand(int command)
    {
        adapterCommand("G" + QByteArray::number(0x20 + primaryAddress));
        try {
            adapterCommand("G" + QByteArray::number(command));
        } catch (...) {
            try { writeFrame("G63"); } catch (...) {}
            throw;
        }
        adapterCommand("G63");
    }

    const int primaryAddress;
    const QByteArray addressText;
    QSerialPort serial;
};

} // namespace

std::unique_ptr<GpibConnection> openElektorGpib(const QString &portName, int address)
{
    return std::make_unique<ElektorGpib>(portName, address);
}
