#include "pipelinetransporter.h"
#include <QThread>

namespace RedisClient {

PipelineTransporter::PipelineTransporter(Connection* c) :
    DefaultTransporter(c)
{
}

PipelineTransporter::~PipelineTransporter()
{
}

void PipelineTransporter::addCommand(const Command &cmd)
{
    QMutexLocker locker(&m_pipelineMutex);
    m_pipelineCmd = cmd;

    // Default behavior is it's not a pipeline command
    if (!cmd.isPipelineCommand())
    {
        DefaultTransporter::addCommand(cmd);
        return;
    }

    m_bulkLength = cmd.length();
    QByteArray cmdBytes = cmd.getByteRepresentation();
    sendCommand(cmdBytes);

    emit commandAdded();
}

void PipelineTransporter::readyRead()
{
    QMutexLocker locker(&m_pipelineMutex);
    if (m_pipelineCmd.isPipelineCommand())
    {
        while (m_bulkLength >= 0 && canReadFromSocket())
        {
            QByteArray tmp = m_socket->readLine();
            m_bulkLength--;
        }
        if (m_bulkLength >= 0)
            return;

        if (canReadFromSocket())
        {
            QByteArray src = readFromSocket();
            m_response.appendToSource(src);
            if (!m_response.isValid())
                return;

            // The exec response should be OK and we can return it
            RedisClient::Response responseToSend = m_response;
            m_response.reset();

            auto runningCmd = QSharedPointer<RedisClient::AbstractTransporter::RunningCommand>(new RedisClient::AbstractTransporter::RunningCommand(m_pipelineCmd));
            m_runningCommands.enqueue(runningCmd);
            sendResponse(responseToSend);
        }
    }
    else
        DefaultTransporter::readyRead();
}

};
