#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QtSerialPort/QSerialPortInfo>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    m_server = new QTcpServer();

    if(m_server->listen(QHostAddress::Any, 9001))
    {
       connect(this,SIGNAL(newMessage(QString)),this,SLOT(processMessage(QString)));
       connect(m_server, SIGNAL(newConnection()), this, SLOT(newConnection()));

       this->ui->statusBar->showMessage("Server is listening...");
    }
    else
    {
        QMessageBox::critical(this,"QTCPServer",QString("Unable to start the server: %1.").arg(m_server->errorString()));
        exit(EXIT_FAILURE);
    }

    // Setup SBUS
    m_sbusWorker = new QSbusThreadWorker;
    m_sbusWorker->moveToThread(&sbusThread);
    connect(&sbusThread, &QThread::finished, m_sbusWorker, &QObject::deleteLater);
    connect(this, &MainWindow::updateSbus, m_sbusWorker, &QSbusThreadWorker::update);
    connect(this, &MainWindow::openSbus, m_sbusWorker, &QSbusThreadWorker::open);
    connect(this->ui->comboBox_ports,&QComboBox::currentTextChanged,this,&MainWindow::serialPortChanged);
    connect(m_sbusWorker, &QSbusThreadWorker::statusMsg, this, &MainWindow::displayMessage);
    connect(m_sbusWorker, &QSbusThreadWorker::updateSbus, this,&MainWindow::processMessageSecondary);

    m_sbusReadWorker = new QSbusReadThreadWorker;
    m_sbusReadWorker->moveToThread(&sbusReadThread);
    connect(&sbusReadThread, &QThread::finished, m_sbusReadWorker, &QObject::deleteLater);
    connect(m_sbusReadWorker, &QSbusReadThreadWorker::updateSbus, m_sbusWorker, &QSbusThreadWorker::updateSecondary);

    connect(this, &MainWindow::openSbusSecondary, m_sbusReadWorker, &QSbusReadThreadWorker::open);
    connect(this->ui->comboBox_ports_2,&QComboBox::currentTextChanged,this,&MainWindow::serialPortChanged2);
    connect(m_sbusReadWorker, &QSbusReadThreadWorker::statusMsg, this, &MainWindow::displayMessage);

    m_CRSFReadWorker = new QCRSFReadThreadWorker;
    m_CRSFReadWorker->moveToThread(&crsfReadThread);
    connect(&crsfReadThread, &QThread::finished, m_CRSFReadWorker, &QObject::deleteLater);
    connect(m_CRSFReadWorker, &QCRSFReadThreadWorker::updateCRSF, m_sbusWorker, &QSbusThreadWorker::updateSecondary);

    connect(this, &MainWindow::openCRSFSecondary, m_CRSFReadWorker, &QCRSFReadThreadWorker::open);
    connect(this->ui->comboBox_ports_2,&QComboBox::currentTextChanged,this,&MainWindow::serialPortChanged2);
    connect(m_CRSFReadWorker, &QCRSFReadThreadWorker::statusMsg, this, &MainWindow::displayMessage);

    m_settings = new QSettings("FA-Tools","QTCPServer");
    m_serialPort = m_settings->value("SerialPort","ttyAMA1").toString();
    m_serialPort2 = m_settings->value("SerialPort2","ttyAMA3").toString();
    this->ui->comboBox_ports->addItem(m_serialPort);
    this->ui->comboBox_ports->setCurrentText(m_serialPort);
    this->ui->comboBox_ports_2->addItem(m_serialPort2);
    this->ui->comboBox_ports_2->setCurrentText(m_serialPort2);

    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (auto port : ports)
    {
        if (port.portName() != m_serialPort)
        {
            this->ui->comboBox_ports->addItem(port.portName());
        }
        if (port.portName() != m_serialPort2)
        {
            this->ui->comboBox_ports_2->addItem(port.portName());
        }
    }

    sbusThread.start();
    //sbusReadThread.start();
    crsfReadThread.start();
}

MainWindow::~MainWindow()
{
    if (sbusThread.isRunning())
    {
        sbusThread.quit();
        sbusThread.wait();
    }

    if (sbusReadThread.isRunning())
    {
        sbusReadThread.quit();
        sbusReadThread.wait();
    }

    if (crsfReadThread.isRunning())
    {
        crsfReadThread.quit();
        crsfReadThread.wait();
    }

    foreach (QTcpSocket* socket, connection_list)
    {
        socket->close();
        socket->deleteLater();
    }

    m_server->close();
    m_server->deleteLater();
    m_settings->deleteLater();

    delete ui;
}

void MainWindow::newConnection()
{
    while (m_server->hasPendingConnections())
        appendToSocketList(m_server->nextPendingConnection());
}

void MainWindow::appendToSocketList(QTcpSocket* socket)
{
    connection_list.append(socket);
    connect(socket, SIGNAL(readyRead()), this , SLOT(readSocket()));
    connect(socket, SIGNAL(disconnected()), this , SLOT(discardSocket()));
    this->ui->comboBox_receiver->addItem(QString::number(socket->socketDescriptor()));
}

void MainWindow::readSocket()
{
    QTcpSocket* socket = reinterpret_cast<QTcpSocket*>(sender());

    QByteArray block = socket->readAll();
    QDataStream in(&block, QIODevice::ReadOnly);
    in.setVersion(QDataStream::Qt_5_11);

    while (!in.atEnd())
    {
        QString receiveString;
        in >> receiveString;
        emit newMessage(receiveString);
    }
}

void MainWindow::discardSocket()
{
    QTcpSocket* socket = reinterpret_cast<QTcpSocket*>(sender());

    for (int i=0;i<connection_list.size();i++)
    {
        if (connection_list.at(i) == socket)
        {
            connection_list.removeAt(i);
            break;
        }
    }

    socket->deleteLater();
}

void MainWindow::on_pushButton_sendMessage_clicked()
{
    QString receiver = this->ui->comboBox_receiver->currentText();

    if(receiver=="Broadcast")
    {
        foreach (QTcpSocket* socket,connection_list)
        {
            sendMessage(socket);
        }
    }
    else
    {
        foreach (QTcpSocket* socket,connection_list)
        {
            if(socket->socketDescriptor() == receiver.toLongLong())
            {
                sendMessage(socket);
                break;
            }
        }
    }
    this->ui->lineEdit_message->clear();
}


void MainWindow::sendMessage(QTcpSocket* socket)
{
    if(socket)
    {
        if(socket->isOpen())
        {
            QString str = this->ui->lineEdit_message->text();

            QByteArray block;
            QDataStream out(&block, QIODevice::WriteOnly);

            out.setVersion(QDataStream::Qt_5_11);
            out << str;
            socket->write(block);
        }
        else
            QMessageBox::critical(this,"QTCPServer","Socket doesn't seem to be opened");
    }
    else
        QMessageBox::critical(this,"QTCPServer","Not connected");
}

void MainWindow::displayMessage(const QString& str)
{
    this->ui->textBrowser_receivedMessages->append(str);
}

void MainWindow::processMessage(const QString& str)
{
    if (str.isEmpty()) return;

    // Protocol starts with 'B' (begin), num axis, ... axis values (servo style), E (end) checksum
    const QList<QString> list = str.split(",");
    int index = 0;
    int channelCount = 0;
    int channelIndex = 0;
    int checksum = 0;

    for (int i=0; i<str.count(); i++)
    {
        if (str.at(i)=='E') break;
        checksum += str.at(i).toLatin1();
    }

    QList<int> channelValues;

    for (auto &token : list)
    {
        switch (index)
        {
            case 0:
            {
                // First token 'B' for begin
                if (token != "B")
                {
                    displayMessage(QString("Invalid string received 'B-missing' '%1'").arg(str));
                    return;
                }
            }
            break;
            case 1:
            {
                // Channel count
                channelCount = token.toInt();
                if (channelCount<1 || channelCount>16)
                {
                    displayMessage(QString("Invalid channel count '%1' received!").arg(token));
                    return;
                }
            }
            break;
            default:
            {
                if (channelCount>0)
                {
                    if (channelIndex<channelCount)
                    {
                        int channelValue = token.toInt();
                        channelValues.append(channelValue);
                        channelIndex++;
                    }
                }
            }
        }

        if (index==list.count()-2)
        {
            // 2nd to last token is 'E' (end)
            if (token != "E")
            {
                displayMessage(QString("Invalid string received 'E-missing', '%1'").arg(str));
                return;
            }
        }

        if (index==list.count()-1)
        {
            // Last token is checksum
            int cs = token.toInt();
            if (cs!=checksum)
            {
                displayMessage(QString("Checksum failed! '%1'").arg(str));
                return;
            }
        }

        index++;
    }

    emit updateSbus(channelValues);

    for (index = 0; index<channelValues.count(); index++)
    {
        int value = channelValues.at(index);
        switch (index)
        {
            case 0: this->ui->channel1->setValue(value);
            break;
            case 1: this->ui->channel2->setValue(value);
            break;
            case 2: this->ui->channel3->setValue(value);
            break;
            case 3: this->ui->channel4->setValue(value);
            break;
            case 4: this->ui->channel5->setValue(value);
            break;
            case 5: this->ui->channel6->setValue(value);
            break;
            case 6: this->ui->channel7->setValue(value);
            break;
            case 7: this->ui->channel8->setValue(value);
            break;
            case 8: this->ui->channel9->setValue(value);
            break;
            case 9: this->ui->channel10->setValue(value);
            break;
            case 10: this->ui->channel11->setValue(value);
            break;
            case 11: this->ui->channel12->setValue(value);
            break;
            case 12: this->ui->channel13->setValue(value);
            break;
            case 13: this->ui->channel14->setValue(value);
            break;
            case 14: this->ui->channel15->setValue(value);
            break;
            case 15: this->ui->channel16->setValue(value);
            break;
        }
    }
}

void MainWindow::processMessageSecondary(QList<int> channels)
{
    for (int index = 0; index<channels.count(); index++)
    {
        int value = channels.at(index);
        switch (index)
        {
            case 0: this->ui->channel1->setValue(value);
            break;
            case 1: this->ui->channel2->setValue(value);
            break;
            case 2: this->ui->channel3->setValue(value);
            break;
            case 3: this->ui->channel4->setValue(value);
            break;
            case 4: this->ui->channel5->setValue(value);
            break;
            case 5: this->ui->channel6->setValue(value);
            break;
            case 6: this->ui->channel7->setValue(value);
            break;
            case 7: this->ui->channel8->setValue(value);
            break;
            case 8: this->ui->channel9->setValue(value);
            break;
            case 9: this->ui->channel10->setValue(value);
            break;
            case 10: this->ui->channel11->setValue(value);
            break;
            case 11: this->ui->channel12->setValue(value);
            break;
            case 12: this->ui->channel13->setValue(value);
            break;
            case 13: this->ui->channel14->setValue(value);
            break;
            case 14: this->ui->channel15->setValue(value);
            break;
            case 15: this->ui->channel16->setValue(value);
            break;
        }
    }
}

void MainWindow::serialPortChanged(const QString &port)
{
    if (m_serialPort != port)
    {
        m_serialPort = port;
        m_settings->setValue("SerialPort",port);
        m_settings->sync();
    }

    emit openSbus(port);
}

void MainWindow::serialPortChanged2(const QString &port)
{
    if (m_serialPort2 != port)
    {
        m_serialPort2 = port;
        m_settings->setValue("SerialPort2",port);
        m_settings->sync();
    }

    // emit openSbusSecondary(port);
    emit openCRSFSecondary(port);
}
