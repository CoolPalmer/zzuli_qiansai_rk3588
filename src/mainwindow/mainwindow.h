#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QVector>
#include <QElapsedTimer>
#include <QLabel>

#include "../common/data_types.h"

// 前向声明
class DrmDisplay;
class SharedMemBridge;

/**
 * @brief 主窗口类
 * 
 * 继承自QMainWindow，实现以下功能：
 * 1. 协调DrmDisplay和SharedMemBridge两个模块
 * 2. 使用QTimer驱动定时刷新循环
 * 3. 在paintEvent中绘制人脸识别框和状态信息
 * 4. 处理来自后端进程的控制命令
 * 
 * 显示层级（从下到上）：
 * - DRM叠加平面：视频画面（由DrmDisplay管理）
 * - Qt UI层：人脸框、状态栏等（由本类paintEvent绘制）
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    /**
     * @brief 初始化所有子模块
     * @return 成功返回true
     */
    bool initialize();

protected:
    /**
     * @brief 重写绘制事件，绘制人脸框和UI叠加层
     */
    void paintEvent(QPaintEvent* event) override;

    /**
     * @brief 重写按键事件，支持ESC退出
     */
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    /**
     * @brief 定时器刷新槽函数
     * 
     * 每次触发时：
     * 1. 从SharedMemBridge获取最新数据
     * 2. 调用DrmDisplay更新视频帧
     * 3. 请求Qt重绘UI
     */
    void onRefreshTimer();

    /**
     * @brief 处理控制命令
     * @param cmd 命令字符串
     */
    void onControlCommand(const QString& cmd);

    /**
     * @brief Socket连接状态变化处理
     * @param connected 是否已连接
     */
    void onSocketConnectionChanged(bool connected);

private:
    /**
     * @brief 绘制人脸识别框
     * @param painter QPainter引用
     * @param faces 人脸数据列表
     */
    void drawFaceBoxes(QPainter& painter, const QVector<FaceData>& faces);

    /**
     * @brief 绘制状态信息
     * @param painter QPainter引用
     */
    void drawStatusInfo(QPainter& painter);

    /**
     * @brief 更新FPS计算
     */
    void updateFps();

    // 子模块
    DrmDisplay* m_drmDisplay;
    SharedMemBridge* m_bridge;

    // 定时器
    QTimer* m_refreshTimer;

    // 当前帧数据（在主线程中缓存，避免在paintEvent中加锁）
    VideoFrame m_currentFrame;
    QVector<FaceData> m_currentFaces;

    // FPS计算
    QElapsedTimer m_fpsTimer;
    int m_frameCount;
    float m_currentFps;
    uint64_t m_lastFrameSequence;

    // 状态栏控件
    QLabel* m_statusLabel;

    // 配置参数
    QString m_videoShmPath;
    QString m_faceShmPath;
    QString m_socketPath;
    int m_refreshIntervalMs;
};

#endif // MAINWINDOW_H
