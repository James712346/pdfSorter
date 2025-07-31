EMCC = emcc
CXXFLAGS = -std=c++20 -s WASM=1 -O3 -s EXIT_RUNTIME=1 -s ALLOW_MEMORY_GROWTH=1 -s LLD_REPORT_UNDEFINED -s ERROR_ON_UNDEFINED_SYMBOLS=0
EXPORTED_FUNCS = '["_add_image", "_process_images_with_progress", "_get_final_results", "_get_total_images", "_clear_images", "_malloc", "_free"]'
OPENCV_CFLAGS := -I/usr/include/opencv4
OPENCV_LIBS := -L/home/jamesp/Repos/webassemble/opencv/platforms/js/build_wasm/lib -lopencv_core -lopencv_objdetect -lopencv_imgproc -lopencv_imgcodecs -lopencv_calib3d -lopencv_flann -lopencv_features2d
OUT_JS = sorting.js
SRC = sorting.cpp

all:
	$(EMCC) $(CXXFLAGS) $(SRC) \
		$(OPENCV_CFLAGS) $(OPENCV_LIBS) \
		-s EXPORTED_FUNCTIONS=$(EXPORTED_FUNCS) \
		-s MODULARIZE=1 -s 'EXPORT_NAME="Module"' \
		-s "EXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8','lengthBytesUTF8', 'HEAPU8']"\
		-o $(OUT_JS)

clean:
	rm -f sorting.js sorting.wasm

.PHONY: all clean
