#include "response.h"
#include <QDebug>
#include <QVector>
#include <QObject>
#include <QVariantList>
#include "qredisclient/utils/text.h"
#include "qredisclient/utils/compat.h"

const redisReplyObjectFunctions RedisClient::Response::defaultFunctions = {
    RedisClient::Response::createStringObject,
    RedisClient::Response::createArrayObject,
    RedisClient::Response::createIntegerObject,
    RedisClient::Response::createNilObject,
    RedisClient::Response::freeObject
};

RedisClient::Response::Response()
    : m_responseSource(""), 
      m_redisReader(QSharedPointer<redisReader>(redisReaderCreate(), redisReaderFree)),
      m_result(nullptr)
{
}

RedisClient::Response::Response(const QByteArray & src)
    : m_responseSource(""),
      m_redisReader(QSharedPointer<redisReader>(redisReaderCreate(), redisReaderFree)),
      m_result(nullptr)
{
    feed(src);
}

RedisClient::Response::~Response(void)
{
}

void RedisClient::Response::setSource(const QByteArray& src)
{
    reset();
    feed(src);
}

void RedisClient::Response::reset()
{
    m_responseSource.clear();
    m_redisReader = QSharedPointer<redisReader>(redisReaderCreate(), redisReaderFree);
    m_result.clear();
}

QByteArray RedisClient::Response::source()
{
    return m_responseSource;
}

void RedisClient::Response::appendToSource(const QByteArray& src)
{
    feed(src);
}

QByteArray RedisClient::Response::getUnusedBuffer()
{
    if (!hasUnusedBuffer())
        return QByteArray{};

    return QByteArray::fromRawData(m_redisReader->buf, m_redisReader->len).mid(m_redisReader->pos);
}

QString RedisClient::Response::toRawString() const
{
    return m_responseSource.left(1500);
}

bool RedisClient::Response::isEmpty() const
{
    return m_responseSource.isEmpty();
}

RedisClient::Response::Type RedisClient::Response::getType() const
{
    return getResponseType(m_responseSource);
}

QVariant RedisClient::Response::getValue()
{
    if (!m_result.isNull() || parse()) // Return cached result
        return *m_result;
       
    return QVariant();
}   

QVariant* convertUnsafeArray(QVariant* arr)
{
    QVariantList result;
    auto val = arr->value<QVector<QVariant*>>();

    for (int index=0; index < val.size(); ++index) {
        if (val[index]->canConvert<QVector<QVariant*>>()) {
            QScopedPointer<QVariant> subArray(convertUnsafeArray(val[index]));
            result.append(*subArray);
        } else {
            result.append(*val[index]);
            delete val[index];
        }
    }
    delete arr;
    return (new QVariant(result));
}

bool RedisClient::Response::parse()
{
    if (m_responseSource.isEmpty())
        return false;

    QVariant* reply = nullptr;

    if (redisReaderGetReply(m_redisReader.data(), (void **)&reply) == REDIS_ERR) {
        qDebug() << "hiredis cannot parse buffer" << m_redisReader.data()->errstr;
        qDebug() << "current buffer:" << QByteArray::fromRawData(m_redisReader.data()->buf, m_redisReader.data()->len);
        qDebug() << "all buffer:" << m_responseSource;

        delete reply;

        return false;
    }

    if (!reply)
        return false;

    if (getType() == MultiBulk) {
        reply = convertUnsafeArray(reply);
    }

    m_result = QSharedPointer<QVariant>(reply);

    return true;
}

void RedisClient::Response::feed(const QByteArray& buffer)
{
    m_responseSource.append(buffer);

    if (redisReaderFeed(m_redisReader.data(), buffer.constData(), buffer.size()) == REDIS_ERR) {
        qDebug() << "hiredis cannot feed buffer, error:" << m_redisReader.data()->err;
        qDebug() << "current buffer:" << QByteArray::fromRawData(m_redisReader.data()->buf, m_redisReader.data()->len);
        qDebug() << "buffer part:" << buffer;
        qDebug() << "all buffer:" << m_responseSource;
    }
}

void setParent(const redisReadTask * task, QVariant* r)
{
    QVariant* parent = (QVariant*)task->parent->obj;
    Q_ASSERT(parent->isValid());

    auto value = parent->value<QVector<QVariant*>>();
    value[task->idx] = r;
    parent->setValue(value);
}

void *RedisClient::Response::createStringObject(const redisReadTask * task, char *str, size_t len)
{
    QVariant* s = new QVariant(QByteArray(str, len));

    if (task->parent)
        setParent(task, s);

    return s;
}

void *RedisClient::Response::createArrayObject(const redisReadTask * task, int elements)
{
    QVariant* arr = new QVariant();
    arr->setValue(QVector<QVariant*>(elements));

    if (task->parent)
       setParent(task, arr);

    return arr;
}

void *RedisClient::Response::createIntegerObject(const redisReadTask *task, long long value)
{
    QVariant* val = new QVariant(value);

    if (task->parent)
       setParent(task, val);

    return val;
}

void *RedisClient::Response::createNilObject(const redisReadTask *task)
{
    QVariant* nil = new QVariant();

    if (task->parent)
       setParent(task, nil);

    return nil;
}

void RedisClient::Response::freeObject(void *obj)
{
    if (obj == nullptr)
        return;

    QVariant* o = (QVariant*)obj;

    if (o->canConvert<QVector<QVariant*>>()) {
        auto val = o->value<QVector<QVariant*>>();

        for (int index=0; index < val.size(); ++index) {
            freeObject((void *)val[index]);
        }
    }

    delete o;
}

RedisClient::Response::Type RedisClient::Response::getResponseType(const QByteArray & r) const
{    
    const char typeChar = (r.length() == 0)? ' ' : r.at(0);
    return getResponseType(typeChar);
}

RedisClient::Response::Type RedisClient::Response::getResponseType(const char typeChar) const
{    
    if (typeChar == '+') return Status; 
    if (typeChar == '-') return Error;
    if (typeChar == ':') return Integer;
    if (typeChar == '$') return Bulk;
    if (typeChar == '*') return MultiBulk;
    return Unknown;
}

bool RedisClient::Response::isValid()
{
    return m_result || parse();
}

bool RedisClient::Response::isMessage() const
{
    if (!isArray())
        return false;

    QVariantList result = m_result->toList();

    return result.size() == 3 && result[0] == "message";
}

bool RedisClient::Response::isArray() const
{
    return m_result && m_result->canConvert(QMetaType::QVariantList);
}

bool RedisClient::Response::hasUnusedBuffer() const
{
    return m_result && m_redisReader->pos != m_redisReader->len;
}

bool RedisClient::Response::isAskRedirect() const
{
    return getResponseType(m_responseSource) == Error
            && m_responseSource.startsWith("-ASK");
}

bool RedisClient::Response::isMovedRedirect() const
{
    return getResponseType(m_responseSource) == Error
            && m_responseSource.startsWith("-MOVED");
}

QByteArray RedisClient::Response::getRedirectionHost() const
{
    if (!isMovedRedirect() && !isAskRedirect())
        return QByteArray();

    QByteArray hostAndPort = m_responseSource.split(' ')[2];

    return hostAndPort.split(':')[0];
}

uint RedisClient::Response::getRedirectionPort() const
{
    if (!isMovedRedirect() && !isAskRedirect())
        return 0;

    QByteArray hostAndPort = m_responseSource.split(' ')[2];

    return QString(hostAndPort.split(':')[1]).toUInt();
}

QByteArray RedisClient::Response::getChannel() const
{
    if (!isMessage())
        return QByteArray{};

    QVariantList result = m_result->toList();

    return result[1].toByteArray();
}

QString RedisClient::Response::valueToHumanReadString(const QVariant& value, int indentLevel)
{
    QString result;
    QString indent = QString(" ").repeated(indentLevel);

    if (value.isNull())
    {
        result = QString("null");
    }
    else if (value.type() == QVariant::Bool)
    {
        result = QString(value.toBool() ? "true" : "false");
    }
    else if (value.type() == QVariant::StringList)
    {
        int index = 1;
        for (QString line : value.toStringList()) {
            result.append(
                QString("%1 %2) %3\r\n")
                        .arg(indent)
                        .arg(QString::number(index++))
                        .arg(QString("\"%1\"").arg(line))
                        );
        }
    }
    else if (value.type() == QVariant::List || value.canConvert(QVariant::List))
    {
        QVariantList list = value.value<QVariantList>();

        int index = 1;
        for (QVariant item : list)
        {
            result.append(
                QString("%1 %2) %3\r\n")
                        .arg(indent)
                        .arg(QString::number(index++))
                        .arg(valueToHumanReadString(item, indentLevel + 1))
                        );
        }
    }
    else
    {
        result = QString("\"%1\"").arg(value.toString());
    }

    return indent + result;
}

bool RedisClient::Response::isErrorMessage() const
{
    return getResponseType(m_responseSource) == Error
            && m_responseSource.startsWith("-ERR");
}

bool RedisClient::Response::isDisabledCommandErrorMessage() const
{
    return isErrorMessage() && m_responseSource.contains("unknown command");
}

bool RedisClient::Response::isOkMessage() const
{
    return getResponseType(m_responseSource) == Status
            && m_responseSource.startsWith("+OK");
}
