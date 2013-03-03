# Marco Zavatta
# TELECOM Bretagne
# Android.mk build file for libcoap by Olaf Bergmann

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := zesensecoap
LOCAL_SRC_FILES := ze_sm_resbuf.c ze_coap_resources.c ze_coap_server_core.c ze_coap_server_root.c ze_sm_reqbuf.c ze_streaming_manager.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../libcoap-3.0.0-android $(LOCAL_PATH)/../libzertp
LOCAL_LDLIBS  := -llog -landroid -lEGL -lGLESv1_CM
LOCAL_CFLAGS :=  -Wall -Wextra -std=c99 -pedantic -g -O2
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)

include $(BUILD_STATIC_LIBRARY)
