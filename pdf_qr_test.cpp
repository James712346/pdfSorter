#include <iostream>
#include <opencv2/objdetect.hpp>
#include <opencv2/opencv.hpp>
#include <poppler-document.h>
#include <poppler-page-renderer.h>
#include <poppler-page.h>
#include <string>
#include <vector>

struct CropBox {
  std::string name;
  double x_ratio, y_ratio, w_ratio, h_ratio;
};

cv::Mat popplerToMat(poppler::image &img) {
  if (!img.is_valid())
    return cv::Mat();

  int w = img.width();
  int h = img.height();
  cv::Mat mat(h, w, CV_8UC4, img.data(), img.bytes_per_row());
  cv::Mat mat_bgr;
  cv::cvtColor(mat, mat_bgr, cv::COLOR_RGBA2BGR);
  return mat_bgr.clone();
}

cv::Mat preprocess(const cv::Mat &cropped) {
  cv::Mat gray, blurred, sharpened;
  cv::cvtColor(cropped, gray, cv::COLOR_BGR2GRAY);
  cv::Mat kernel = (cv::Mat_<float>(3, 3) << 0, -1, 0, -1, 5, -1, 0, -1, 0);
  cv::filter2D(cropped, sharpened, cropped.depth(), kernel);
  return sharpened;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <pdf_file>\n";
    return 1;
  }

  std::string pdf_file = argv[1];
  std::unique_ptr<poppler::document> doc(
      poppler::document::load_from_file(pdf_file));
  if (!doc) {
    std::cerr << "Error: Cannot open PDF " << pdf_file << "\n";
    return 1;
  }

  // Define crop regions (same as before)
  std::vector<CropBox> crops = {
      {"top_left", 0.0, 0.0, 0.5, 0.125},
      {"bottom_right", 0.5, 0.875, 0.5, 0.125},
      {"top_right", 0.5, 0.0, 0.5, 0.125},
      {"bottom_left", 0.0, 0.875, 0.5, 0.125},

      {"landscape_top_left", 0.0, 0.0, 0.125, 0.5},
      {"landscape_top_right", 0.875, 0.0, 0.125, 0.5},
      {"landscape_bottom_left", 0.0, 0.5, 0.125, 0.5},
      {"landscape_bottom_right", 0.875, 0.5, 0.125, 0.5},

      {"custom_qr_box", 0.53, 0.8922, 0.1952, 0.1010}};

  cv::QRCodeDetector qrDetector;

  for (int i = 0; i < doc->pages(); ++i) {
    std::unique_ptr<poppler::page> page(doc->create_page(i));
    if (!page)
      continue;

    poppler::page_renderer renderer;
    renderer.set_render_hint(poppler::page_renderer::antialiasing, true);
    renderer.set_render_hint(poppler::page_renderer::text_antialiasing, true);

    poppler::image pimg = renderer.render_page(page.get(), 300, 300); // 300 DPI
    cv::Mat image = preprocess(popplerToMat(pimg));
    if (image.empty())
      continue;

    int w = image.cols;
    int h = image.rows;

    std::cout << "Processing page " << i + 1 << "...\n";

    for (const auto &crop : crops) {
      int x = static_cast<int>(w * crop.x_ratio);
      int y = static_cast<int>(h * crop.y_ratio);
      int cw = static_cast<int>(w * crop.w_ratio);
      int ch = static_cast<int>(h * crop.h_ratio);

      if (x < 0 || y < 0 || x + cw > w || y + ch > h)
        continue;

      cv::Rect roi(x, y, cw, ch);
      cv::Mat cropped = image(roi);

      std::string data = qrDetector.detectAndDecode(cropped);
      if (!data.empty()) {
        std::cout << "  [FOUND] " << crop.name << " -> " << data << "\n";
        cv::rectangle(image, roi, cv::Scalar(0, 255, 0), 3);
        cv::putText(image, crop.name, roi.tl() + cv::Point(5, 20),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
      } else {
        std::cout << "  [MISS ] " << crop.name << "\n";
        cv::rectangle(image, roi, cv::Scalar(0, 0, 255), 2);
        cv::putText(image, crop.name, roi.tl() + cv::Point(5, 20),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255), 2);
      }
    }

    std::string outname = "pages/page_" + std::to_string(i + 1) + ".png";
    cv::imwrite(outname, image);
    std::cout << "  Saved annotated page: " << outname << "\n";
  }

  return 0;
}
