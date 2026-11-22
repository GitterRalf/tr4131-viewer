#pragma once

#include <QByteArrayList>
#include <QString>
#include <QVector>

#include <optional>

struct GpibAddress
{
    int board = 0;
    int primary = 3;
    QString elektorPort;

    bool operator==(const GpibAddress &other) const
    {
        return board == other.board && primary == other.primary && elektorPort == other.elektorPort;
    }
};

struct DeviceMarker
{
    double position = 0; // Hz in frequency mode, seconds in Zero Span
    double level = 0;
    QString unit;
};

struct Sweep
{
    double centerHz = 0;
    double spanHz = 0;
    double reference = 0;
    double division = 0;
    int verticalDivisions = 8;
    int scaleMode = 0;
    int levelUnitMode = 0;
    int triggerMode = 0;
    int minimumAttenuationDb = 0;
    double resolutionHz = 0;
    double videoHz = 0;
    double sweepSecondsPerDiv = 0;
    double attenuationDb = 0;
    QString unit;
    QByteArray referenceCode;
    QVector<double> write;
    QVector<double> traceA;
    std::optional<DeviceMarker> marker;

    double xStart() const;
    double xStop() const;
    double yStart() const;
    double yStep() const;
};

enum class DeviceOperation { Acquire, AcquireMarker, CommandOnly, Remote, Local };

struct SweepResult
{
    Sweep sweep;
    QString error;
    QString markerError;
};

SweepResult operateTr4131(GpibAddress address, DeviceOperation operation,
                          const QByteArrayList &commands = {});
SweepResult readTr4131Write(GpibAddress address, Sweep sweep);
