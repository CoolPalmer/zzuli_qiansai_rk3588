cmake_minimum_required(VERSION 3.16)

project(rk3588_qt_monitor
    VERSION 1.0.0
    DESCRIPTION "RK3588嵌入式智能监控显示系统"
    LANGUAGES CXX
)

# ============================================================================
# 编译器设置
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 启用Qt自动MOC处理（Q_OBJECT宏需要）
set(CMAKE_AUTOMOC ON)

# 交叉编译时可能需要设置sysroot
# set(CMAKE_SYSROOT /path/to/sysroot)

# ============================================================================
# 查找依赖
# ============================================================================

# 查找Qt5
find_package(Qt5 REQUIRED COMPONENTS
    Core
    Widgets
)

# 查找libdrm（通过pkg-config）
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED libdrm)

# ============================================================================
# 源文件定义
# ============================================================================
set(SOURCES
    src/main.cpp
    src/mainwindow/mainwindow.cpp
    src/drm/drm_display.cpp
    src/bridge/shared_mem_bridge.cpp
)

set(HEADERS
    src/common/data_types.h
    src/mainwindow/mainwindow.h
    src/drm/drm_display.h
    src/bridge/shared_mem_bridge.h
)

# ============================================================================
# 创建可执行目标
# ============================================================================
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# ============================================================================
# 包含目录
# ============================================================================
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LIBDRM_INCLUDE_DIRS}
)

# ============================================================================
# 链接库
# ============================================================================
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Widgets
    ${LIBDRM_LIBRARIES}
    # rt: POSIX共享内存 (shm_open等)
    # pthread: 线程支持
    rt
    pthread
)

# ============================================================================
# 编译选项
# ============================================================================
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    # RK3588是ARM64架构
    # 交叉编译时通过toolchain文件设置
)

# ============================================================================
# 安装规则
# ============================================================================
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# 安装配置文件示例
install(FILES config/app_config.json.example
    DESTINATION share/${PROJECT_NAME}/config
)

# 安装启动脚本
install(PROGRAMS scripts/run.sh.example
    DESTINATION share/${PROJECT_NAME}/scripts
)

# ============================================================================
# 构建信息输出
# ============================================================================
message(STATUS "========================================")
message(STATUS "项目: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "C++标准: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Qt版本: ${Qt5_VERSION}")
message(STATUS "libdrm包含目录: ${LIBDRM_INCLUDE_DIRS}")
message(STATUS "libdrm库: ${LIBDRM_LIBRARIES}")
message(STATUS "========================================")
