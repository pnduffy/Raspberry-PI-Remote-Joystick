#ifndef QSBUSTHREADWORKER_H
#define QSBUSTHREADWORKER_H

#include <QObject>
#include <QDateTime>
#include <QReadWriteLock>
#include <SBUS.h>
#include <CrsfSerial.h>

class QSbusThreadWorker : public QObject
{
    Q_OBJECT
public:
    explicit QSbusThreadWorker(QObject *parent = nullptr);

signals:
    void statusMsg(const QString &msg);
    void updateSbus(QList<int> channels);

public slots:
    void open(QString port);
    void update(QList<int> channels);
    void updateTimer();
    void updateSecondary(QList<int> channels);

private:
    QList<int> channels;
    QList<int> channelsSecondary;
    QString m_port;
    QDateTime m_lastUpdateTime;
    QDateTime m_lastUpdateTimeSecondary;
    QReadWriteLock m_lock;
    SBUS m_sbus;
    bool m_isOpen;
    bool m_isFailSafe;
};

class QSbusReadThreadWorker : public QObject
{
    Q_OBJECT
public:
    explicit QSbusReadThreadWorker(QObject *parent = nullptr);

signals:
    void statusMsg(const QString &msg);
    void updateSbus(QList<int> channels);

public slots:
    void open(QString port);
    void updateTimer();
private:

    static void packetCallback(const sbus_packet_t &packet);
    static QSbusReadThreadWorker *s_pReadWorker;

    void packetCallback1(const sbus_packet_t &packet);

    QList<int> channels;
    QString m_port;
    SBUS m_sbus;
    bool m_isOpen;

};

class QCRSFReadThreadWorker : public QObject
{
    Q_OBJECT
public:
    explicit QCRSFReadThreadWorker(QObject *parent = nullptr);
    ~QCRSFReadThreadWorker() { if (m_pCRSF) delete m_pCRSF; }

signals:
    void statusMsg(const QString &msg);
    void updateCRSF(QList<int> channels);

public slots:
    void open(QString port);
    void updateTimer();
    void packetCallback();

private:
    QList<int> channels;
    QString m_port;
    CrsfSerial *m_pCRSF;
    bool m_isOpen;

};

Q_DECLARE_METATYPE(QList<int>)

#endif // QSBUSTHREADWORKER_H
