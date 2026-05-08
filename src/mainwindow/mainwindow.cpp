#include "mainwindow.h"
#include "../drm/drm_display.h"
#include "../bridge/shared_mem_bridge.h"

#include <QPainter>
#include <QKeyEvent>
#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

// ============================================================================
// 构造与析构
// ============================================================================

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_drmDisplay(nullptr)
    , m_bridge(nullptr)
    , m_refreshTimer(nullptr)
    , m_frameCount(0)
    , m_currentFps(0.0f)
    , m_lastFrameSequence(0)
    , m_statusLabel(nullptr)
    , m_videoShmPath("/dev/shm/video_frame_shm")
    , m_faceShmPath("/dev/shm/face_data_shm")
    , m_socketPath("/tmp/monitor_control.sock")
    , m_refreshIntervalMs(33)  // ~30fps
{
    // 初始化默认视频帧
    memset(&m_currentFrame, 0, sizeof(VideoFrame));
    m_currentFrame.fd = -1;

    // 设置窗口属性：无边框全屏
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    // 尝试加载配置文件
    QString configPath = "/opt/rk3588_monitor/config/app_config.json";
    if (QFile::exists(configPath)) {
        QFile configFile(configPath);
        if (configFile.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(configFile.readAll());
            QJsonObject root = doc.object();
            if (root.contains("video_shm_path")) {
                m_videoShmPath = root["video_shm_path"].toString();
            }
            if (root.contains("face_shm_path")) {
                m_faceShmPath = root["face_shm_path"].toString();
            }
            if (root.contains("socket_path")) {
                m_socketPath = root["socket_path"].toString();
            }
            if (root.contains("refresh_interval_ms")) {
                m_refreshIntervalMs = root["refresh_interval_ms"].toInt();
            }
            qDebug() << "[MainWindow] 已加载配置文件:" << configPath;
        }
    }
}

MainWindow::~MainWindow()
{
    if (m_refreshTimer) {
        m_refreshTimer->stop();
    }

    // DrmDisplay和SharedMemBridge由Qt父子关系自动释放
    qDebug() << "[MainWindow] 已销毁";
}

// ============================================================================
// 初始化
// ============================================================================

bool MainWindow::initialize()
{
    qDebug() << "[MainWindow] 开始初始化...";

    // 1. 初始化DRM显示
    m_drmDisplay = new DrmDisplay();
    if (!m_drmDisplay->initialize()) {
        qWarning() << "[MainWindow] DRM显示初始化失败！";
        // DRM初始化失败不一定是致命错误，可能在桌面环境下运行
        // 此时视频显示功能不可用，但UI仍可工作
    } else {
        qDebug() << "[MainWindow] DRM显示初始化成功，分辨率:"
                 << m_drmDisplay->screenWidth() << "x" << m_drmDisplay->screenHeight();
    }

    // 2. 设置窗口大小为屏幕分辨率
    if (m_drmDisplay->isInitialized() &&
        m_drmDisplay->screenWidth() > 0 &&
        m_drmDisplay->screenHeight() > 0) {
        setFixedSize(m_drmDisplay->screenWidth(), m_drmDisplay->screenHeight());
    } else {
        // 回退：使用默认分辨率或从环境变量获取
        setFixedSize(1920, 1080);
    }

    // 3. 初始化共享内存桥接模块
    m_bridge = new SharedMemBridge(this);
    if (!m_bridge->initialize(m_videoShmPath, m_faceShmPath, m_socketPath)) {
        qWarning() << "[MainWindow] 共享内存桥接初始化失败！";
        // 同样不视为致命错误
    }

    // 4. 连接信号槽
    connect(m_bridge, &SharedMemBridge::controlCommandReceived,
            this, &MainWindow::onControlCommand);
    connect(m_bridge, &SharedMemBridge::socketConnectionChanged,
            this, &MainWindow::onSocketConnectionChanged);

    // 5. 创建状态栏标签（用于测试UI叠加）
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(0, 0, 0, 160);"
        "  color: #00ff00;"
        "  font-size: 14px;"
        "  padding: 4px 8px;"
        "  border-radius: 4px;"
        "}"
    );
    m_statusLabel->setText("RK3588 Monitor | 等待连接...");
    m_statusLabel->adjustSize();
    m_statusLabel->move(10, 10);

    // 6. 启动刷新定时器
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    m_refreshTimer->start(m_refreshIntervalMs);

    // 7. 启动FPS计时器
    m_fpsTimer.start();

    // 8. 全屏显示
    showFullScreen();

    qDebug() << "[MainWindow] 初始化完成，刷新间隔:" << m_refreshIntervalMs << "ms";
    return true;
}

// ============================================================================
// 绘制事件
// ============================================================================

void MainWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 清除背景（半透明黑色，以便视频层可见）
    painter.fillRect(rect(), QColor(0, 0, 0, 30));

    // 绘制人脸识别框
    if (!m_currentFaces.isEmpty()) {
        drawFaceBoxes(painter, m_currentFaces);
    }

    // 绘制状态信息叠加
    drawStatusInfo(painter);
}

// ============================================================================
// 按键事件
// ============================================================================

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        qDebug() << "[MainWindow] ESC键按下，退出应用";
        QApplication::quit();
    }
    QMainWindow::keyPressEvent(event);
}

// ============================================================================
// 定时器槽函数
// ============================================================================

void MainWindow::onRefreshTimer()
{
    // 1. 从共享内存获取最新数据
    m_currentFrame = m_bridge->getCurrentVideoFrame();
    m_currentFaces = m_bridge->getCurrentFaces();

    // 2. 更新视频帧到DRM叠加平面
    if (m_drmDisplay->isInitialized() && m_currentFrame.fd >= 0) {
        // 检查帧序号是否更新（避免重复更新同一帧）
        if (m_currentFrame.sequence != m_lastFrameSequence) {
            if (!m_drmDisplay->updateFrame(m_currentFrame)) {
                qWarning() << "[MainWindow] 更新视频帧失败";
            }
            m_lastFrameSequence = m_currentFrame.sequence;
        }
    }

    // 3. 更新FPS
    updateFps();

    // 4. 请求Qt重绘UI
    update();
}

void MainWindow::onControlCommand(const QString& cmd)
{
    qDebug() << "[MainWindow] 处理控制命令:" << cmd;

    if (cmd.startsWith("SWITCH_SOURCE")) {
        // 处理切换视频源命令
        QString source = cmd.contains(":") ? cmd.section(':', 1) : "default";
        qDebug() << "[MainWindow] 切换视频源到:" << source;
        // TODO: 实现视频源切换逻辑
    } else if (cmd == "START_STREAM") {
        qDebug() << "[MainWindow] 开始推流";
        // TODO: 实现开始推流逻辑
    } else if (cmd == "STOP_STREAM") {
        qDebug() << "[MainWindow] 停止推流";
        // TODO: 实现停止推流逻辑
    } else {
        qWarning() << "[MainWindow] 未知命令:" << cmd;
    }
}

void MainWindow::onSocketConnectionChanged(bool connected)
{
    if (m_statusLabel) {
        if (connected) {
            m_statusLabel->setText("RK3588 Monitor | 已连接");
            m_statusLabel->setStyleSheet(
                "QLabel {"
                "  background-color: rgba(0, 80, 0, 160);"
                "  color: #00ff00;"
                "  font-size: 14px;"
                "  padding: 4px 8px;"
                "  border-radius: 4px;"
                "}"
            );
        } else {
            m_statusLabel->setText("RK3588 Monitor | 连接断开");
            m_statusLabel->setStyleSheet(
                "QLabel {"
                "  background-color: rgba(80, 0, 0, 160);"
                "  color: #ff4444;"
                "  font-size: 14px;"
                "  padding: 4px 8px;"
                "  border-radius: 4px;"
                "}"
            );
        }
        m_statusLabel->adjustSize();
    }
}

// ============================================================================
// 绘制辅助方法
// ============================================================================

void MainWindow::drawFaceBoxes(QPainter& painter, const QVector<FaceData>& faces)
{
    // 设置画笔：绿色半透明边框
    QPen pen(QColor(0, 255, 0, 200), 3, Qt::SolidLine);
    painter.setPen(pen);

    // 设置画刷：半透明填充
    painter.setBrush(QColor(0, 255, 0, 30));

    QFont font;
    font.setPixelSize(16);
    font.setBold(true);
    painter.setFont(font);

    for (const FaceData& face : faces) {
        QRect faceRect(face.x, face.y, face.width, face.height);

        // 绘制边界框
        painter.drawRect(faceRect);

        // 绘制标签背景
        QString label = QString("ID:%1 %2%")
            .arg(face.id)
            .arg(static_cast<int>(face.confidence * 100));
        QFontMetrics fm(font);
        int labelWidth = fm.horizontalAdvance(label) + 12;
        int labelHeight = fm.height() + 6;

        QRect labelRect(face.x, face.y - labelHeight, labelWidth, labelHeight);
        painter.fillRect(labelRect, QColor(0, 255, 0, 180));

        // 绘制标签文字
        painter.setPen(QColor(0, 0, 0));
        painter.drawText(labelRect, Qt::AlignCenter, label);

        // 恢复画笔颜色
        painter.setPen(pen);
    }
}

void MainWindow::drawStatusInfo(QPainter& painter)
{
    // 在屏幕右下角绘制FPS和帧信息
    QFont font;
    font.setPixelSize(14);
    font.setBold(true);
    painter.setFont(font);

    QString info = QString("FPS: %1 | 帧序号: %2 | 人脸数: %3")
        .arg(m_currentFps, 0, 'f', 1)
        .arg(m_lastFrameSequence)
        .arg(m_currentFaces.size());

    QFontMetrics fm(font);
    int textWidth = fm.horizontalAdvance(info) + 20;
    int textHeight = fm.height() + 12;

    int x = width() - textWidth - 10;
    int y = height() - textHeight - 10;

    // 绘制背景
    painter.fillRect(x, y, textWidth, textHeight, QColor(0, 0, 0, 160));

    // 绘制文字
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(x, y, textWidth, textHeight, Qt::AlignCenter, info);
}

void MainWindow::updateFps()
{
    m_frameCount++;
    qint64 elapsed = m_fpsTimer.elapsed();

    // 每秒更新一次FPS
    if (elapsed >= 1000) {
        m_currentFps = m_frameCount * 1000.0f / elapsed;
        m_frameCount = 0;
        m_fpsTimer.restart();

        // 更新状态栏
        if (m_statusLabel) {
            bool connected = m_bridge && m_bridge->isInitialized();
            m_statusLabel->setText(
                QString("RK3588 Monitor | FPS: %1 | %2")
                    .arg(m_currentFps, 0, 'f', 1)
                    .arg(connected ? "运行中" : "未连接")
            );
            m_statusLabel->adjustSize();
        }
    }
}
