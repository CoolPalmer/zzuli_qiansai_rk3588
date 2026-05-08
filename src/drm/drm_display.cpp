#include "drm_display.h"
#include "../common/data_types.h"

#include <QDebug>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

// ============================================================================
// 构造与析构
// ============================================================================

DrmDisplay::DrmDisplay()
    : m_drmFd(-1)
    , m_drmResources(nullptr)
    , m_planeResources(nullptr)
    , m_overlayPlaneId(0)
    , m_crtcId(0)
    , m_connectorId(0)
    , m_currentFbId(0)
    , m_currentGemHandle(0)
    , m_screenWidth(0)
    , m_screenHeight(0)
    , m_initialized(false)
{
}

DrmDisplay::~DrmDisplay()
{
    cleanup();
}

// ============================================================================
// 公共接口
// ============================================================================

bool DrmDisplay::initialize()
{
    if (m_initialized) {
        qWarning() << "[DrmDisplay] 已经初始化，跳过重复初始化";
        return true;
    }

    // 1. 打开DRM设备
    // 优先尝试 /dev/dri/card0，RK3588通常使用card0
    const char* devicePaths[] = {
        "/dev/dri/card0",
        "/dev/dri/card1",
        "/dev/dri/card2"
    };

    for (const char* path : devicePaths) {
        m_drmFd = open(path, O_RDWR | O_CLOEXEC);
        if (m_drmFd >= 0) {
            qDebug() << "[DrmDisplay] 成功打开DRM设备:" << path;
            break;
        }
    }

    if (m_drmFd < 0) {
        qWarning() << "[DrmDisplay] 无法打开任何DRM设备:" << strerror(errno);
        return false;
    }

    // 2. 获取DRM资源
    m_drmResources = drmModeGetResources(m_drmFd);
    if (!m_drmResources) {
        qWarning() << "[DrmDisplay] 获取DRM资源失败:" << strerror(errno);
        cleanup();
        return false;
    }

    qDebug() << "[DrmDisplay] DRM资源: "
             << m_drmResources->count_connectors << "个连接器, "
             << m_drmResources->count_crtcs << "个CRTC, "
             << m_drmResources->count_encoders << "个编码器";

    // 3. 查找活跃的connector和CRTC，获取屏幕分辨率
    bool foundConnector = false;
    for (int i = 0; i < m_drmResources->count_connectors; ++i) {
        drmModeConnector* conn = drmModeGetConnector(m_drmFd, m_drmResources->connectors[i]);
        if (!conn) continue;

        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            m_connectorId = conn->connector_id;
            // 使用第一个模式（通常是首选分辨率）
            m_screenWidth = conn->modes[0].hdisplay;
            m_screenHeight = conn->modes[0].vdisplay;
            qDebug() << "[DrmDisplay] 找到活跃连接器 ID:" << m_connectorId
                     << "分辨率:" << m_screenWidth << "x" << m_screenHeight;

            // 查找对应的CRTC
            if (conn->encoder_id) {
                drmModeEncoder* encoder = drmModeGetEncoder(m_drmFd, conn->encoder_id);
                if (encoder) {
                    m_crtcId = encoder->crtc_id;
                    drmModeFreeEncoder(encoder);
                }
            }

            // 如果没有从encoder获取到CRTC，使用第一个可用的
            if (m_crtcId == 0 && m_drmResources->count_crtcs > 0) {
                m_crtcId = m_drmResources->crtcs[0];
            }

            drmModeFreeConnector(conn);
            foundConnector = true;
            break;
        }
        drmModeFreeConnector(conn);
    }

    if (!foundConnector) {
        qWarning() << "[DrmDisplay] 未找到活跃的显示连接器";
        cleanup();
        return false;
    }

    // 4. 获取平面资源并查找叠加平面
    m_planeResources = drmModeGetPlaneResources(m_drmFd);
    if (!m_planeResources) {
        qWarning() << "[DrmDisplay] 获取平面资源失败:" << strerror(errno);
        cleanup();
        return false;
    }

    qDebug() << "[DrmDisplay] 可用平面数量:" << m_planeResources->count_planes;

    if (!findOverlayPlane()) {
        qWarning() << "[DrmDisplay] 未找到可用的叠加平面(Overlay Plane)";
        cleanup();
        return false;
    }

    m_initialized = true;
    qDebug() << "[DrmDisplay] 初始化成功，叠加平面ID:" << m_overlayPlaneId
             << "CRTC ID:" << m_crtcId;
    return true;
}

bool DrmDisplay::updateFrame(const VideoFrame& frame)
{
    if (!m_initialized) {
        qWarning() << "[DrmDisplay] 未初始化，无法更新帧";
        return false;
    }

    if (frame.fd < 0) {
        qWarning() << "[DrmDisplay] 无效的DMA-BUF fd";
        return false;
    }

    // 1. 释放上一帧的资源
    releaseCurrentFrame();

    // 2. 将DMA-BUF fd导入为DRM GEM handle
    struct drm_prime_handle prime = {};
    prime.fd = frame.fd;
    prime.flags = 0;
    prime.handle = 0;

    int ret = drmIoctl(m_drmFd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime);
    if (ret < 0) {
        qWarning() << "[DrmDisplay] 将DMA-BUF fd导入为GEM handle失败:" << strerror(errno);
        return false;
    }
    m_currentGemHandle = prime.handle;

    // 3. 创建framebuffer
    // 使用drmModeAddFB2支持多平面和各种格式
    uint32_t handles[4] = { m_currentGemHandle, 0, 0, 0 };
    uint32_t pitches[4] = { frame.stride, 0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    uint32_t fbId = 0;

    ret = drmModeAddFB2(
        m_drmFd,
        frame.width,
        frame.height,
        frame.format,   // 如 DRM_FORMAT_ABGR8888
        handles,
        pitches,
        offsets,
        &fbId,
        0               // flags
    );

    if (ret < 0) {
        qWarning() << "[DrmDisplay] 创建framebuffer失败:" << strerror(errno)
                     << "格式:" << Qt::hex << frame.format
                     << "尺寸:" << frame.width << "x" << frame.height
                     << "stride:" << frame.stride;
        // 清理已导入的GEM handle
        struct drm_gem_close gemClose = {};
        gemClose.handle = m_currentGemHandle;
        drmIoctl(m_drmFd, DRM_IOCTL_GEM_CLOSE, &gemClose);
        m_currentGemHandle = 0;
        return false;
    }
    m_currentFbId = fbId;

    // 4. 设置到叠加平面
    // src_x, src_y, src_w, src_h 使用16.16定点数格式
    ret = drmModeSetPlane(
        m_drmFd,
        m_overlayPlaneId,
        m_crtcId,
        m_currentFbId,
        0,                                          // flags
        0, 0,                                       // crtc_x, crtc_y (全屏)
        m_screenWidth, m_screenHeight,              // crtc_w, crtc_h
        0, 0,                                       // src_x, src_y (16.16定点)
        frame.width << 16, frame.height << 16       // src_w, src_h (16.16定点)
    );

    if (ret < 0) {
        qWarning() << "[DrmDisplay] 设置叠加平面失败:" << strerror(errno);
        releaseCurrentFrame();
        return false;
    }

    return true;
}

// ============================================================================
// 私有方法
// ============================================================================

bool DrmDisplay::findOverlayPlane()
{
    for (uint32_t i = 0; i < m_planeResources->count_planes; ++i) {
        drmModePlane* plane = drmModeGetPlane(m_drmFd, m_planeResources->planes[i]);
        if (!plane) continue;

        // 获取平面的属性以确定类型
        drmModeObjectProperties* props = drmModeObjectGetProperties(
            m_drmFd, plane->plane_id, DRM_MODE_OBJECT_PLANE
        );

        if (props) {
            for (uint32_t j = 0; j < props->count_props; ++j) {
                drmModePropertyRes* prop = drmModeGetProperty(m_drmFd, props->props[j]);
                if (prop && strcmp(prop->name, "type") == 0) {
                    uint64_t planeType = props->prop_values[j];

                    if (planeType == DRM_PLANE_TYPE_OVERLAY) {
                        // 检查该平面是否支持当前CRTC
                        if (plane->possible_crtcs & (1 << 0)) {
                            m_overlayPlaneId = plane->plane_id;
                            qDebug() << "[DrmDisplay] 找到叠加平面 ID:" << m_overlayPlaneId
                                     << "(平面索引:" << i << ")";
                            drmModeFreeProperty(prop);
                            drmModeFreeObjectProperties(props);
                            drmModeFreePlane(plane);
                            return true;
                        }
                    }
                    drmModeFreeProperty(prop);
                }
            }
            drmModeFreeObjectProperties(props);
        }
        drmModeFreePlane(plane);
    }

    return false;
}

void DrmDisplay::releaseCurrentFrame()
{
    if (m_currentFbId > 0) {
        drmModeRmFB(m_drmFd, m_currentFbId);
        m_currentFbId = 0;
    }

    if (m_currentGemHandle > 0) {
        struct drm_gem_close gemClose = {};
        gemClose.handle = m_currentGemHandle;
        drmIoctl(m_drmFd, DRM_IOCTL_GEM_CLOSE, &gemClose);
        m_currentGemHandle = 0;
    }
}

void DrmDisplay::cleanup()
{
    // 释放当前帧资源
    releaseCurrentFrame();

    // 释放平面资源
    if (m_planeResources) {
        drmModeFreePlaneResources(m_planeResources);
        m_planeResources = nullptr;
    }

    // 释放DRM资源
    if (m_drmResources) {
        drmModeFreeResources(m_drmResources);
        m_drmResources = nullptr;
    }

    // 关闭DRM设备
    if (m_drmFd >= 0) {
        close(m_drmFd);
        m_drmFd = -1;
    }

    m_initialized = false;
    m_overlayPlaneId = 0;
    m_crtcId = 0;
    m_connectorId = 0;
    m_screenWidth = 0;
    m_screenHeight = 0;

    qDebug() << "[DrmDisplay] 资源已释放";
}
