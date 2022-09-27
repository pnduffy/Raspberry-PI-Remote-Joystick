#include "qsbusthreadworker.h"
#include <QTimer>

QSbusThreadWorker::QSbusThreadWorker(QObject *parent) : QObject(parent)
{
    m_isOpen = false;
    m_isFailSafe = false;
    m_port = "";
}

void QSbusThreadWorker::open(QString port)
{
    if (port != m_port || !m_isOpen)
    {
        m_port = port;
        sbus_err_t err = m_sbus.install(port.prepend("/dev/").toStdString().c_str(),true);
        if (err != SBUS_OK)
        {
            statusMsg(QString("Failed to open port '%1'").arg(port));
        }
        else
        {
            QTimer* sbusUpdateTimer = new QTimer(this);
            sbusUpdateTimer->setInterval(20);
            connect(sbusUpdateTimer, &QTimer::timeout, this, &QSbusThreadWorker::updateTimer);
            sbusUpdateTimer->start();

            statusMsg(QString("SBUS port open on '%1'").arg(port));
            m_isOpen = true;
        }
    }
}

void QSbusThreadWorker::updateTimer()
{
    if (m_isOpen && (channels.count()>0 || channelsSecondary.count()>0))
    {
        m_lock.lockForRead();

        bool useSecondary = false;
        sbus_packet_t packet;
        packet.failsafe = false;
        packet.frameLost = false;
        packet.ch17 = false;
        packet.ch18 = false;

        QDateTime now = QDateTime::currentDateTime();

        if (m_lastUpdateTime.isValid())
        {
            // Check if more than 3 seconds elapsed since last update
            if (m_lastUpdateTime.msecsTo(now)>3000)
            {
                // Check secondary
                if (m_lastUpdateTimeSecondary.isValid())
                {
                    if (m_lastUpdateTimeSecondary.msecsTo(now)>3000)
                    {
                        packet.failsafe = true;
                    }
                    else
                    {
                        useSecondary = true;
                    }
                }
                else
                {
                    packet.failsafe = true;
                }

                if (packet.failsafe && !m_isFailSafe)
                {
                    m_isFailSafe = true;
                    statusMsg(QString("Failsafe occured at '%1'").arg(now.toString()));
                }
            }
        }
        else
        {
            // Check secondary
            if (m_lastUpdateTimeSecondary.isValid())
            {
                if (m_lastUpdateTimeSecondary.msecsTo(now)>3000)
                {
                    packet.failsafe = true;
                }
                else
                {
                    useSecondary = true;
                }
            }
            else
            {
                packet.failsafe = true;
            }

            if (packet.failsafe && !m_isFailSafe)
            {
                m_isFailSafe = true;
                statusMsg(QString("Failsafe occured at '%1'").arg(now.toString()));
            }
        }

        for (int i=0; i<SBUS_NUM_CHANNELS; i++)
        {
            packet.channels[i] = 0;
        }

        if (useSecondary)
        {
            for (int i=0; i<channelsSecondary.count(); i++)
            {
                packet.channels[i]=uint16_t(channelsSecondary.at(i));
            }
        }
        else
        {
            for (int i=0; i<channels.count(); i++)
            {
                packet.channels[i]=uint16_t(channels.at(i));
            }
        }

        sbus_err_t err = m_sbus.write(packet);
        if (err != SBUS_OK)
        {
            statusMsg(QString("Failed to write SBUS port '%1'").arg(m_port));
        }

        m_lock.unlock();

        if (useSecondary)
        {
            // Send to main thread to update UI
            emit updateSbus(channelsSecondary);
        }
    }
}

void QSbusThreadWorker::update(QList<int> channels)
{
    m_lock.lockForWrite();
    this->channels = channels;
    m_lastUpdateTime = QDateTime::currentDateTime();
    if (m_isFailSafe)
    {
        statusMsg(QString("Failsafe cleared at '%1'").arg(m_lastUpdateTime.toString()));
        m_isFailSafe = false;
    }
    m_lock.unlock();
}

void QSbusThreadWorker::updateSecondary(QList<int> channels)
{
    m_lock.lockForWrite();
    this->channelsSecondary = channels;
    m_lastUpdateTimeSecondary = QDateTime::currentDateTime();
    if (m_isFailSafe)
    {
        statusMsg(QString("Failsafe secondary cleared at '%1'").arg(m_lastUpdateTimeSecondary.toString()));
        m_isFailSafe = false;
    }
    m_lock.unlock();
}

QSbusReadThreadWorker* QSbusReadThreadWorker::s_pReadWorker = nullptr;

QSbusReadThreadWorker::QSbusReadThreadWorker(QObject *parent) : QObject(parent)
{
    m_isOpen = false;
    m_port = "";
    s_pReadWorker = this;
}

void QSbusReadThreadWorker::open(QString port)
{
    if (port != m_port || !m_isOpen)
    {
        m_port = port;
        sbus_err_t err = m_sbus.install(port.prepend("/dev/").toStdString().c_str(),false);
        if (err != SBUS_OK)
        {
            statusMsg(QString("Failed to open read port '%1'").arg(port));
        }
        else
        {
            QTimer* sbusUpdateTimer = new QTimer(this);
            sbusUpdateTimer->setInterval(10);
            connect(sbusUpdateTimer, &QTimer::timeout, this, &QSbusReadThreadWorker::updateTimer);
            sbusUpdateTimer->start();
            m_sbus.onPacket(QSbusReadThreadWorker::packetCallback);

            statusMsg(QString("SBUS read port open on '%1'").arg(port));
            m_isOpen = true;
        }
    }
}

void QSbusReadThreadWorker::updateTimer()
{
    if (m_isOpen)
    {
        sbus_err_t err = m_sbus.read();
        if (err != SBUS_OK)
        {
            statusMsg(QString("SBUS read failed"));
        }
        else
        {
            const sbus_packet_t& lastPacket = m_sbus.lastPacket();
            packetCallback1(lastPacket);
        }
    }
}

void QSbusReadThreadWorker::packetCallback(const sbus_packet_t &packet)
{
    if (s_pReadWorker)
    {
        s_pReadWorker->packetCallback1(packet);
    }
}

void QSbusReadThreadWorker::packetCallback1(const sbus_packet_t &packet)
{
    // Only send if not failsafe
    if (!packet.failsafe)
    {
        channels.clear();

        for (int i=0; i<SBUS_NUM_CHANNELS; i++)
        {
            channels.append(packet.channels[i]);
        }

        emit updateSbus(channels);

    }
}

QCRSFReadThreadWorker::QCRSFReadThreadWorker(QObject *parent) : QObject(parent)
{
    m_isOpen = false;
    m_port = "";
    m_pCRSF = nullptr;
}

void QCRSFReadThreadWorker::open(QString port)
{
    if (port != m_port || !m_isOpen)
    {
        m_port = port;
        m_pCRSF = new CrsfSerial(port.prepend("/dev/").toStdString().c_str(),false);
        if (!m_pCRSF->isPortOpen())
        {
            statusMsg(QString("Failed to open read port '%1'").arg(port));
        }
        else
        {
            connect(m_pCRSF, &CrsfSerial::OnPacket, this, &QCRSFReadThreadWorker::packetCallback);
            QTimer* sbusUpdateTimer = new QTimer(this);
            sbusUpdateTimer->setInterval(5);
            connect(sbusUpdateTimer, &QTimer::timeout, this, &QCRSFReadThreadWorker::updateTimer);
            sbusUpdateTimer->start();
            statusMsg(QString("SBUS read port open on '%1'").arg(port));
            m_isOpen = true;
        }
    }
}

void QCRSFReadThreadWorker::updateTimer()
{
    if (m_pCRSF && m_isOpen)
    {
        m_pCRSF->loop();
    }
}

void QCRSFReadThreadWorker::packetCallback()
{
    // Only send if not failsafe
    channels.clear();

    for (unsigned int i=1; i<CRSF_NUM_CHANNELS; i++)
    {
        channels.append(m_pCRSF->getChannel(i));
    }

    emit updateCRSF(channels);
}

