import Quartz
import Vision
from Cocoa import NSURL
from Foundation import NSDictionary

def macos_ocr(image_path: str):
    # 1. 将文件路径转换为 macOS 的 NSURL
    image_url = NSURL.fileURLWithPath_(image_path)

    # 2. 创建图像请求处理器 (VNImageRequestHandler)
    options = NSDictionary.dictionary()
    handler = Vision.VNImageRequestHandler.alloc().initWithURL_options_(image_url, options)

    # 定义存储结果的变量
    ocr_results = []

    # 3. 定义识别完成后的回调函数
    def completion_handler(request, error):
        if error:
            print(f"OCR 出错: {error}")
            return
        # 获取识别到的文本观测对象
        observations = request.results()
        for observation in observations:
            # 获取候选文本（1代表最匹配的那一个候选）
            top_candidate = observation.topCandidates_(1)[0]
            ocr_results.append(top_candidate.string())

    # 4. 创建文本识别请求 (VNRecognizeTextRequest)
    # 在 PyObjC 中，有回调的初始化方法使用 initWithCompletionHandler_
    request = Vision.VNRecognizeTextRequest.alloc().initWithCompletionHandler_(completion_handler)

    # 设置识别模式: .accurate (准确优先) 或 .fast (速度优先)
    # 0 代表 accurate, 1 代表 fast
    request.setRecognitionLevel_(0)

    # 设置是否自动检测语言
    request.setUsesLanguageCorrection_(True)

    # 5. 执行 OCR 请求
    success, error = handler.performRequests_error_([request], None)

    if not success:
        print(f"无法执行 OCR 请求: {error}")
        return None

    return ocr_results

# 使用示例
if __name__ == "__main__":
    # 请替换为你本地的图片路径
    from files import select_single_file
    import UniformTypeIdentifiers

    image_path = select_single_file([UniformTypeIdentifiers.UTTypeImage])
    if image_path:
        text_list = macos_ocr(image_path)
        print("\n".join(text_list))
