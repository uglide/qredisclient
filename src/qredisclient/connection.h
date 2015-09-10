#pragma once
#include <QObject>
#include <functional>
#include <QVariantList>
#include <QSharedPointer>
#include <QEventLoop>
#include <QTimer>
#include "connectionconfig.h"
#include "exception.h"
#include "command.h"
#include "response.h"
#include "scancommand.h"

namespace RedisClient {

class AbstractTransporter;
class Executor;

/**
 * @brief The ServerInfo struct
 * Represents redis-server information parsed from INFO command.
 */
struct ServerInfo
{
    double version;
    static ServerInfo fromString(const QString& info);
};

/**
 * @brief The Connection class
 * Main client class.
 */
class Connection : public QObject
{
    Q_OBJECT
    ADD_EXCEPTION
public:
    enum class Mode { Normal, PubSub };
    class InvalidModeException : public Connection::Exception {};
public:
    /**
     * @brief Constructs connection class
     * @param c - connection config
     * NOTE: different config options are required for different transporters.
     */
    Connection(const ConnectionConfig &c);

    /**
     * @brief ~Connection
     * If connection established internally call disconnect()
     */
    virtual ~Connection();

    /**
     * @brief connects to redis-server
     * @return true - on success
     * @throws Connection::Exception if config is invalid or something went wrong.
     */
    bool connect();

    /**
     * @brief isConnected
     * @return
     */
    bool isConnected();

    /**
     * @brief Wait connected state for timeout
     * timeout - in miliseconds
     * @return
     */
    bool waitConnectedState(unsigned int);

    /**
     * @brief disconnect from redis-server
     */
    void disconnect();

    /**
     * @brief getConfig
     * @return
     */
    ConnectionConfig getConfig() const;

    /**
     * @brief setConnectionConfig
     */
    void setConnectionConfig(const ConnectionConfig &);

    /**
     * @brief Get current mode
     * @return
     */
    Mode mode() const;

    /**
     * @brief Get redis-server version
     * @return
     */
    virtual double getServerVersion();

    /*
     * Command execution API
     */
    /**
     * @brief Execute command without callback in async mode.
     * @param rawCmd
     * @param db
     */
    void command(QList<QByteArray> rawCmd, int db = -1);

    /**
     * @brief Execute command with callback in async mode.
     * @param rawCmd
     * @param owner
     * @param callback
     * @param db
     */
    void command(QList<QByteArray> rawCmd, QObject *owner,
                 RedisClient::Command::Callback callback, int db = -1);

    /**
     * @brief Execute command without callback and wait for response.
     * @param rawCmd
     * @param db
     * @return
     */
    Response commandSync(QList<QByteArray> rawCmd, int db = -1);
    /*
     * Aliases for ^ function
     */
    Response commandSync(QString cmd, int db = -1);
    Response commandSync(QString cmd, QString arg1, int db = -1);
    Response commandSync(QString cmd, QString arg1, QString arg2, int db = -1);

    /**
     * @brief CollectionCallback
     */
    typedef std::function<void(QVariant, QString err)>  CollectionCallback;

    /**
     * @brief retrieveCollection
     * @param cmd
     * @param callback
     */
    virtual void retrieveCollection(QSharedPointer<ScanCommand> cmd,
                                    CollectionCallback callback);

    /**
     * @brief runCommand - Low level commands execution API
     * @param cmd
     */
    virtual void runCommand(const Command &cmd);

    /*
     * Low level functions for modification
     * commands execution.
     */
    void setTransporter(QSharedPointer<AbstractTransporter>);
    QSharedPointer<AbstractTransporter> getTransporter() const;

signals:    
    void addCommandToWorker(Command);
    void error(const QString&);
    void log(const QString&);
    void connected();
    void disconnected();
    void authOk();
    void authError(const QString&);

protected:
    void setConnectedState();
    void createTransporter();
    bool isTransporterRunning();

    void processScanCommand(QSharedPointer<ScanCommand> cmd,
                            CollectionCallback callback,
                            QSharedPointer<QVariantList> result=QSharedPointer<QVariantList>());

    Response commandSync(QList<QByteArray> rawCmd,
                         bool isHiPriorityCommand, int db = -1);

protected slots:
    void connectionReady();
    void commandAddedToTransporter();
    void auth();

protected:
    ConnectionConfig m_config;
    bool m_connected;    
    QSharedPointer<QThread> m_transporterThread;
    QSharedPointer<AbstractTransporter> m_transporter;
    QEventLoop m_connectionLoop;
    QEventLoop m_cmdLoop;

    QTimer m_timeoutTimer;
    int m_dbNumber;
    ServerInfo m_serverInfo;
    Mode m_currentMode;
};
}
