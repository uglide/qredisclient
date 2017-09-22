#include "scancommand.h"


void RedisClient::ScanCommand::setCursor(long long cursor)
{
    if (cursor <= 0)
        return;

    if (isKeyScanCommand(m_commandWithArguments[0])) {
        m_commandWithArguments[1] = QString::number(cursor).toUtf8();
    } else if (isValueScanCommand(m_commandWithArguments[0])) {
        m_commandWithArguments[2] = QString::number(cursor).toUtf8();
    }
}

bool RedisClient::ScanCommand::isValidScanCommand() const
{
    auto parts = getSplitedRepresentattion();

    return (parts.size() > 1 && isKeyScanCommand(parts[0]))
            || (parts.size() > 2 && isValueScanCommand(parts[0]));
}

bool RedisClient::ScanCommand::isKeyScanCommand(const QString &cmd) const
{
    return cmd.toLower() == "scan";
}

bool RedisClient::ScanCommand::isValueScanCommand(const QString &cmd) const
{
    return cmd.toLower() == "zscan"
            || cmd.toLower() == "sscan"
            || cmd.toLower() == "hscan";
}
