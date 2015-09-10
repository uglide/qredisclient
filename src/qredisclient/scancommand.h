#pragma once
#include "command.h"

namespace RedisClient {

class ScanCommand : public Command
{
public:
    ScanCommand(const QList<QByteArray>& cmd, int db) : Command(cmd, db) {}
    ScanCommand(const QList<QByteArray>& cmd) : Command(cmd) {}

    void setCursor(int cursor);

    bool isValidScanCommand();

private:
    bool isKeyScanCommand(const QString& cmd);
    bool isValueScanCommand(const QString& cmd);
};

}
