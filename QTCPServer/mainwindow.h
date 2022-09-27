#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDebug>
#include <QMessageBox>
#include <QMetaType>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QSettings>
#include <qsbusthreadworker.h>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    void newMessage(QString);
    void updateSbus(QList<int> channels);
    void openSbus(QString port);
    void openSbusSecondary(QString port);
    void openCRSFSecondary(QString port);

private slots:
    void newConnection();
    void appendToSocketList(QTcpSocket* socket);

    void readSocket();
    void discardSocket();

    void displayMessage(const QString& str);
    void processMessage(const QString& str);
    void processMessageSecondary(QList<int> channels);
    void sendMessage(QTcpSocket* socket);
    void serialPortChanged(const QString &);
    void serialPortChanged2(const QString &);

    void on_pushButton_sendMessage_clicked();

private:
    Ui::MainWindow *ui;

    QTcpServer* m_server;
    QList<QTcpSocket*> connection_list;
    QSbusThreadWorker *m_sbusWorker;
    QSbusReadThreadWorker *m_sbusReadWorker;
    QCRSFReadThreadWorker *m_CRSFReadWorker;
    QThread sbusThread;
    QThread sbusReadThread;
    QThread crsfReadThread;

    QSettings *m_settings;
    QString m_serialPort;
    QString m_serialPort2;

};

#endif // MAINWINDOW_H
