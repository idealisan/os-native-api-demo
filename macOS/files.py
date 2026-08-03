import sys
from Cocoa import NSOpenPanel, NSApplication, NSApp, NSModalResponseOK
from UniformTypeIdentifiers import UTType


def select_single_file(allowed_types:[UTType]):
    """
    select a single file by a mac native dialog
    allowed types: [UTType] a list of UTType to restrict selection
    :return: str or None (if canceled)
    """
    # 1. 初始化 macOS 应用程序环境（确保对话框可以正常显示和接收焦点）
    app = NSApplication.sharedApplication()

    # 2. 激活当前应用并将其置于前台，使弹出的对话框不被其他窗口遮挡
    NSApp.activateIgnoringOtherApps_(True)

    # 3. 创建原生的打开面板 (NSOpenPanel)
    panel = NSOpenPanel.openPanel()

    # 4. 配置面板属性
    panel.setCanChooseFiles_(True)  # 允许选择文件
    panel.setCanChooseDirectories_(False)  # 禁止选择文件夹
    panel.setAllowsMultipleSelection_(False)  # 禁止多选

    # 5. 运行对话框并获取用户的操作结果
    # runModal 会阻塞当前线程，直到用户点击“打开”或“取消”
    response = panel.runModal()

    # 6. 判断用户是否点击了“打开” (OK)
    if response == NSModalResponseOK:
        # 获取选中的第一个文件的 URL 路径
        file_url = panel.URLs().firstObject()
        if file_url:
            # 将 NSURL 转换为 Python 字符串路径
            return file_url.path()

    # 如果用户点击“取消”或关闭窗口，返回 None
    return None


# 使用示例
if __name__ == "__main__":
    print("正在打开原生选择窗口...")
    selected_path = select_single_file()

    if selected_path:
        print(f"用户选择的文件路径: {selected_path}")
    else:
        print("用户取消了选择。")
