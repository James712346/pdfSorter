// worker.js - Save this as a separate file
// Only handle WASM operations in the worker

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
      postMessage({ type: 'ERROR', error: error.message });
    }
  }
  
  else if (type === 'PROCESS_BATCH') {
    try {
      const { startIndex, endIndex } = data;
      
      const progressPtr = wasmModule._process_images_with_progress(startIndex, endIndex);
      const progressStr = UTF8ToString(wasmModule, progressPtr);
      const progress = JSON.parse(progressStr);
      
      postMessage({ 
        type: 'BATCH_COMPLETE', 
        current: progress.processed, 
        total: progress.total
      });
    } catch (error) {
      postMessage({ type: 'ERROR', error: error.message });
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
  
  else if (type === 'GET_TOTAL_IMAGES') {
    try {
      const total = wasmModule._get_total_images();
      postMessage({ type: 'TOTAL_IMAGES', total });
    } catch (error) {
      postMessage({ type: 'ERROR', error: error.message });
    }
  }
};
