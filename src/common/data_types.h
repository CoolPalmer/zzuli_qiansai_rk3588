#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <cstdint>

/**
 * @brief 视频帧结构体（与后端进程约定的共享内存布局）
 * 
 * 该结构体通过共享内存传递，包含DMA-BUF文件描述符，
 * 用于DRM/KMS零拷贝视频显示。
 */
struct VideoFrame {
    int fd;                  // DMA-BUF的文件描述符
    uint32_t width;          // 帧宽度（像素）
    uint32_t height;         // 帧高度（像素）
    uint32_t stride;         // 行跨度（字节）
    uint32_t format;         // 像素格式，如 DRM_FORMAT_ABGR8888 (0x34325241)
    uint64_t timestamp_us;   // 时间戳（微秒）
    uint32_t sequence;       // 帧序号，用于检测丢帧
    uint32_t reserved[3];    // 保留字段，用于未来扩展
};

/**
 * @brief 单个人脸检测信息
 */
struct FaceData {
    uint32_t id;             // 人脸跟踪ID
    uint32_t x;              // 边界框左上角X坐标
    uint32_t y;              // 边界框左上角Y坐标
    uint32_t width;          // 边界框宽度
    uint32_t height;         // 边界框高度
    float confidence;        // 置信度 [0.0, 1.0]
    uint32_t reserved[2];    // 保留字段
};

/**
 * @brief 人脸数据数组头（描述一帧中的人脸列表）
 * 
 * 共享内存布局: [FaceDataHeader][FaceData 0][FaceData 1]...[FaceData N-1]
 * 最大支持 MAX_FACES 个人脸
 */
static constexpr uint32_t MAX_FACES = 32;

struct FaceDataHeader {
    uint64_t frame_id;       // 对应的视频帧序号
    uint32_t num_faces;      // 当前帧中检测到的人脸数量
    uint32_t reserved;       // 保留字段，保证8字节对齐
    // FaceData faces[num_faces] 紧随其后
};

/**
 * @brief 共享内存头部结构（视频帧共享内存）
 * 
 * 用于进程间同步，包含一个简单的标志位机制
 */
struct SharedMemHeader {
    uint32_t magic;          // 魔数，用于验证共享内存有效性
    uint32_t version;        // 版本号
    uint64_t write_count;    // 写入计数器，用于检测更新
    uint32_t ready;          // 数据就绪标志 (0: 未就绪, 1: 就绪)
    uint32_t reserved;       // 保留字段
};

static constexpr uint32_t SHM_MAGIC = 0x4D4F4E49;  // "MONI"
static constexpr uint32_t SHM_VERSION = 1;

/**
 * @brief 控制命令类型枚举
 */
enum class ControlCommand : uint32_t {
    NONE = 0,
    SWITCH_SOURCE,           // 切换视频源
    SET_RESOLUTION,          // 设置分辨率
    START_STREAM,            // 开始推流
    STOP_STREAM,             // 停止推流
    CUSTOM = 0x100           // 自定义命令起始值
};

/**
 * @brief 控制命令消息结构（通过Unix Socket传递）
 */
struct ControlMessage {
    uint32_t command;        // ControlCommand枚举值
    uint32_t param_length;   // 参数数据长度
    // char params[param_length] 紧随其后
};

#endif // DATA_TYPES_H
