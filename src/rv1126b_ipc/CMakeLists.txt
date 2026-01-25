
#include(FindPkgConfig)
#pkg_check_modules (GLIB REQUIRED IMPORTED_TARGET glib-2.0)

# link_directories(${PROJECT_SOURCE_DIR}/lib)
include_directories(${PROJECT_SOURCE_DIR}/include
					${CMAKE_SYSROOT}/usr/include/rkaiq/
					${CMAKE_SYSROOT}/usr/include/rkaiq/isp
					${CMAKE_SYSROOT}/usr/include/rkaiq/uAPI
					${CMAKE_SYSROOT}/usr/include/rkaiq/uAPI2
					${CMAKE_SYSROOT}/usr/include/rkaiq/algos
					${CMAKE_SYSROOT}/usr/include/rkaiq/common
					${CMAKE_SYSROOT}/usr/include/rkaiq/xcore
					${CMAKE_SYSROOT}/usr/include/rkaiq/iq_parser
					${CMAKE_SYSROOT}/usr/include/rkaiq/iq_parser_v2
					${CMAKE_SYSROOT}/usr/include/libdrm
					${CMAKE_SYSROOT}/usr/include/freetype2
					${WPA_SUPPLICANT_INCLUDE_DIRS}
					)

aux_source_directory(./ SRCS)

aux_source_directory(video SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/ SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/audio SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/isp/rv1126b SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/socket_server SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/param SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/system SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/rtsp SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/rtmp SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/osd SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/osd/freetype2 SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/roi SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/network SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/storage SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/event SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/region_clip SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/sysutil SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/venc SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/uvc SRCS)


aux_source_directory(yolo26/engine SRCS)
aux_source_directory(yolo26/task SRCS)
aux_source_directory(yolo26/draw SRCS)
aux_source_directory(yolo26/process SRCS)

include_directories(video
					${PROJECT_SOURCE_DIR}/common
					${PROJECT_SOURCE_DIR}/common/audio
					${PROJECT_SOURCE_DIR}/common/isp/rv1126b
					${PROJECT_SOURCE_DIR}/common/socket_server
					${PROJECT_SOURCE_DIR}/common/rtsp
					${PROJECT_SOURCE_DIR}/common/rtmp
					${PROJECT_SOURCE_DIR}/common/param
					${PROJECT_SOURCE_DIR}/common/system
					${PROJECT_SOURCE_DIR}/common/osd
					${PROJECT_SOURCE_DIR}/common/osd/freetype2
					${PROJECT_SOURCE_DIR}/common/roi
					${PROJECT_SOURCE_DIR}/common/network
					${PROJECT_SOURCE_DIR}/common/storage
					${PROJECT_SOURCE_DIR}/common/event
					${PROJECT_SOURCE_DIR}/common/region_clip
					${PROJECT_SOURCE_DIR}/common/sysutil
					${PROJECT_SOURCE_DIR}/common/venc
					${PROJECT_SOURCE_DIR}/common/uvc

					yolo26/
					rknn/
)

add_definitions(-DRKIPC_RV1126B)

set(OpenCV_DIR ${CMAKE_SOURCE_DIR}/lib/arm-rockchip1240-linux-gnueabihf/opencv-mobile/lib/cmake/opencv4)
find_package(OpenCV REQUIRED)

link_directories(${PROJECT_SOURCE_DIR}/lib/arm-rockchip1240-linux-gnueabihf)
install(FILES ${PROJECT_SOURCE_DIR}/lib/arm-rockchip1240-linux-gnueabihf/libwpa_client.so DESTINATION lib)
install(DIRECTORY $(PROJECT_SOURCE_DIR)/lib/arm-rockchip1240-linux-gnueabihf/opencv-mobile/lib 
				DESTINATION lib FILES_MATCHING PATTERN "*.a")

add_definitions(-DISP_HW_V35)
add_definitions(-g -ggdb)

add_executable(${PROJECT_NAME} ${SRCS})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-result -Wno-unused-but-set-variable -Wno-unused-variable -Werror=format")

target_link_libraries(${PROJECT_NAME} 
	${OpenCV_LIBS} pthread rockit rkaiq rtsp freetype wpa_client rkmuxer m rknnrt rockchip_mpp rga rkaudio)


install(FILES rkipc-2688x1520.ini DESTINATION share)
install(PROGRAMS RkLunch.sh DESTINATION bin)
install(FILES ../../common/speaker_test.wav DESTINATION share)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(PROGRAMS RkLunch-stop.sh DESTINATION bin)
install(PROGRAMS ${PROJECT_SOURCE_DIR}/common/uvc/rkipc_usb_config.sh DESTINATION bin)
install(FILES ${PROJECT_SOURCE_DIR}/common/osd/image.bmp DESTINATION share)
install(FILES ${PROJECT_SOURCE_DIR}/common/osd/SourceHanSansCN.ttf DESTINATION share)
install(FILES ${PROJECT_SOURCE_DIR}/common/osd/SourceHanSansEN.ttf DESTINATION share)
