LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := fixbootableimage
LOCAL_SRC_FILES := fixbootableimage.c

include $(BUILD_EXECUTABLE)