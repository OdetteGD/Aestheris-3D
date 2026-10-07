#pragma once

// Centralized Android diagnostics for the single native engine boundary.

#include <android/log.h>
#include <cstdint>
#include <type_traits>

#ifndef AETHERIS_LOG_TAG
#define AETHERIS_LOG_TAG "AetherisEngine"
#endif

#define AETHERIS_LOGI(...) __android_log_print(ANDROID_LOG_INFO, AETHERIS_LOG_TAG, __VA_ARGS__)
#define AETHERIS_LOGW(...) __android_log_print(ANDROID_LOG_WARN, AETHERIS_LOG_TAG, __VA_ARGS__)
#define AETHERIS_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, AETHERIS_LOG_TAG, __VA_ARGS__)
#define AETHERIS_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, AETHERIS_LOG_TAG, __VA_ARGS__)

#define AETHERIS_VK_TAG "VulkanRenderer"
#define AETHERIS_VK_LOGI(...) __android_log_print(ANDROID_LOG_INFO, AETHERIS_VK_TAG, __VA_ARGS__)
#define AETHERIS_VK_LOGW(...) __android_log_print(ANDROID_LOG_WARN, AETHERIS_VK_TAG, __VA_ARGS__)
#define AETHERIS_VK_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, AETHERIS_VK_TAG, __VA_ARGS__)
#define AETHERIS_VK_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, AETHERIS_VK_TAG, __VA_ARGS__)

namespace aetheris {

template <typename T>
inline uint64_t VkHandleValue(T handle) noexcept {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<uintptr_t>(handle);
    } else {
        return static_cast<uint64_t>(handle);
    }
}

inline const char* VkResultName(int result) noexcept {
    switch (result) {
        case 0: return "VK_SUCCESS";
        case 1: return "VK_NOT_READY";
        case 2: return "VK_TIMEOUT";
        case 3: return "VK_EVENT_SET";
        case 4: return "VK_EVENT_RESET";
        case -1: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case -2: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case -3: return "VK_ERROR_INITIALIZATION_FAILED";
        case -4: return "VK_ERROR_DEVICE_LOST";
        case -5: return "VK_ERROR_MEMORY_MAP_FAILED";
        case -6: return "VK_ERROR_LAYER_NOT_PRESENT";
        case -7: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case -8: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case -9: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case -1000001004: return "VK_ERROR_OUT_OF_DATE_KHR";
        case 1000001003: return "VK_SUBOPTIMAL_KHR";
        default: return "VK_RESULT_UNKNOWN";
    }
}
}
