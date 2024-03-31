#!/bin/sh


./ffmpeg -init_hw_device vulkan=vk:0 \
    -filter_hw_device vk \
    -i $2 \
    -filter_complex "format=rgba,hwupload,haar_vulkan=iterations=$1,hwdownload,format=rgba"\
    $3
