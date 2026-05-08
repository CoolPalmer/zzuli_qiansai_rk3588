# RK3588嵌入式智能监控显示系统

基于Qt QWidget + DRM/KMS的嵌入式智能监控显示系统，运行在RK3588嵌入式Linux板上。

## 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                        屏幕显示                              │
│  ┌───────────────────────────────────────────────────────┐  │
│  │              Qt UI层 (QWidget/QPainter)                │  │
│  │         人脸识别框、状态栏、FPS信息                     │  │
│  └───────────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────┐  │
│  │           DRM叠加平面 (Overlay Plane)                  │  │
│  │              视频画面 (零拷贝显示)                      │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘

┌──────────────┐    共享内存A     ┌──────────────┐    DRM/KMS    ┌──────────┐
│              │ ──────────────→ │  DrmDisplay  │ ───────────→ │  叠加平面  │
│   后端进程    │                 └──────────────┘              └──────────┘
│              │    共享内存B     ┌──────────────────┐  paintEvent  ┌──────┐
│              │ ──────────────→ │ SharedMemBridge  │ ──────────→ │ UI层 │
│              │                 └──────────────────┘              └──────┘
│              │    Unix Socket   ┌──────────────────┐  信号/槽
│              │ ──────────────→ │ SharedMemBridge  │ ──────────→ MainWindow
└──────────────┘                 └──────────────────┘
```

## 模块说明

| 模块 | 文件 | 职责 |
|------|------|------|
| **DrmDisplay** | `src/drm/drm_display.h/cpp` | 封装DRM/KMS操作，管理视频叠加平面，实现DMA-BUF零拷贝显示 |
| **SharedMemBridge** | `src/bridge/shared_mem_bridge.h/cpp` | 读取共享内存中的视频帧和人脸数据；实现Unix Socket IPC客户端 |
| **MainWindow** | `src/mainwindow/mainwindow.h/cpp` | 主界面；协调各模块，定时刷新并绘制UI（人脸框、状态信息） |
| **数据结构** | `src/common/data_types.h` | 跨模块共享的数据结构定义 |

## 文件结构

```
rk3588_qt_monitor/
├── CMakeLists.txt                      # CMake构建配置
├── README.md                           # 本文件
├── src/
│   ├── main.cpp                        # 应用程序入口
│   ├── mainwindow/
│   │   ├── mainwindow.h                # 主窗口头文件
│   │   └── mainwindow.cpp              # 主窗口实现
│   ├── drm/
│   │   ├── drm_display.h               # DRM显示管理器头文件
│   │   └── drm_display.cpp             # DRM显示管理器实现
│   ├── bridge/
│   │   ├── shared_mem_bridge.h         # 共享内存桥接头文件
│   │   └── shared_mem_bridge.cpp       # 共享内存桥接实现
│   └── common/
│       └── data_types.h                # 公共数据结构定义
├── config/
│   └── app_config.json.example         # 配置文件示例
└── scripts/
    └── run.sh.example                  # 启动脚本示例
```

## 环境要求

### 硬件
- RK3588开发板（或兼容的ARM64 Linux设备）
- 支持DRM/KMS的显示输出

### 软件
- **操作系统**: Linux (ARM64/aarch64)
- **编译工具链**: 
  - CMake 3.16+
  - GCC/G++ (支持C++17)
  - 交叉编译工具链（如aarch64-linux-gnu-g++）
- **依赖库**:
  - Qt 5.x (Core, Widgets)
  - libdrm (DRM/KMS用户空间库)
  - POSIX共享内存支持 (librt)
  - OpenGL ES 2.0 (EGLFS平台插件需要)

### 安装依赖（在目标板上）

```bash
# Debian/Ubuntu系
sudo apt-get install libdrm-dev qt5-default libgles2-mesa-dev

# 或使用交叉编译sysroot中的库
```

## 编译方法

### 本地编译（在RK3588板上直接编译）

```bash
cd rk3588_qt_monitor
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 交叉编译（在x86主机上为ARM64编译）

1. **准备交叉编译工具链文件** `toolchain-aarch64.cmake`:

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_SYSROOT /path/to/your/sysroot)
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

2. **执行交叉编译**:

```bash
cd rk3588_qt_monitor
mkdir build && cd build
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=../toolchain-aarch64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/opt/rk3588_monitor
make -j$(nproc)
```

## 部署与运行

### 部署到目标板

```bash
# 复制可执行文件
scp build/rk3588_qt_monitor root@rk3588:/opt/rk3588_monitor/

# 复制配置文件
scp config/app_config.json.example root@rk3588:/opt/rk3588_monitor/config/app_config.json

# 复制启动脚本
scp scripts/run.sh.example root@rk3588:/opt/rk3588_monitor/scripts/run.sh
ssh root@rk3588 "chmod +x /opt/rk3588_monitor/scripts/run.sh"
```

### 运行

```bash
# 方式1: 使用启动脚本（推荐）
cd /opt/rk3588_monitor
./scripts/run.sh

# 方式2: 直接运行
export QT_QPA_PLATFORM=eglfs
export QT_QPA_EGLFS_INTEGRATION=eglfs_kms
export QT_QPA_EGLFS_HIDECURSOR=1
./rk3588_qt_monitor

# 方式3: 使用systemd服务（生产环境）
# 参考下方systemd配置
```

### systemd服务配置（可选）

创建 `/etc/systemd/system/rk3588-monitor.service`:

```ini
[Unit]
Description=RK3588智能监控显示系统
After=network.target

[Service]
Type=simple
User=root
Environment=QT_QPA_PLATFORM=eglfs
Environment=QT_QPA_EGLFS_INTEGRATION=eglfs_kms
Environment=QT_QPA_EGLFS_HIDECURSOR=1
ExecStart=/opt/rk3588_monitor/rk3588_qt_monitor
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable rk3588-monitor
sudo systemctl start rk3588-monitor
```

## 后端进程接口规范

### 共享内存

#### 视频帧共享内存 (`/dev/shm/video_frame_shm`)

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0 | 4B | magic | 魔数: 0x4D4F4E49 ("MONI") |
| 4 | 4B | version | 版本号: 1 |
| 8 | 8B | write_count | 写入计数器 |
| 16 | 4B | ready | 数据就绪标志 (0/1) |
| 20 | 4B | reserved | 保留 |
| 24 | 4B | fd | DMA-BUF文件描述符 |
| 28 | 4B | width | 帧宽度 |
| 32 | 4B | height | 帧高度 |
| 36 | 4B | stride | 行跨度 |
| 40 | 4B | format | 像素格式 (DRM_FORMAT_*) |
| 44 | 8B | timestamp_us | 时间戳(微秒) |
| 52 | 4B | sequence | 帧序号 |

#### 人脸数据共享内存 (`/dev/shm/face_data_shm`)

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0 | 20B | SharedMemHeader | 共享内存头部 |
| 20 | 16B | FaceDataHeader | 人脸数据头部 |
| 36 | 28B × N | FaceData[N] | 人脸数据数组 |

### Unix Socket控制命令

连接路径: `/tmp/monitor_control.sock` (SOCK_STREAM)

| 命令值 | 名称 | 参数 | 说明 |
|--------|------|------|------|
| 1 | SWITCH_SOURCE | 字符串(视频源标识) | 切换视频源 |
| 2 | SET_RESOLUTION | 字符串("1920x1080") | 设置分辨率 |
| 3 | START_STREAM | 无 | 开始推流 |
| 4 | STOP_STREAM | 无 | 停止推流 |

消息格式:
```
[4字节: command][4字节: param_length][param_length字节: params]
```

## 常见问题

### Q: 启动后黑屏
A: 检查以下几点：
1. 确认DRM设备存在: `ls -la /dev/dri/`
2. 确认有叠加平面可用: `modetest -M rockchip -p`
3. 确认用户有DRM访问权限: `groups` 应包含 `video`

### Q: 编译时找不到libdrm
A: 安装libdrm开发包: `sudo apt-get install libdrm-dev`
或在交叉编译sysroot中确保libdrm已安装。

### Q: Qt报错 "Could not find the platform plugin"
A: 确保Qt EGLFS插件已安装:
```bash
# 查找eglfs插件
find / -name "libqeglfs.so" 2>/dev/null
# 确保QT_QPA_PLATFORM_PLUGIN_PATH指向正确目录
```

### Q: 共享内存未创建
A: 后端进程需要先创建共享内存。检查:
```bash
ls -la /dev/shm/
```

## 许可证

本项目仅供学习和内部使用。
