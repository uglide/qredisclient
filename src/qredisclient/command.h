#pragma once
#include <asyncfuture.h>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>
#include "response.h"

namespace RedisClient {

/**
 * @brief The Command class
 *
 * This class is part of Public API but should be used directly only for
 * advanced cases.
 */
class Command {
 public:
  typedef std::function<void(Response, QString)> Callback;

public:
    /**
     * @brief Constructs empty command
     */
    Command();

  /**
   * @brief Constructs command with out callback
   * @param cmd - Command parts
   * @param db - Database index where this command should be executed
   */
  Command(const QList<QByteArray>& cmd, int db = -1);

  /**
   * @brief Constructs command with callback
   * @param cmd - Command parts
   * @param context - QObject, command will be canceled if this object destroyed
   * @param callback - Callback for response processing
   * @param db - Database index where this command should be executed
   */
  Command(const QList<QByteArray>& cmd, QObject* context, Callback callback,
          int db = -1);

  /**
   * @brief ~Command
   */
  virtual ~Command();

  /**
   * @brief Append additional arg/part to command ("SET 1" + "2")
   * @param part
   * @return Reference to current object
   */
  Command& append(const QByteArray& part);

  /**
   * @brief Add a new command to pipeline
   * @param List of arguments
   * @return Reference to current object
   */
  Command& addToPipeline(const QList<QByteArray> cmd);

  /**
   * @brief length
   * @return Number of command arguments
   */
  int length() const;

  /**
   * @brief Get command in RESP or Pipeline format
   * @return QByteArray
   */
  QByteArray  getByteRepresentation() const;

  /**
   * @brief Get source command as single string
   * @return
   */
  QByteArray getRawString(int limit = 200) const;

  /**
   * @brief Get source command as list of args
   * @return
   */
  QList<QByteArray> getSplitedRepresentattion() const;

  /**
   * @brief Get specific argument/part of the command
   * @param i
   * @return
   */
  QString getPartAsString(int i) const;  

  /**
   * @brief getKeyName
   * @param cmd
   * @return
   */
  QByteArray getKeyName() const;

  /**
   * @brief getHashSlot
   * @return
   */
  quint16 getHashSlot() const;

  /**
   * @brief Get database index where this command should be executed
   * @return
   */
  int getDbIndex() const;

  /**
   * @brief hasDbIndex
   * @return
   */
  bool hasDbIndex() const;

  /**
   * @brief Get callback context
   * @return
   */
  QObject* getOwner() const;

  /**
   * @brief Set context and callback
   * @param context
   * @param callback
   */
  void setCallBack(QObject* context, Callback callback);

  /**
   * @brief getCallBack
   * @return
   */
  Callback getCallBack() const;

  /**
   * @brief hasCallback
   * @return
   */
  bool hasCallback() const;

  /**
   * @brief getFuture
   * @return
   */
  AsyncFuture::Deferred<Response> getDeferred() const;

  /**
   * @brief Mark this command as High Priority command.
   * Command will be added to the begining of the Connection queue instead of
   * end.
   */
  void markAsHiPriorityCommand();

  /**
   * @brief isHiPriorityCommand
   * @return
   */
  bool isHiPriorityCommand() const;

  /**
   * @brief Enable/disable pipeline mode. Default is off.
   * @param enable
   */
  void setPipelineCommand(const bool enable);

  /**
   * @brief isValid
   * @return
   */
  bool isValid() const;
  bool isEmpty() const;

  /*
   * Command type checks
   */
  bool isScanCommand() const;
  bool isSelectCommand() const;
  bool isSubscriptionCommand() const;
  bool isUnSubscriptionCommand() const;
  bool isAuthCommand() const;
  bool isPipelineCommand() const;

protected:
    /**
     * @brief Serialize command to RESP format
     * @return
     */
    QByteArray serializeToRESP(QList<QByteArray> args) const;

public:
  /**
   * @brief Parse command from raw string.
   * Useful for CLI clients.
   * @return Command parts as list.
   */
  static QList<QByteArray> splitCommandString(const QString&);

  static quint16 calcKeyHashSlot(const QByteArray& key);

protected:
    QObject * m_owner;
    QList<QByteArray> m_commandWithArguments;
    QList<QList<QByteArray>> m_pipelineCommands;
    int m_dbIndex;
    bool m_hiPriorityCommand;
    bool m_isPipeline;
    Callback m_callback;
    AsyncFuture::Deferred<Response> m_deferred;
};
}  // namespace RedisClient
