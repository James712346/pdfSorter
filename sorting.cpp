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
        int cropSizeSmall = w / 10;  // Smaller region for more precise edge searches

        std::vector<std::pair<std::string, cv::Rect>> corners = {
            // Original corner regions (priority search)
            {"top_left",     cv::Rect(0, 0, cropSizeW, cropSizeH)},
            {"bottom_right", cv::Rect(w - cropSizeW, h - cropSizeH, cropSizeW, cropSizeH)},
            {"top_right",    cv::Rect(w - cropSizeW, 0, cropSizeW, cropSizeH)},
            {"bottom_left",  cv::Rect(0, h - cropSizeH, cropSizeW, cropSizeH)},
            {"landscape_top_left",     cv::Rect(0, 0, cropSizeH, cropSizeW)},
            {"landscape_top_right",    cv::Rect(w - cropSizeH, 0, cropSizeH, cropSizeW)},
            {"landscape_bottom_left",  cv::Rect(0, h - cropSizeW, cropSizeH, cropSizeW)},
            {"landscape_bottom_right", cv::Rect(w - cropSizeH, h - cropSizeW, cropSizeH, cropSizeW)},
            {"worst_bottom_right", cv::Rect(w - cropSizeWW, h - cropSizeH, cropSizeWW, cropSizeH)},
            
            // Additional edge regions (secondary search)
            // Top edge variations
            {"top_middle",        cv::Rect(w/2 - cropSizeWW/2, 0, cropSizeWW, cropSizeH)},
            {"top_left_quarter",  cv::Rect(w/4 - cropSizeWW/2, 0, cropSizeWW, cropSizeH)},
            {"top_right_quarter", cv::Rect(3*w/4 - cropSizeWW/2, 0, cropSizeWW, cropSizeH)},
            
            // Bottom edge variations (your specific need)
            {"bottom_middle",      cv::Rect(w/2 - cropSizeWW/2, h - cropSizeH, cropSizeWW, cropSizeH)},
            {"bottom_left_quarter", cv::Rect(w/4 - cropSizeWW/2, h - cropSizeH, cropSizeWW, cropSizeH)},
            {"bottom_right_quarter",cv::Rect(3*w/4 - cropSizeWW/2, h - cropSizeH, cropSizeWW, cropSizeH)},
            {"bottom_right_small", cv::Rect(w - cropSizeSmall, h - cropSizeH, cropSizeSmall, cropSizeH)},
            {"bottom_left_small",  cv::Rect(0, h - cropSizeH, cropSizeSmall, cropSizeH)},
            
            // Left edge variations
            {"left_middle",        cv::Rect(0, h/2 - cropSizeH/2, cropSizeWW, cropSizeH)},
            {"left_top_quarter",   cv::Rect(0, h/4 - cropSizeH/2, cropSizeWW, cropSizeH)},
            {"left_bottom_quarter",cv::Rect(0, 3*h/4 - cropSizeH/2, cropSizeWW, cropSizeH)},
            
            // Right edge variations
            {"right_middle",        cv::Rect(w - cropSizeWW, h/2 - cropSizeH/2, cropSizeWW, cropSizeH)},
            {"right_top_quarter",   cv::Rect(w - cropSizeWW, h/4 - cropSizeH/2, cropSizeWW, cropSizeH)},
            {"right_bottom_quarter",cv::Rect(w - cropSizeWW, 3*h/4 - cropSizeH/2, cropSizeWW, cropSizeH)},
            
            // Small precise edge regions
            {"top_left_small",    cv::Rect(0, 0, cropSizeSmall, cropSizeH)},
            {"top_right_small",   cv::Rect(w - cropSizeSmall, 0, cropSizeSmall, cropSizeH)},
            {"left_top_small",    cv::Rect(0, 0, cropSizeWW, cropSizeSmall)},
            {"right_top_small",   cv::Rect(w - cropSizeWW, 0, cropSizeWW, cropSizeSmall)},
            {"left_bottom_small", cv::Rect(0, h - cropSizeSmall, cropSizeWW, cropSizeSmall)},
            {"right_bottom_small",cv::Rect(w - cropSizeWW, h - cropSizeSmall, cropSizeWW, cropSizeSmall)}
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

