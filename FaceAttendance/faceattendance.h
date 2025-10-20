#ifndef FACEATTENDANCE_H
#define FACEATTENDANCE_H

#include <QMainWindow>
#include <opencv.hpp>
#include <QTcpSocket>
#include <QTimer>
#include <QSerialPort>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

using namespace cv;
using namespace std;

QT_BEGIN_NAMESPACE
namespace Ui
{
    class FaceAttendance;
}
QT_END_NAMESPACE

class FaceAttendance : public QMainWindow
{
    Q_OBJECT

public:
    FaceAttendance(QWidget *parent = nullptr);
    ~FaceAttendance();

    // 定时器事件
    void timerEvent(QTimerEvent *e);

protected slots:
    void recv_data();
private slots:
    void timer_connect();
    void stop_connect();
    void start_connect();

    void readSerialData();

    void startMjpegStream();
    void processMjpegStreamData();
    void processJpegFrame(const QByteArray &jpegData);

private:
    Ui::FaceAttendance *ui;

    // haar--级联分类器
    cv::CascadeClassifier cascade;

    // ESP32-CAM 相关
    QNetworkAccessManager *networkManager;
    QTimer *captureTimer;
    cv::Mat currentFrame;

    // 创建网络套接字,定时器
    QTcpSocket msocket;
    QTimer mtimer;

    // 标志是否是 同一个 人脸进入到识别区域
    // int flag;
    int faceStillCount;
    bool hasSent;

    // 保存 人脸的数据
    cv::Mat faceMat;

    // 串口
    QSerialPort *serial;

    QByteArray lastETag; // 用于HTTP缓存控制

    QByteArray mjpegBuffer;
    bool streamActive;
};
#endif // FACEATTENDANCE_H
