#include "shared_mem_bridge.h"

#include <QDebug>
#include <QTimer>
#include <QCoreApplication>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <poll.h>

// ============================================================================
// 构造与析构
// ============================================================================

SharedMemBridge::SharedMemBridge(QObject* parent)
    : QObject(parent)
    , m_videoShmPtr(nullptr)
    , m_videoShmSize(0)
    , m_faceShmPtr(nullptr)
    , m_faceShmSize(0)
    , m_socketFd(-1)
    , m_socketConnected(0)
    , m_socketThread(nullptr)
    , m_running(0)
    , m_initialized(false)
{
    // 初始化默认视频帧
    memset(&m_currentVideoFrame, 0, sizeof(VideoFrame));
    m_currentVideoFrame.fd = -1;
}

SharedMemBridge::~SharedMemBridge()
{
    // 停止Socket接收线程
    m_running.storeRelaxed(0);

    if (m_socketThread) {
        m_socketThread->quit();
        m_socketThread->wait(3000);
        delete m_socketThread;
        m_socketThread = nullptr;
    }

    // 关闭Socket
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }

    // 取消共享内存映射
    unmapSharedMemory(m_videoShmPtr, m_videoShmSize);
    unmapSharedMemory(m_faceShmPtr, m_faceShmSize);

    qDebug() << "[SharedMemBridge] 资源已释放";
}

// ============================================================================
// 公共接口
// ============================================================================

bool SharedMemBridge::initialize(const QString& videoShmPath,
                                  const QString& faceShmPath,
                                  const QString& socketPath)
{
    if (m_initialized) {
        qWarning() << "[SharedMemBridge] 已经初始化，跳过重复初始化";
        return true;
    }

    m_videoShmPath = videoShmPath;
    m_faceShmPath = faceShmPath;
    m_socketPath = socketPath;

    // 1. 映射视频帧共享内存
    m_videoShmPtr = mapSharedMemory(m_videoShmPath, m_videoShmSize);
    if (!m_videoShmPtr) {
        qWarning() << "[SharedMemBridge] 映射视频帧共享内存失败:" << m_videoShmPath;
        // 共享内存可能尚未创建，不视为致命错误，继续初始化
    } else {
        qDebug() << "[SharedMemBridge] 视频帧共享内存已映射，大小:" << m_videoShmSize;
    }

    // 2. 映射人脸数据共享内存
    m_faceShmPtr = mapSharedMemory(m_faceShmPath, m_faceShmSize);
    if (!m_faceShmPtr) {
        qWarning() << "[SharedMemBridge] 映射人脸数据共享内存失败:" << m_faceShmPath;
        // 同上，不视为致命错误
    } else {
        qDebug() << "[SharedMemBridge] 人脸数据共享内存已映射，大小:" << m_faceShmSize;
    }

    // 3. 启动Socket接收线程
    m_running.storeRelaxed(1);
    m_socketThread = new QThread(this);
    // 使用moveToThread方式在子线程中运行socket接收循环
    QObject::connect(m_socketThread, &QThread::started, this, [this]() {
        socketReceiveLoop();
    });
    m_socketThread->start();

    m_initialized = true;
    qDebug() << "[SharedMemBridge] 初始化完成";
    return true;
}

VideoFrame SharedMemBridge::getCurrentVideoFrame() const
{
    QMutexLocker locker(&m_videoMutex);
    return m_currentVideoFrame;
}

QVector<FaceData> SharedMemBridge::getCurrentFaces() const
{
    QMutexLocker locker(&m_faceMutex);
    return m_currentFaces;
}

uint64_t SharedMemBridge::getVideoWriteCount() const
{
    if (!m_videoShmPtr) return 0;

    // 读取共享内存头部的写入计数
    const SharedMemHeader* header = static_cast<const SharedMemHeader*>(m_videoShmPtr);
    return header->write_count;
}

// ============================================================================
// 私有方法
// ============================================================================

void* SharedMemBridge::mapSharedMemory(const QString& path, size_t& size)
{
    // 打开POSIX共享内存对象
    QByteArray pathBytes = path.toLocal8Bit();
    int fd = shm_open(pathBytes.constData(), O_RDONLY, 0);
    if (fd < 0) {
        qWarning() << "[SharedMemBridge] 打开共享内存失败:" << path << "错误:" << strerror(errno);
        return nullptr;
    }

    // 获取共享内存大小
    struct stat st;
    if (fstat(fd, &st) < 0) {
        qWarning() << "[SharedMemBridge] 获取共享内存大小失败:" << strerror(errno);
        ::close(fd);
        return nullptr;
    }
    size = static_cast<size_t>(st.st_size);

    if (size == 0) {
        qWarning() << "[SharedMemBridge] 共享内存大小为0:" << path;
        ::close(fd);
        return nullptr;
    }

    // 映射到进程地址空间（只读）
    void* ptr = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);  // 映射后可以关闭fd

    if (ptr == MAP_FAILED) {
        qWarning() << "[SharedMemBridge] mmap失败:" << path << "错误:" << strerror(errno);
        return nullptr;
    }

    // 建议内核该区域将被顺序访问
    madvise(ptr, size, MADV_SEQUENTIAL);

    return ptr;
}

void SharedMemBridge::unmapSharedMemory(void*& ptr, size_t size)
{
    if (ptr && ptr != MAP_FAILED) {
        munmap(ptr, size);
        ptr = nullptr;
        size = 0;
    }
}

bool SharedMemBridge::validateShmHeader(void* ptr) const
{
    if (!ptr) return false;

    const SharedMemHeader* header = static_cast<const SharedMemHeader*>(ptr);
    return (header->magic == SHM_MAGIC && header->version == SHM_VERSION);
}

void SharedMemBridge::socketReceiveLoop()
{
    qDebug() << "[SharedMemBridge] Socket接收线程启动";

    while (m_running.loadRelaxed()) {
        // 尝试连接Socket
        if (m_socketFd < 0) {
            if (!connectToSocket()) {
                // 连接失败，等待后重试
                QThread::msleep(1000);
                continue;
            }
        }

        // 使用poll等待数据，带超时以便检查m_running
        struct pollfd pfd;
        pfd.fd = m_socketFd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, 500);  // 500ms超时
        if (ret < 0) {
            if (errno == EINTR) continue;
            qWarning() << "[SharedMemBridge] poll错误:" << strerror(errno);
            ::close(m_socketFd);
            m_socketFd = -1;
            m_socketConnected.storeRelaxed(0);
            emit socketConnectionChanged(false);
            continue;
        }

        if (ret == 0) {
            // 超时，继续循环
            continue;
        }

        if (pfd.revents & (POLLHUP | POLLERR)) {
            qWarning() << "[SharedMemBridge] Socket连接断开";
            ::close(m_socketFd);
            m_socketFd = -1;
            m_socketConnected.storeRelaxed(0);
            emit socketConnectionChanged(false);
            continue;
        }

        if (pfd.revents & POLLIN) {
            // 读取消息头
            ControlMessage msgHeader;
            ssize_t bytesRead = recv(m_socketFd, &msgHeader, sizeof(ControlMessage), MSG_WAITALL);

            if (bytesRead <= 0) {
                if (bytesRead == 0) {
                    qWarning() << "[SharedMemBridge] Socket服务器关闭连接";
                } else {
                    qWarning() << "[SharedMemBridge] recv错误:" << strerror(errno);
                }
                ::close(m_socketFd);
                m_socketFd = -1;
                m_socketConnected.storeRelaxed(0);
                emit socketConnectionChanged(false);
                continue;
            }

            if (bytesRead != sizeof(ControlMessage)) {
                qWarning() << "[SharedMemBridge] 收到不完整的消息头:" << bytesRead << "字节";
                continue;
            }

            // 读取参数数据
            QByteArray params;
            if (msgHeader.param_length > 0) {
                if (msgHeader.param_length > 4096) {
                    // 防止恶意超大参数
                    qWarning() << "[SharedMemBridge] 参数长度异常:" << msgHeader.param_length;
                    continue;
                }
                params.resize(msgHeader.param_length);
                bytesRead = recv(m_socketFd, params.data(), msgHeader.param_length, MSG_WAITALL);
                if (bytesRead != static_cast<ssize_t>(msgHeader.param_length)) {
                    qWarning() << "[SharedMemBridge] 读取参数数据不完整";
                    continue;
                }
            }

            // 解析命令并发出信号
            QString cmdStr;
            switch (static_cast<ControlCommand>(msgHeader.command)) {
            case ControlCommand::SWITCH_SOURCE:
                cmdStr = "SWITCH_SOURCE";
                if (!params.isEmpty()) {
                    cmdStr += ":" + QString::fromUtf8(params);
                }
                break;
            case ControlCommand::SET_RESOLUTION:
                cmdStr = "SET_RESOLUTION";
                if (!params.isEmpty()) {
                    cmdStr += ":" + QString::fromUtf8(params);
                }
                break;
            case ControlCommand::START_STREAM:
                cmdStr = "START_STREAM";
                break;
            case ControlCommand::STOP_STREAM:
                cmdStr = "STOP_STREAM";
                break;
            default:
                cmdStr = QString("UNKNOWN:%1").arg(msgHeader.command);
                if (!params.isEmpty()) {
                    cmdStr += ":" + QString::fromUtf8(params);
                }
                break;
            }

            qDebug() << "[SharedMemBridge] 收到控制命令:" << cmdStr;
            emit controlCommandReceived(cmdStr);
        }

        // 同时尝试从共享内存读取最新数据
        // 视频帧数据
        if (m_videoShmPtr && validateShmHeader(m_videoShmPtr)) {
            const SharedMemHeader* header = static_cast<const SharedMemHeader*>(m_videoShmPtr);
            if (header->ready) {
                const VideoFrame* framePtr = reinterpret_cast<const VideoFrame*>(
                    static_cast<const char*>(m_videoShmPtr) + sizeof(SharedMemHeader)
                );
                QMutexLocker locker(&m_videoMutex);
                m_currentVideoFrame = *framePtr;
            }
        }

        // 人脸数据
        if (m_faceShmPtr && validateShmHeader(m_faceShmPtr)) {
            const SharedMemHeader* header = static_cast<const SharedMemHeader*>(m_faceShmPtr);
            if (header->ready) {
                const FaceDataHeader* faceHeader = reinterpret_cast<const FaceDataHeader*>(
                    static_cast<const char*>(m_faceShmPtr) + sizeof(SharedMemHeader)
                );

                uint32_t numFaces = faceHeader->num_faces;
                if (numFaces > MAX_FACES) {
                    numFaces = MAX_FACES;
                }

                const FaceData* facesPtr = reinterpret_cast<const FaceData*>(
                    reinterpret_cast<const char*>(faceHeader) + sizeof(FaceDataHeader)
                );

                QMutexLocker locker(&m_faceMutex);
                m_currentFaces.resize(numFaces);
                for (uint32_t i = 0; i < numFaces; ++i) {
                    m_currentFaces[i] = facesPtr[i];
                }
            }
        }
    }

    qDebug() << "[SharedMemBridge] Socket接收线程退出";
}

bool SharedMemBridge::connectToSocket()
{
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }

    m_socketFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_socketFd < 0) {
        qWarning() << "[SharedMemBridge] 创建Socket失败:" << strerror(errno);
        return false;
    }

    // 设置接收超时
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(m_socketFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    QByteArray pathBytes = m_socketPath.toLocal8Bit();
    strncpy(addr.sun_path, pathBytes.constData(), sizeof(addr.sun_path) - 1);

    int ret = ::connect(m_socketFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (ret < 0) {
        // 连接失败是正常的（服务器可能尚未启动），不输出警告
        ::close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    m_socketConnected.storeRelaxed(1);
    emit socketConnectionChanged(true);
    qDebug() << "[SharedMemBridge] 已连接到Socket服务器:" << m_socketPath;
    return true;
}
