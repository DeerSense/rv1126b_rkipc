
#include(FindPkgConfig)
#pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib - 2.0)

#link_directories(${PROJECT_SOURCE_DIR} / lib)
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
					${CMAKE_SYSROOT}/usr/include/lvgl
					)

aux_source_directory(./ SRCS)

aux_source_directory(video SRCS)
aux_source_directory(ui SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/ SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/ui/common SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/lvgl/common SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/lvgl/drivers SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/lvgl/porting SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/audio SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/isp/rv1126b SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/param SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/player SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/system SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/rtsp SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/osd SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/osd/freetype2 SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/storage SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/event SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/sysutil SRCS)

include_directories(video
					ui
					${PROJECT_SOURCE_DIR}/common
					${PROJECT_SOURCE_DIR}/common/audio
					${PROJECT_SOURCE_DIR}/common/ui/common
					${PROJECT_SOURCE_DIR}/common/lvgl/common
					${PROJECT_SOURCE_DIR}/common/lvgl/drivers
					${PROJECT_SOURCE_DIR}/common/lvgl/porting
					${PROJECT_SOURCE_DIR}/common/isp/rv1126b
					${PROJECT_SOURCE_DIR}/common/rtsp
					${PROJECT_SOURCE_DIR}/common/param
					${PROJECT_SOURCE_DIR}/common/player
					${PROJECT_SOURCE_DIR}/common/system
					${PROJECT_SOURCE_DIR}/common/osd
					${PROJECT_SOURCE_DIR}/common/osd/freetype2
					${PROJECT_SOURCE_DIR}/common/storage
					${PROJECT_SOURCE_DIR}/common/event
					${PROJECT_SOURCE_DIR}/common/sysutil
					)

message(STATUS "RK_APP_ARCH_TYPE is ${RK_APP_ARCH_TYPE}")

add_definitions(-DDRAW_UI_BY_VO=1 -DLVGL_SUPPORT_TOUCHSCREEN)
add_definitions(-DISP_HW_V35)
add_definitions(-g -ggdb)
add_definitions(-DRV1126B=1)
if(COMPILE_FOR_BUILDROOT)
add_definitions(-DBUILDROOT)
endif()

add_executable(${PROJECT_NAME} ${SRCS})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-result -Wno-unused-but-set-variable -Wno-unused-variabl -Werror=format")

target_link_libraries(${PROJECT_NAME}
						pthread rockit rkaiq rtsp freetype rkmuxer m rga rkaudio
						lvgl rkdemuxer png RkEis)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(FILES rkipc.ini DESTINATION share)
install(FILES rkipc-gc8613.ini DESTINATION share)
install(FILES rkipc-imx586.ini DESTINATION share)
if(COMPILE_FOR_BUILDROOT)
install(PROGRAMS RkLunch-buildroot.sh DESTINATION bin RENAME RkLunch.sh)
else()
install(PROGRAMS RkLunch.sh DESTINATION bin)
endif()
install(PROGRAMS RkLunch-stop.sh DESTINATION bin)
install(FILES ${PROJECT_SOURCE_DIR}/common/osd/PuHuiTi.ttf DESTINATION share)
file(GLOB ICON_FILES res/ui/*)
install(FILES ${ICON_FILES} DESTINATION share/res/ui/)
file(GLOB EIS_CONFIG_FILES video/rkeis_config_*)
install(FILES ${EIS_CONFIG_FILES} DESTINATION share)
