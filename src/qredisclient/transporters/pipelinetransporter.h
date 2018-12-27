#pragma once

#include "defaulttransporter.h"

namespace RedisClient {

/**
 * @brief The PipelineTransporter class
 * Provides execution of redis pipeline commands.
 */
class PipelineTransporter : public DefaultTransporter {
    Q_OBJECT

public:
    PipelineTransporter(Connection* c);
    ~PipelineTransporter() override;

public slots:
    void addCommand(const Command &) override;
    void readyRead() override;

private:
    QMutex m_pipelineMutex;
    Command m_pipelineCmd;
    int m_bulkLength;
};

}  // namespace RedisClient
