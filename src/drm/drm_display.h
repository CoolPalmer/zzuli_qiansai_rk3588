#ifndef DRM_DISPLAY_H
#define DRM_DISPLAY_H

#include <cstdint>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

// 前向声明，避免在头文件中包含data_types.h
struct VideoFrame;

/**
 * @brief DRM显示管理器
 * 
 * 封装DRM/KMS操作，管理视频叠加平面（Overlay Plane）。
 * 负责将DMA-BUF文件描述符导入为DRM GEM对象，创建framebuffer，
 * 并将其设置到预留的叠加平面上实现零拷贝视频显示。
 * 
 * 使用流程：
 * 1. 构造 DrmDisplay 对象
 * 2. 调用 initialize() 初始化DRM设备和叠加平面
 * 3. 循环调用 updateFrame() 更新视频帧
 * 4. 析构时自动释放所有DRM资源
 */
class DrmDisplay {
public:
    DrmDisplay();
    ~DrmDisplay();

    // 禁止拷贝和赋值
    DrmDisplay(const DrmDisplay&) = delete;
    DrmDisplay& operator=(const DrmDisplay&) = delete;

    /**
     * @brief 初始化DRM设备和叠加平面
     * @return 成功返回true，失败返回false
     * 
     * 执行以下操作：
     * 1. 打开 /dev/dri/card0 设备
     * 2. 获取DRM资源（connectors, CRTCs, planes）
     * 3. 查找一个可用的 DRM_PLANE_TYPE_OVERLAY 类型平面
     * 4. 保存屏幕分辨率信息
     */
    bool initialize();

    /**
     * @brief 更新一帧视频到叠加平面
     * @param frame 视频帧结构体，包含DMA-BUF fd等信息
     * @return 成功返回true，失败返回false
     * 
     * 执行以下操作：
     * 1. 释放上一帧的framebuffer和GEM handle（如果有）
     * 2. 将DMA-BUF fd导入为DRM GEM handle
     * 3. 使用drmModeAddFB2创建framebuffer
     * 4. 调用drmModeSetPlane将framebuffer设置到叠加平面
     */
    bool updateFrame(const VideoFrame& frame);

    /**
     * @brief 获取屏幕宽度
     * @return 屏幕宽度（像素），未初始化时返回0
     */
    int screenWidth() const { return m_screenWidth; }

    /**
     * @brief 获取屏幕高度
     * @return 屏幕高度（像素），未初始化时返回0
     */
    int screenHeight() const { return m_screenHeight; }

    /**
     * @brief 检查DRM设备是否已成功初始化
     * @return 已初始化返回true
     */
    bool isInitialized() const { return m_initialized; }

private:
    /**
     * @brief 查找可用的叠加平面
     * @return 成功找到返回true
     */
    bool findOverlayPlane();

    /**
     * @brief 释放当前帧的DRM资源（framebuffer和GEM handle）
     */
    void releaseCurrentFrame();

    /**
     * @brief 释放所有DRM资源
     */
    void cleanup();

    // DRM设备文件描述符
    int m_drmFd;

    // DRM资源
    drmModeRes* m_drmResources;
    drmModePlaneRes* m_planeResources;

    // 叠加平面信息
    uint32_t m_overlayPlaneId;
    uint32_t m_crtcId;
    uint32_t m_connectorId;

    // 当前帧的DRM对象
    uint32_t m_currentFbId;       // 当前framebuffer ID
    uint32_t m_currentGemHandle;  // 当前GEM handle

    // 屏幕分辨率
    int m_screenWidth;
    int m_screenHeight;

    // 初始化状态
    bool m_initialized;
};

#endif // DRM_DISPLAY_H
