#pragma once

// Web/JSPI lifetime boundary: the Host must drain the active Wasm stack and
// finish native save writes before releasing TJS objects and their observers.
void TVPWebRequestNativeExit(int status);
