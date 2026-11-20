#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

class GpibConnection
{
public:
    virtual ~GpibConnection() = default;
    virtual void send(const QByteArray &command) = 0;
    virtual QByteArray query(const QByteArray &command, qsizetype binaryBytes = 0) = 0;
    virtual unsigned statusByte() = 0;
    virtual void local() = 0;
    virtual void remote() = 0;
};

std::unique_ptr<GpibConnection> openElektorGpib(const QString &portName, int address);
