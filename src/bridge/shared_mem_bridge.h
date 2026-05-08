#ifndef SHARED_MEM_BRIDGE_H
#define SHARED_MEM_BRIDGE_H

#include <QObject>
#include <QVector>
#include <QMutex>
#include <QThread>
#include <QAtomicInt>

#include "../common/data_types.h"

// 前向声明
struct VideoFrame;

/**
 * @brief 共享内存与IPC桥接模块
 * 
 * 负责：
 * 1. 映射两块POSIX共享内存（视频帧、人脸数据）
 * 2. 提供线程安全的数据读取接口
 * 3. 实现Unix Domain Socket客户端，异步接收控制命令
 * 
 * 共享内存布局：
 * - 视频帧共享内存: [SharedMemHeader][VideoFrame]
 * - 人脸数据共享内存: [SharedMemHeader][FaceDataHeader][FaceData 0]...[FaceData N-1]
 */
class SharedMemBridge : public QObject
{
    Q_OBJECT

public:
    explicit SharedMemBridge(QObject* parent = nullptr);
    ~SharedMemBridge();

    // 禁止拷贝和赋值
    SharedMemBridge(const SharedMemBridge&) = delete;
    SharedMemBridge& operator=(const SharedMemBridge&) = delete;

    /**
     * @brief 初始化共享内存映射和Socket连接
     * @param videoShmPath 视频帧共享内存路径 (默认: /dev/shm/video_frame_shm)
     * @param faceShmPath 人脸数据共享内存路径 (默认: /dev/shm/face_data_shm)
     * @param socketPath Unix Socket路径 (默认: /tmp/monitor_control.sock)
     * @return 成功返回true
     */
    bool initialize(const QString& videoShmPath = "/dev/shm/video_frame_shm",
                    const QString& faceShmPath = "/dev/shm/face_data_shm",
                    const QString& socketPath = "/tmp/monitor_control.sock");

    /**
     * @brief 获取最新的视频帧数据（线程安全）
     * @return VideoFrame结构体副本
     */
    VideoFrame getCurrentVideoFrame() const;

    /**
     * @brief 获取最新的人脸数据列表（线程安全）
     * @return FaceData向量副本
     */
    QVector<FaceData> getCurrentFaces() const;

    /**
     * @brief 检查是否已初始化
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief 获取视频帧共享内存的写入计数（用于检测更新）
     */
    uint64_t getVideoWriteCount() const;

signals:
    /**
     * @brief 收到控制命令时发出
     * @param cmd 命令字符串
     */
    void controlCommandReceived(const QString& cmd);

    /**
     * @brief Socket连接状态变化
     * @param connected 是否已连接
     */
    void socketConnectionChanged(bool connected);

private:
    /**
     * @brief 映射POSIX共享内存
     * @param path 共享内存路径
     * @param size 映射大小（输出参数）
     * @return 映射后的内存指针，失败返回nullptr
     */
    void* mapSharedMemory(const QString& path, size_t& size);

    /**
     * @brief 取消共享内存映射
     */
    void unmapSharedMemory(void*& ptr, size_t size);

    /**
     * @brief 验证共享内存头部有效性
     */
    bool validateShmHeader(void* ptr) const;

    /**
     * @brief Socket接收线程函数
     */
    void socketReceiveLoop();

    /**
     * @brief 连接到Unix Socket服务器
     * @return 成功返回true
     */
    bool connectToSocket();

    // 视频帧共享内存
    void* m_videoShmPtr;
    size_t m_videoShmSize;
    QString m_videoShmPath;

    // 人脸数据共享内存
    void* m_faceShmPtr;
    size_t m_faceShmSize;
    QString m_faceShmPath;

    // Unix Socket
    int m_socketFd;
    QString m_socketPath;
    QAtomicInt m_socketConnected;

    // 数据缓冲区（线程安全读取）
    mutable QMutex m_videoMutex;
    VideoFrame m_currentVideoFrame;

    mutable QMutex m_faceMutex;
    QVector<FaceData> m_currentFaces;

    // Socket接收线程
    QThread* m_socketThread;
    QAtomicInt m_running;

    // 初始化状态
    bool m_initialized;
};

#endif // SHARED_MEM_BRIDGE_H
