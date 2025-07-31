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

// Simplified preprocessing function
cv::Mat preprocessForQR(const cv::Mat& input) {
    cv::Mat processed;
    
    // Convert to grayscale if needed
    if (input.channels() > 1) {
        cv::cvtColor(input, processed, cv::COLOR_BGR2GRAY);
    } else {
        processed = input.clone();
    }
    
    // Light Gaussian blur to reduce noise
    cv::GaussianBlur(processed, processed, cv::Size(3, 3), 0);
    
    return processed;
}

// Optimized sliding window QR detection - only top and bottom regions
std::pair<std::string, std::string> optimizedSlidingWindowQRDetection(const cv::Mat& image) {
    cv::QRCodeDetector qrDetector;
    int h = image.rows;
    int w = image.cols;
    
    // Define top and bottom regions (1/4 of image height each)
    int regionHeight = h / 4;
    std::vector<std::pair<std::string, cv::Rect>> regions = {
        {"top_region", cv::Rect(0, 0, w, regionHeight)},
        {"bottom_region", cv::Rect(0, h - regionHeight, w, regionHeight)}
    };
    
    // Try fewer, larger window sizes for efficiency
    std::vector<int> windowSizes = {regionHeight/2, regionHeight/3};
    
    for (const auto& [regionName, regionRect] : regions) {
        cv::Mat region = image(regionRect);
        
        for (int windowSize : windowSizes) {
            int step = windowSize / 2; // 50% overlap for efficiency
            
            for (int y = 0; y <= region.rows - windowSize; y += step) {
                for (int x = 0; x <= region.cols - windowSize; x += step) {
                    cv::Rect roi(x, y, windowSize, windowSize);
                    cv::Mat window = region(roi);
                    
                    std::string data = qrDetector.detectAndDecode(window);
                    if (!data.empty()) {
                        std::string type = regionName + "_window_" + std::to_string(windowSize);
                        return std::make_pair(data, type);
                    }
                }
            }
        }
    }
    return std::make_pair("", "");
}

// Simplified corner detection
std::pair<std::string, std::string> detectQRInCorners(const cv::Mat& image) {
    cv::QRCodeDetector qrDetector;
    int h = image.rows, w = image.cols;
    int cropSizeH = h / 6;
    int cropSizeW = w / 4;

    std::vector<std::pair<std::string, cv::Rect>> corners = {
        {"top_left",     cv::Rect(0, 0, cropSizeW, cropSizeH)},
        {"top_right",    cv::Rect(w - cropSizeW, 0, cropSizeW, cropSizeH)},
        {"bottom_left",  cv::Rect(0, h - cropSizeH, cropSizeW, cropSizeH)},
        {"bottom_right", cv::Rect(w - cropSizeW, h - cropSizeH, cropSizeW, cropSizeH)},
    };

    for (const auto& [label, roi] : corners) {
        // Check bounds
        if (roi.x < 0 || roi.y < 0 || roi.x + roi.width > w || roi.y + roi.height > h) continue;
        
        cv::Mat cropped = image(roi);
        std::string data = qrDetector.detectAndDecode(cropped);
        if (!data.empty()) {
            return std::make_pair(data, label);
        }
    }
    
    return std::make_pair("", "");
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
void add_image(uint8_t* data, int width, int height, int index) {
    cv::Mat img(height, width, CV_8UC4, data);
    images.push_back(img.clone());
}

EMSCRIPTEN_KEEPALIVE
const char* process_images_with_progress(int startIndex, int endIndex) {
    for (int i = startIndex; i < endIndex && i < images.size(); ++i) {
        const cv::Mat& image = images[i];
        cv::QRCodeDetector qrDetector;
        bool found = false;
        PageData pageData;
        pageData.foundpage = i;
        std::string data;
        std::string detectionType;

        // Method 1: Try full image directly
        data = qrDetector.detectAndDecode(image);
        if (!data.empty()) {
            detectionType = "full_image_raw";
            found = true;
        }

        // Method 2: Try full image with preprocessing
        if (!found) {
            cv::Mat processed = preprocessForQR(image);
            data = qrDetector.detectAndDecode(processed);
            if (!data.empty()) {
                detectionType = "full_image_processed";
                found = true;
            }
        }

        // Method 3: Corner detection on original image
        if (!found) {
            auto result = detectQRInCorners(image);
            if (!result.first.empty()) {
                data = result.first;
                detectionType = result.second + "_raw";
                found = true;
            }
        }

        // Method 4: Corner detection on processed image
        if (!found) {
            cv::Mat processed = preprocessForQR(image);
            auto result = detectQRInCorners(processed);
            if (!result.first.empty()) {
                data = result.first;
                detectionType = result.second + "_processed";
                found = true;
            }
        }

        // Method 5: Optimized sliding window on original image
        if (!found) {
            auto result = optimizedSlidingWindowQRDetection(image);
            if (!result.first.empty()) {
                data = result.first;
                detectionType = result.second + "_raw";
                found = true;
            }
        }

        // Method 6: Optimized sliding window on processed image
        if (!found) {
            cv::Mat processed = preprocessForQR(image);
            auto result = optimizedSlidingWindowQRDetection(processed);
            if (!result.first.empty()) {
                data = result.first;
                detectionType = result.second + "_processed";
                found = true;
            }
        }

        // Process the found data or mark as error
        if (found) {
            // Parse page number if present
            size_t del = data.find(';');
            if (del != std::string::npos) {
                pageData.pageno = std::atoi(data.substr(del + 1).c_str());
                data = data.substr(0, del);
            } else {
                pageData.pageno = DocumentsFound[data].size() + 1;
            }
            pageData.type = detectionType;
            DocumentsFound[data].push_back(pageData);
        } else {
            // No QR code found with any method
            pageData.pageno = DocumentsFound["Error"].size() + 1;
            pageData.type = "not_found";
            DocumentsFound["Error"].push_back(pageData);
        }
    }

    // Return progress info as JSON
    std::string progress = "{\"processed\":" + std::to_string(endIndex) + ",\"total\":" + std::to_string(images.size()) + "}";
    static std::string progressOutput;
    progressOutput = progress;
    return progressOutput.c_str();
}

EMSCRIPTEN_KEEPALIVE
const char* get_final_results() {
    // Serialize results to JSON string
    std::string result = "{";
    for (auto it = DocumentsFound.begin(); it != DocumentsFound.end(); ++it) {
        const std::string& key = it->first;
        const std::vector<PageData>& vals = it->second;
        
        result += "\"" + key + "\":[";
        for (size_t i = 0; i < vals.size(); ++i) {
            result += "{\"foundpage\":" + std::to_string(vals[i].foundpage) +
                      ",\"pageno\":" + std::to_string(vals[i].pageno) +
                      ",\"type\":\"" + vals[i].type + "\"}";
            if (i + 1 < vals.size()) result += ",";
        }
        result += "]";
        
        // Add comma if not the last element
        auto next_it = it;
        ++next_it;
        if (next_it != DocumentsFound.end()) {
            result += ",";
        }
    }
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

EMSCRIPTEN_KEEPALIVE
int get_total_images() {
    return images.size();
}

} // extern "C"
