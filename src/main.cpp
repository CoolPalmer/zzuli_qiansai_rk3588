#include <QApplication>
#include <QDebug>
#include <QSurfaceFormat>
#include <QScreen>

#include "mainwindow/mainwindow.h"

/**
 * @brief 设置Qt运行环境
 * 
 * 在QApplication构造之前设置必要的环境变量，
 * 确保Qt使用EGLFS平台插件在无桌面环境下运行。
 */
static void setupEnvironment()
{
    // 设置EGLFS平台插件
    // 在嵌入式Linux下，EGLFS允许Qt直接使用EGL/OpenGL ES渲染，
    // 无需X11或Wayland桌面环境
    qputenv("QT_QPA_PLATFORM", "eglfs");

    // 设置EGLFS配置（可选）
    // 指定DRM设备路径
    qputenv("QT_QPA_EGLFS_INTEGRATION", "eglfs_kms");

    // 禁用鼠标光标（嵌入式触摸屏场景）
    qputenv("QT_QPA_EGLFS_HIDECURSOR", "1");

    // 设置屏幕物理尺寸（可选，影响DPI计算）
    // qputenv("QT_QPA_EGLFS_PHYSICAL_WIDTH", "1080");
    // qputenv("QT_QPA_EGLFS_PHYSICAL_HEIGHT", "1920");

    // 设置OpenGL ES版本
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGLES);
    format.setVersion(2, 0);
    QSurfaceFormat::setDefaultFormat(format);

    qDebug() << "[main] 环境变量已设置";
}

/**
 * @brief 应用程序入口点
 * 
 * 初始化Qt应用程序，创建并显示主窗口，进入事件循环。
 * 
 * 使用流程：
 * 1. 设置EGLFS环境变量
 * 2. 创建QApplication
 * 3. 创建并初始化MainWindow
 * 4. 进入事件循环
 * 
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return 应用程序退出码
 */
int main(int argc, char* argv[])
{
    // 1. 设置环境变量（必须在QApplication构造之前）
    setupEnvironment();

    // 2. 创建Qt应用程序
    QApplication app(argc, argv);
    app.setApplicationName("RK3588 Monitor");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("EmbeddedAI");

    qDebug() << "[main] Qt应用程序已创建";
    qDebug() << "[main] Qt版本:" << QT_VERSION_STR;
    qDebug() << "[main] 平台插件:" << app.platformName();

    // 打印屏幕信息
    QScreen* screen = app.primaryScreen();
    if (screen) {
        qDebug() << "[main] 屏幕尺寸:" << screen->size()
                 << "DPI:" << screen->logicalDotsPerInch();
    }

    // 3. 创建主窗口
    MainWindow mainWindow;

    // 4. 初始化主窗口（包括DRM和共享内存）
    if (!mainWindow.initialize()) {
        qWarning() << "[main] 主窗口初始化失败！";
        // 即使初始化失败也继续运行，部分功能可能不可用
    }

    qDebug() << "[main] 进入事件循环...";

    // 5. 进入事件循环
    int result = app.exec();

    qDebug() << "[main] 应用程序退出，返回码:" << result;
    return result;
}
