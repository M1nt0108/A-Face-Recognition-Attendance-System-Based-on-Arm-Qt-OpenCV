#include "faceattendance.h"
#include "ui_faceattendance.h"
#include <QImage>
#include <QPainter>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>

#include <QSerialPort>
#include <QSerialPortInfo>

#include <QBuffer>
#include <QRegularExpression>

#include <QDate>
#include <QTime>

static int faceStillCount = 0; // 连续检测到人脸的次数
static bool hasSent = false;

FaceAttendance::FaceAttendance(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::FaceAttendance)
{
    ui->setupUi(this);

    serial = new QSerialPort(this);
    serial->setPortName("COM5");                  // 改成你实际使用的串口号
    serial->setBaudRate(QSerialPort::Baud115200); // 或9600，根据 STM32 设置
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);
    serial->setFlowControl(QSerialPort::NoFlowControl);

    if (serial->open(QIODevice::ReadWrite))
    {
        qDebug() << "串口打开成功";
    }
    else
    {
        qDebug() << "串口打开失败：" << serial->errorString();
    }

    // 绑定串口接收槽函数
    connect(serial, &QSerialPort::readyRead, this, &FaceAttendance::readSerialData);

    // 初始化网络管理器和定时器
    networkManager = new QNetworkAccessManager(this);

    // 无需使用计时器频繁请求，MJPEG是持续的流
    // captureTimer->start(50);

    // 代替定时器，直接启动流
    startMjpegStream();

    // 导入级联分类器文件
    cascade.load("E:/ARM_QT_opencv_item/opencv452/etc/haarcascades/haarcascade_frontalface_alt2.xml");

    // QTcpSocket当断开连接的时候disconnect信号,连接成功就会发送connect信号
    connect(&msocket, &QTcpSocket::disconnected, this, &FaceAttendance::start_connect);
    connect(&msocket, &QTcpSocket::connected, this, &FaceAttendance::stop_connect);

    // 关联接收数据的槽函数
    connect(&msocket, &QTcpSocket::readyRead, this, &FaceAttendance::recv_data);

    // 定时器连接服务器
    connect(&mtimer, &QTimer::timeout, this, &FaceAttendance::timer_connect);
    // 启动定时器
    mtimer.start(5000); // 每5s连接一次,直到成功

    // flag = 0;

    ui->widgetLb->hide();
}

FaceAttendance::~FaceAttendance()
{
    delete ui;
}

void FaceAttendance::timerEvent(QTimerEvent *e)
{
    // 这个函数可以不做任何事情，或者用于其他定时任务
    // 人脸检测已经在processReceivedFrame中完成
}

void FaceAttendance::recv_data()
{
    QByteArray array = msocket.readAll();
    qDebug() << "Received from backend:" << array;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(array, &err);
    if (err.error != QJsonParseError::NoError)
    {
        qDebug() << "JSON parsing error:" << err.errorString();
        return;
    }

    QJsonObject obj = doc.object();
    QString employeeID = obj.value("employeeID").toString();
    QString name = obj.value("name").toString(); // 仍然接收name用于UI显示和判断未知用户
    QString department = obj.value("department").toString();
    QString timestr = obj.value("time").toString(); // 后端返回的打卡时间字符串

    // --- UI 更新 ---
    if (name.isEmpty())
        name = "未知用户";
    if (department.isEmpty())
        department = "未知部门";
    // 如果后端时间为空，使用当前系统时间作为备用显示
    if (timestr.isEmpty())
        timestr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    ui->numberEdit->setText(employeeID);
    ui->nameEdit->setText(name); // UI上仍然显示姓名
    ui->departmentEdit->setText(department);
    ui->timeEdit->setText(timestr); // UI上显示后端返回的打卡时间

    // 显示头像和信息框
    ui->headLb->setStyleSheet("border-radius:75px;border-image: url(./face.jpg);");
    ui->widgetLb->show();
    // --- UI 更新结束 ---

    // --- STM32 通信逻辑 ---
    // 检查是否为未知用户或ID为空，如果是则不发送到STM32
    if (name == "未知用户" || employeeID.isEmpty())
    {
        qDebug() << "Unknown user or empty ID, not sending to STM32.";
        return; // 不处理未知用户或空ID
    }

    // 获取当前时间和日期
    QString currentTime = QTime::currentTime().toString("HH:mm");
    QString currentDate = QDate::currentDate().toString("yyyy-MM-dd"); // 获取 yyyy-MM-dd 格式日期

    // 构建包含日期的新消息格式
    // 格式: Hello STM32,id:123,time:14:35,date:2024-07-26\n
    QString stm32Message = QString("Hello STM32,id:%1,time:%2,date:%3\n")
                               .arg(employeeID)
                               .arg(currentTime)
                               .arg(currentDate); // 添加日期参数

    qDebug() << "Sending to STM32:" << stm32Message;
    qint64 bytesWritten = serial->write(stm32Message.toUtf8());
    if (bytesWritten == -1)
    {
        qDebug() << "Failed to write to serial port:" << serial->errorString();
    }
    else
    {
        qDebug() << bytesWritten << "bytes written.";
        // 等待发送完成，增加超时判断
        if (!serial->waitForBytesWritten(1000))
        {
            qDebug() << "Serial write timeout or error:" << serial->errorString();
        }
        else
        {
            qDebug() << "Serial write finished.";
        }
    }
    // --- STM32 通信逻辑结束 ---
}
void FaceAttendance::timer_connect()
{
    // 连接服务器
    msocket.connectToHost("192.168.111.171", 9999);
    qDebug() << "正在连接服务器";
}

void FaceAttendance::stop_connect()
{
    mtimer.stop();
    qDebug() << "成功连接服务器";
}

void FaceAttendance::start_connect()
{
    mtimer.start(5000); // 启动定时器
    qDebug() << "断开连接";
}

void FaceAttendance::readSerialData()
{
    QByteArray data = serial->readAll();
    QString dataStr = QString::fromUtf8(data); // 转为字符串方便处理
    qDebug() << "Received from STM32:" << dataStr;

    // 检查是否包含开门成功的反馈信息
    if (dataStr.contains("OK: Door Open"))
    {
        qDebug() << "STM32 confirmed: Door Open command executed.";
        // 在这里可以更新UI或执行其他需要确认的操作
    }
    // 可以添加对其他STM32反馈信息的处理
    else if (dataStr.contains("Simulating Door Open..."))
    {
        qDebug() << "STM32 is simulating door opening.";
    }
    else if (dataStr.contains("Time Sync:"))
    {
        qDebug() << "STM32 confirmed time sync.";
    }
    // ... 可以添加更多 else if 来处理其他调试或状态信息 ...
}

void FaceAttendance::startMjpegStream()
{
    // 停止可能存在的旧流
    streamActive = false;

    // 清空缓冲区
    mjpegBuffer.clear();

    // 使用已知可以工作的URL
    QUrl url("http://192.168.111.2:81/stream");
    QNetworkRequest request(url);

    // 设置请求头
    request.setRawHeader("Connection", "keep-alive");

    // 发起请求并连接响应处理
    QNetworkReply *reply = networkManager->get(request);

    // 连接数据接收信号
    connect(reply, &QNetworkReply::readyRead, this, &FaceAttendance::processMjpegStreamData);

    // 连接错误和完成信号
    connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error),
            [this, reply](QNetworkReply::NetworkError)
            {
                qDebug() << "流错误:" << reply->errorString();
                reply->deleteLater();
                // 短暂延迟后重新连接
                QTimer::singleShot(1000, this, &FaceAttendance::startMjpegStream);
            });

    connect(reply, &QNetworkReply::finished, [this, reply]()
            {
        if (!reply->error()) {
            qDebug() << "流结束";
        }
        reply->deleteLater();
        // 如果流正常结束，尝试重新连接
        if (streamActive) {
            QTimer::singleShot(100, this, &FaceAttendance::startMjpegStream);
        } });

    streamActive = true;
    qDebug() << "已连接MJPEG流:" << url.toString();
}

void FaceAttendance::processMjpegStreamData()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;

    // 将新数据添加到缓冲区
    QByteArray newData = reply->readAll();
    mjpegBuffer.append(newData);

    // 调试信息
    // qDebug() << "收到数据大小:" << newData.size() << "总缓冲区大小:" << mjpegBuffer.size();

    // 搜索JPEG图像的关键标记
    // JPEG文件以 FF D8 开始，以 FF D9 结束
    int startIndex = mjpegBuffer.indexOf("\xFF\xD8");
    while (startIndex >= 0)
    {
        int endIndex = mjpegBuffer.indexOf("\xFF\xD9", startIndex);
        if (endIndex == -1)
        {
            // 没有找到完整的JPEG图像
            break;
        }

        // 完整的JPEG图像
        endIndex += 2; // 包括FF D9结束标记
        QByteArray jpegData = mjpegBuffer.mid(startIndex, endIndex - startIndex);

        // 处理JPEG图像
        // qDebug() << "找到一个完整JPEG图像，大小:" << jpegData.size() << "字节";
        processJpegFrame(jpegData);

        // 移除已处理的数据
        mjpegBuffer.remove(0, endIndex);

        // 查找下一个图像
        startIndex = mjpegBuffer.indexOf("\xFF\xD8");
    }

    // 如果缓冲区过大，清理开头部分直到找到可能的JPEG开始标记
    if (mjpegBuffer.size() > 1000000)
    {
        int nextStart = mjpegBuffer.indexOf("\xFF\xD8");
        if (nextStart > 0)
        {
            mjpegBuffer.remove(0, nextStart);
        }
        else if (nextStart == -1)
        {
            // 没有找到开始标记，清空整个缓冲区
            mjpegBuffer.clear();
            qDebug() << "缓冲区过大且无有效数据，已清空";
        }
    }
}

void FaceAttendance::processJpegFrame(const QByteArray &jpegData)
{
    // 将JPEG数据转换为OpenCV格式
    std::vector<uchar> buffer(jpegData.begin(), jpegData.end());
    currentFrame = cv::imdecode(buffer, cv::IMREAD_COLOR);

    if (!currentFrame.empty())
    {
        // qDebug() << "成功解码图像，大小:" << currentFrame.cols << "x" << currentFrame.rows;

        // 这里的代码与之前的processReceivedFrame函数中的图像处理部分相同
        Mat srcImage = currentFrame.clone();

        // 调整为满足UI尺寸的480x480
        cv::resize(srcImage, srcImage, Size(480, 480));

        // 人脸检测（可选择在小尺寸图像上进行以提高速度）
        Mat grayImage;
        cvtColor(srcImage, grayImage, COLOR_BGR2GRAY);

        // 检测人脸
        std::vector<Rect> faceRects;
        cascade.detectMultiScale(grayImage, faceRects, 1.1, 2, 0, Size(30, 30));

        if (faceRects.size() > 0)
        {
            Rect rect = faceRects.at(0);
            // 注释掉这行来隐藏红框
            // rectangle(srcImage, rect, Scalar(0, 0, 255), 2);

            // 移动人脸框
            ui->headpicLb->move(rect.x - 50, rect.y - 50);

            faceStillCount++;

            if (faceStillCount >= 5 && !hasSent)
            {
                // 发送逻辑（与之前相同）
                std::vector<uchar> buf;
                cv::imencode(".jpg", srcImage, buf);
                QByteArray byte((const char *)buf.data(), buf.size());
                quint64 backsize = byte.size();
                QByteArray sendData;
                QDataStream stream(&sendData, QIODevice::WriteOnly);
                stream.setVersion(QDataStream::Qt_5_14);
                stream << backsize << byte;
                msocket.write(sendData);

                faceMat = srcImage(rect);
                imwrite("./face.jpg", faceMat);
                hasSent = true;

                qDebug() << "人脸已检测并发送！";
            }
        }
        else
        {
            // 重置状态
            faceStillCount = 0;
            hasSent = false;
            ui->headpicLb->move(100, 60);
            ui->widgetLb->hide();
            ui->numberEdit->clear();
            ui->nameEdit->clear();
            ui->departmentEdit->clear();
            ui->timeEdit->clear();
            ui->headLb->setStyleSheet("");
        }

        // 显示图像
        cv::Mat rgbImage;
        cv::cvtColor(srcImage, rgbImage, COLOR_BGR2RGB);
        QImage image(rgbImage.data, rgbImage.cols, rgbImage.rows, rgbImage.step, QImage::Format_RGB888);
        QPixmap pixmap = QPixmap::fromImage(image);
        ui->videoLb->setPixmap(pixmap);
    }
    else
    {
        qDebug() << "图像解码失败!";
    }
}
