#pragma once
#include <QObject>
#include <functional>
#include <QVariantList>
#include <QSharedPointer>
#include <QEventLoop>
#include <QTimer>
#include <QMutex>
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
    bool clusterMode;
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

    friend class AbstractTransporter;

public:
    enum class Mode { Normal, PubSub, Cluster };
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
    virtual bool connect();

    /**
     * @brief isConnected
     * @return
     */
    virtual bool isConnected();

    /**
     * @brief disconnect from redis-server
     */
    virtual void disconnect();

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
     * @brief command
     * @param cmd
     */
    void command(const Command& cmd);

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
     * @brief commandSync
     * @param cmd
     * @return
     */
    Response commandSync(const Command& cmd);

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
    Response commandSync(QString cmd, QString arg1, QString arg2, QString arg3, int db = -1);

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
    virtual void runCommand(Command &cmd);

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
    void createTransporter();
    bool isTransporterRunning();

    Response internalCommandSync(QList<QByteArray> rawCmd);

    void processScanCommand(QSharedPointer<ScanCommand> cmd,
                            CollectionCallback callback,
                            QSharedPointer<QVariantList> result=QSharedPointer<QVariantList>());

    void changeCurrentDbNumber(int db);

protected slots:
    void auth();

protected:
    ConnectionConfig m_config;   
    QSharedPointer<QThread> m_transporterThread;
    QSharedPointer<AbstractTransporter> m_transporter;

    int m_dbNumber;
    ServerInfo m_serverInfo;
    Mode m_currentMode;
    QMutex m_dbNumberMutex;
};
}
