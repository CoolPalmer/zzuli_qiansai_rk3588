set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# ============================================
# 编译器（用软链接，不带版本号）
# ============================================
set(CMAKE_C_COMPILER   /workspace/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/aarch64-buildroot-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER /workspace/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/aarch64-buildroot-linux-gnu-g++)

# ============================================
# sysroot（头文件和库都在这里面）
# ============================================
set(CMAKE_SYSROOT /workspace/aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot)

# ============================================
# 让 CMake 只在 sysroot 里找库/头文件/包配置，
# 避免找到你电脑（x86）自带的库
# ============================================
set(CMAKE_FIND_ROOT_PATH )
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ============================================
# 显式指定 Qt5 配置路径（确保能找到 Qt5Config.cmake）
# ============================================
list(APPEND CMAKE_PREFIX_PATH )

# ============================================
# 关键：配置 pkg-config 交叉编译环境
# 防止它找到你电脑 x86 的 libdrm，强制去 sysroot 里找 RK3588 的
# ============================================
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")

