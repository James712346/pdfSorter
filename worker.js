// worker.js - Save this as a separate file
// Enhanced worker with error recovery and individual page processing
let wasmModule = null;

function UTF8ToString(mod, ptr) {
  const memory = mod.HEAPU8;
  let endPtr = ptr;
  while(memory[endPtr] !== 0) endPtr++;
  return new TextDecoder('utf8').decode(memory.subarray(ptr, endPtr));
}

self.onmessage = async function(e) {
  const { type, data } = e.data;
  
  if (type === 'INIT_WASM') {
    try {
      postMessage({ type: 'STATUS', message: 'Loading WASM module...' });
      
      // Import the JS file
      importScripts('sorting.js');
      
      // Initialize the module
      wasmModule = await Module();
      postMessage({ type: 'WASM_READY' });
    } catch (error) {
      postMessage({ type: 'ERROR', error: error.message });
    }
  }
  
  else if (type === 'ADD_IMAGE') {
    try {
      const { imageData, width, height, index } = data;
      
      const len = imageData.length;
      const ptr = wasmModule._malloc(len);
      wasmModule.HEAPU8.set(imageData, ptr);
      wasmModule._add_image(ptr, width, height, index);
      wasmModule._free(ptr);
      
      postMessage({ type: 'IMAGE_ADDED', index });
    } catch (error) {
      postMessage({ type: 'ERROR', error: `Error adding image ${data.index}: ${error.message}` });
    }
  }
  
  else if (type === 'PROCESS_BATCH') {
    try {
      const { startIndex, endIndex, skipList = [], individualMode = false } = data;
      
      if (individualMode) {
        // Individual page processing mode for error recovery
        let processedCount = 0;
        const totalToProcess = endIndex - startIndex;
        
        for (let i = startIndex; i < endIndex; i++) {
          if (skipList.includes(i + 1)) {
            postMessage({ 
              type: 'PAGE_SKIPPED', 
              pageIndex: i + 1 
            });
            continue;
          }
          
          try {
            // Process single page
            const progressPtr = wasmModule._process_images_with_progress(i, i+1);
            const progressStr = UTF8ToString(wasmModule, progressPtr);
            const progress = JSON.parse(progressStr);
            
            processedCount++;
            postMessage({ 
              type: 'PAGE_COMPLETE', 
              pageIndex: i + 1,
              current: processedCount, 
              total: totalToProcess,
              pageData: progress
            });
            
          } catch (pageError) {
            postMessage({ 
              type: 'PAGE_ERROR', 
              pageIndex: i + 1, 
              error: pageError.message,
              batchStart: startIndex,
              batchEnd: endIndex
            });
            // Stop individual processing and wait for user decision
            return;
          }
        }
        
        postMessage({ 
          type: 'INDIVIDUAL_BATCH_COMPLETE', 
          current: processedCount, 
          total: totalToProcess,
          batchStart: startIndex,
          batchEnd: endIndex
        });
        
      } else {
        // Normal batch processing mode
        const progressPtr = wasmModule._process_images_with_progress(startIndex, endIndex);
        const progressStr = UTF8ToString(wasmModule, progressPtr);
        const progress = JSON.parse(progressStr);
        
        postMessage({ 
          type: 'BATCH_COMPLETE', 
          current: progress.processed, 
          total: progress.total,
          batchStart: startIndex,
          batchEnd: endIndex
        });
      }
      
    } catch (error) {
      // Batch failed - suggest switching to individual mode
      postMessage({ 
        type: 'BATCH_ERROR', 
        error: error.message,
        batchStart: data.startIndex,
        batchEnd: data.endIndex
      });
    }
  }
  
  else if (type === 'GET_RESULTS') {
    try {
      const resultPtr = wasmModule._get_final_results();
      const resultStr = UTF8ToString(wasmModule, resultPtr);
      
      let sortedPages;
      try {
        sortedPages = JSON.parse(resultStr);
      } catch (e) {
        throw new Error("Failed to parse WASM results: " + e.message);
      }
      
      postMessage({ type: 'RESULTS_READY', sortedPages });
      
      // Cleanup
      wasmModule._clear_images();
    } catch (error) {
      postMessage({ type: 'ERROR', error: error.message });
    }
  }
  
  else if (type === 'GET_PARTIAL_RESULTS') {
    try {
      // Get partial results from WASM module
      const partialResultPtr = wasmModule._get_partial_results();
      const partialResultStr = UTF8ToString(wasmModule, partialResultPtr);
      
      let partialData;
      try {
        partialData = JSON.parse(partialResultStr);
      } catch (e) {
        throw new Error("Failed to parse partial results: " + e.message);
      }
      
      // Also get current processed count for additional validation
      const processedCount = wasmModule._get_processed_count();
      const totalImages = wasmModule._get_total_images();
      
      // Enhance partial data with additional metadata
      const enhancedPartialData = {
        ...partialData,
        metadata: {
          timestamp: Date.now(),
          processingComplete: processedCount >= totalImages,
          progressPercentage: totalImages > 0 ? Math.round((processedCount / totalImages) * 100) : 0
        }
      };
    console.log(enhancedPartialData);
      postMessage({ 
        type: 'PARTIAL_RESULTS', 
        partialResults: enhancedPartialData
      });
      
    } catch (error) {
      // Fallback to basic progress info if partial results fail
      try {
        const processedCount = wasmModule._get_processed_count();
        const totalImages = wasmModule._get_total_images();
        
        const fallbackData = {
          progress: {
            processedImages: processedCount,
            totalImages: totalImages,
            documentsFound: 0
          },
          results: {},
          metadata: {
            timestamp: Date.now(),
            processingComplete: processedCount >= totalImages,
            progressPercentage: totalImages > 0 ? Math.round((processedCount / totalImages) * 100) : 0,
            error: "Partial results unavailable, showing basic progress",
            originalError: error.message
          }
        };
        
        postMessage({ 
          type: 'PARTIAL_RESULTS', 
          partialResults: fallbackData
        });
        
      } catch (fallbackError) {
        // Complete fallback if even basic info is unavailable
        postMessage({ 
          type: 'PARTIAL_RESULTS', 
          partialResults: {
            progress: { processedImages: 0, totalImages: 0, documentsFound: 0 },
            results: {},
            metadata: {
              timestamp: Date.now(),
              processingComplete: false,
              progressPercentage: 0,
              error: "Unable to retrieve any partial results",
              originalError: error.message,
              fallbackError: fallbackError.message
            }
          }
        });
      }
    }
  }
  
  else if (type === 'GET_TOTAL_IMAGES') {
    try {
      const total = wasmModule._get_total_images();
      postMessage({ type: 'TOTAL_IMAGES', total });
    } catch (error) {
      postMessage({ type: 'ERROR', error: error.message });
    }
  }
  
  else if (type === 'GET_PROCESSED_COUNT') {
    try {
      const processed = wasmModule._get_processed_count();
      const total = wasmModule._get_total_images();
      postMessage({ 
        type: 'PROCESSED_COUNT', 
        processed, 
        total,
        percentage: total > 0 ? Math.round((processed / total) * 100) : 0
      });
    } catch (error) {
      postMessage({ type: 'ERROR', error: error.message });
    }
  }
  
  else if (type === 'CLEAR_IMAGES') {
    try {
      wasmModule._clear_images();
      postMessage({ type: 'IMAGES_CLEARED' });
    } catch (error) {
      postMessage({ type: 'ERROR', error: error.message });
    }
  }
};
