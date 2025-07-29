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

// Preprocessing function for better QR detection
cv::Mat preprocessForQR(const cv::Mat& input) {
    cv::Mat processed;
    
    // Convert to grayscale if needed
    if (input.channels() > 1) {
        cv::cvtColor(input, processed, cv::COLOR_BGR2GRAY);
    } else {
        processed = input.clone();
    }
    
    // Apply Gaussian blur to reduce noise
    cv::GaussianBlur(processed, processed, cv::Size(3, 3), 0);
    
    // Adaptive thresholding for better contrast
    cv::Mat binary;
    cv::adaptiveThreshold(processed, binary, 255, 
                         cv::ADAPTIVE_THRESH_GAUSSIAN_C, 
                         cv::THRESH_BINARY, 11, 2);
    
    // Optional: Morphological operations to clean up
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
    cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);
    
    return binary;
}

// Multi-scale QR detection
std::pair<std::string, std::string> detectQRAtMultipleScales(const cv::Mat& image) {
    cv::QRCodeDetector qrDetector;
    
    // Try different scales
    std::vector<double> scales = {1.0, 0.8, 0.6, 0.4, 0.3, 0.2};
    
    for (double scale : scales) {
        cv::Mat resized;
        cv::resize(image, resized, cv::Size(), scale, scale);
        
        std::vector<cv::Point> points;
        std::string data = qrDetector.detectAndDecode(resized, points);
        
        if (!data.empty()) {
            std::string type = "scale_" + std::to_string(scale);
            return std::make_pair(data, type);
        }
    }
    return std::make_pair("", "");
}

// Sliding window QR detection
std::pair<std::string, std::string> slidingWindowQRDetection(const cv::Mat& image) {
    cv::QRCodeDetector qrDetector;
    int minDim = std::min(image.rows, image.cols);
    
    // Try different window sizes
    std::vector<int> windowSizes = {minDim/3, minDim/4, minDim/5, minDim/6};
    
    for (int windowSize : windowSizes) {
        int step = windowSize / 3; // 33% overlap
        
        for (int y = 0; y <= image.rows - windowSize; y += step) {
            for (int x = 0; x <= image.cols - windowSize; x += step) {
                cv::Rect roi(x, y, windowSize, windowSize);
                cv::Mat window = image(roi);
                
                std::string data = qrDetector.detectAndDecode(window);
                if (!data.empty()) {
                    std::string type = "sliding_window_" + std::to_string(windowSize);
                    return std::make_pair(data, type);
                }
            }
        }
    }
    return std::make_pair("", "");
}

// Enhanced corner detection with more regions
std::pair<std::string, std::string> detectQRInCorners(const cv::Mat& image) {
    cv::QRCodeDetector qrDetector;
    int h = image.rows, w = image.cols;
    int cropSizeH = h / 6;
    int cropSizeW = w / 2;
    int cropSizeWW = w / 4;

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
        
        // Bottom edge variations
        {"bottom_middle",      cv::Rect(w/2 - cropSizeWW/2, h - cropSizeH, cropSizeWW, cropSizeH)},
        {"bottom_left_quarter", cv::Rect(w/4 - cropSizeWW/2, h - cropSizeH, cropSizeWW, cropSizeH)},
        {"bottom_right_quarter",cv::Rect(3*w/4 - cropSizeWW/2, h - cropSizeH, cropSizeWW, cropSizeH)},
        
        // Left edge variations
        {"left_middle",        cv::Rect(0, h/2 - cropSizeH/2, cropSizeWW, cropSizeH)},
        {"left_top_quarter",   cv::Rect(0, h/4 - cropSizeH/2, cropSizeWW, cropSizeH)},
        {"left_bottom_quarter",cv::Rect(0, 3*h/4 - cropSizeH/2, cropSizeWW, cropSizeH)},
        
        // Right edge variations
        {"right_middle",        cv::Rect(w - cropSizeWW, h/2 - cropSizeH/2, cropSizeWW, cropSizeH)},
        {"right_top_quarter",   cv::Rect(w - cropSizeWW, h/4 - cropSizeH/2, cropSizeWW, cropSizeH)},
        {"right_bottom_quarter",cv::Rect(w - cropSizeWW, 3*h/4 - cropSizeH/2, cropSizeWW, cropSizeH)},
        
        // Center regions
        {"center",             cv::Rect(w/2 - cropSizeWW/2, h/2 - cropSizeH/2, cropSizeWW, cropSizeH)},
        {"center_large",       cv::Rect(w/2 - cropSizeW/4, h/2 - cropSizeH/2, cropSizeW/2, cropSizeH)},
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
const char* process_images() {
    DocumentsFound.clear();

    for (int i = 0; i < images.size(); ++i) {
        const cv::Mat& image = images[i];
        cv::QRCodeDetector qrDetector;
        bool found = false;
        PageData pageData;
        pageData.foundpage = i;
        std::string data;
        std::string detectionType;

        // Method 1: Try full image with preprocessing
        cv::Mat processed = preprocessForQR(image);
        data = qrDetector.detectAndDecode(processed);
        
        if (!data.empty()) {
            detectionType = "full_image_processed";
            found = true;
        }

        // Method 2: Try full image without preprocessing
        if (!found) {
            data = qrDetector.detectAndDecode(image);
            if (!data.empty()) {
                detectionType = "full_image_raw";
                found = true;
            }
        }

        // Method 3: Multi-scale detection on processed image
        if (!found) {
            auto result = detectQRAtMultipleScales(processed);
            if (!result.first.empty()) {
                data = result.first;
                detectionType = result.second + "_processed";
                found = true;
            }
        }

        // Method 4: Multi-scale detection on original image
        if (!found) {
            auto result = detectQRAtMultipleScales(image);
            if (!result.first.empty()) {
                data = result.first;
                detectionType = result.second + "_raw";
                found = true;
            }
        }

        // Method 5: Sliding window on processed image
        if (!found) {
            auto result = slidingWindowQRDetection(processed);
            if (!result.first.empty()) {
                data = result.first;
                detectionType = result.second + "_processed";
                found = true;
            }
        }

        // Method 6: Sliding window on original image
        if (!found) {
            auto result = slidingWindowQRDetection(image);
            if (!result.first.empty()) {
                data = result.first;
                detectionType = result.second + "_raw";
                found = true;
            }
        }

        // Method 7: Corner detection on processed image
        if (!found) {
            auto result = detectQRInCorners(processed);
            if (!result.first.empty()) {
                data = result.first;
                detectionType = result.second + "_processed";
                found = true;
            }
        }

        // Method 8: Corner detection on original image (fallback to original method)
        if (!found) {
            auto result = detectQRInCorners(image);
            if (!result.first.empty()) {
                data = result.first;
                detectionType = result.second + "_raw";
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

} // extern "C"
