#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>
#include <unordered_map>
#include <string>
#include <vector>

struct PageData {
    int foundpage;
    std::string type;
    int pageno;
};

std::unordered_map<std::string, std::vector<PageData>> DocumentsFound;
std::vector<cv::Mat> images;

extern "C" {

EMSCRIPTEN_KEEPALIVE
void add_image(uint8_t* data, int width, int height, int index) {
    cv::Mat img(height, width, CV_8UC4, data);
    images.push_back(img.clone());
}

EMSCRIPTEN_KEEPALIVE
const char* process_images() {
    DocumentsFound.clear();

    for (int i = 0; i < images.size(); ++i) {
        const cv::Mat& image = images[i];
        int h = image.rows, w = image.cols;
        int cropSizeH = h / 8;
        int cropSizeW = w / 2;
        int cropSizeWW = w / 6;

        std::vector<std::pair<std::string, cv::Rect>> corners = {
            {"top_left",     cv::Rect(0, 0, cropSizeW, cropSizeH)},
            {"bottom_right", cv::Rect(w - cropSizeW, h - cropSizeH, cropSizeW, cropSizeH)},
            {"top_right",    cv::Rect(w - cropSizeW, 0, cropSizeW, cropSizeH)},
            {"bottom_left",  cv::Rect(0, h - cropSizeH, cropSizeW, cropSizeH)},

            {"landscape_top_left",     cv::Rect(0, 0, cropSizeH, cropSizeW)},
            {"landscape_top_right",    cv::Rect(w - cropSizeH, 0, cropSizeH, cropSizeW)},
            {"landscape_bottom_left",  cv::Rect(0, h - cropSizeW, cropSizeH, cropSizeW)},
            {"landscape_bottom_right", cv::Rect(w - cropSizeH, h - cropSizeW, cropSizeH, cropSizeW)},
            {"worest-bottom_right", cv::Rect(w - cropSizeWW, h - cropSizeH, cropSizeWW, cropSizeH)}
        };

        PageData pageData;
        pageData.foundpage = i;
        cv::QRCodeDetector qrDetector;
        bool found = false;

        for (const auto& [label, roi] : corners) {
            if (roi.x < 0 || roi.y < 0 || roi.x + roi.width > w || roi.y + roi.height > h) continue;
            cv::Mat cropped = image(roi);
            std::string data = qrDetector.detectAndDecode(cropped);
            if (!data.empty()) {
                size_t del = data.find(';');
                if (del != std::string::npos) {
                    pageData.pageno = std::atoi(data.substr(del + 1).c_str());
                    data = data.substr(0, del);
                } else {
                    pageData.pageno = DocumentsFound[data].size() + 1;
                }
                pageData.type = label;
                DocumentsFound[data].push_back(pageData);
                found = true;
                break;
            }
        }

        if (!found) {
            pageData.pageno = DocumentsFound["Error"].size() + 1;
            DocumentsFound["Error"].push_back(pageData);
        }
    }

    // Serialize results to JSON string
    std::string result = "{";
    for (auto& [key, vals] : DocumentsFound) {
        result += "\"" + key + "\":[";
        for (size_t i = 0; i < vals.size(); ++i) {
            result += "{\"foundpage\":" + std::to_string(vals[i].foundpage) +
                      ",\"pageno\":" + std::to_string(vals[i].pageno) + "}";
            if (i + 1 < vals.size()) result += ",";
        }
        result += "],";
    }
    if (!DocumentsFound.empty()) result.pop_back();
    result += "}";

    static std::string output;
    output = result;
    return output.c_str();
}

EMSCRIPTEN_KEEPALIVE
void clear_images() {
    images.clear();
    DocumentsFound.clear();
}

} // extern "C"

