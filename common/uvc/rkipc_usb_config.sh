#!/bin/sh

# UVC_MULTI_OPTIONS: Defines the options for UVC_MULTI variable.
# Available options: NO (close), ONE (Expand a device), TWO (Expand two devices)
UVC_MULTI=NO
CDC_ENABLE=NO
ADB_ENABLE=YES
USB_FUNCTIONS_DIR=/sys/kernel/config/usb_gadget/rockchip/functions
USB_CONFIGS_DIR=/sys/kernel/config/usb_gadget/rockchip/configs/b.1
USB_FUNCTIONS_CNT=1
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  -f FORMAT (YUYV/MJPEG/H.264/H.265)"
    echo "  -w WIDTH"
    echo "  -h HEIGHT"
    echo "  -a Enable UAC2 audio"
    echo "example:"
    echo "  $0 -f H.264 -w 1920 -h 1080"
    echo "  $0 -f H.264 -w 1920 -h 1080 -a"
    echo "  $0 -a"
}

# parse parameters
ENABLE_UAC2=false
while getopts "f:w:h:a" opt; do
    case $opt in
        f) FORMAT="$OPTARG" ;;
        w) WIDTH="$OPTARG" ;;
        h) HEIGHT="$OPTARG" ;;
        a) ENABLE_UAC2=true ;;
        ?) usage; exit 1 ;;
    esac
done

#Process the remaining non optional parameters
shift $((OPTIND-1))
for arg in "$@"; do
    case "$arg" in
        uac2) ENABLE_UAC2=true ;;
        *) usage; exit 1 ;;
    esac
done

syslink_function()
{
	ln -s ${USB_FUNCTIONS_DIR}/$1 ${USB_CONFIGS_DIR}/f${USB_FUNCTIONS_CNT}
	let USB_FUNCTIONS_CNT=USB_FUNCTIONS_CNT+1
}

configure_uvc_resolution_nv12()
{
   UVC_DISPLAY_W=$1
   UVC_DISPLAY_H=$2
   UVC_DISPLAY_DIR=${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/uncompressed/u/${UVC_DISPLAY_W}_${UVC_DISPLAY_H}p
   mkdir ${UVC_DISPLAY_DIR}
   echo $UVC_DISPLAY_W > ${UVC_DISPLAY_DIR}/wWidth
   echo $UVC_DISPLAY_H > ${UVC_DISPLAY_DIR}/wHeight
   echo 333333 > ${UVC_DISPLAY_DIR}/dwDefaultFrameInterval
   echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*20)) > ${UVC_DISPLAY_DIR}/dwMinBitRate
   echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*20)) > ${UVC_DISPLAY_DIR}/dwMaxBitRate
   echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*2)) > ${UVC_DISPLAY_DIR}/dwMaxVideoFrameBufferSize
   echo -e "333333\n666666\n1000000\n2000000" > ${UVC_DISPLAY_DIR}/dwFrameInterval
   echo 12 > ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/uncompressed/u/bBitsPerPixel
   echo -ne \\x4e\\x56\\x31\\x32\\x00\\x00\\x10\\x00\\x80\\x00\\x00\\xaa\\x00\\x38\\x9b\\x71 > ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/uncompressed/u/guidFormat
}

configure_uvc_resolution_yuyv()
{
    UVC_DISPLAY_W=$1
    UVC_DISPLAY_H=$2
    UVC_DISPLAY_DIR=${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/uncompressed/u/${UVC_DISPLAY_W}_${UVC_DISPLAY_H}p
    mkdir ${UVC_DISPLAY_DIR}
    echo $UVC_DISPLAY_W > ${UVC_DISPLAY_DIR}/wWidth
    echo $UVC_DISPLAY_H > ${UVC_DISPLAY_DIR}/wHeight
    echo 333333 > ${UVC_DISPLAY_DIR}/dwDefaultFrameInterval
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*20)) > ${UVC_DISPLAY_DIR}/dwMinBitRate
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*20)) > ${UVC_DISPLAY_DIR}/dwMaxBitRate
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*2)) > ${UVC_DISPLAY_DIR}/dwMaxVideoFrameBufferSize
    echo -e "333333\n666666\n1000000\n2000000" > ${UVC_DISPLAY_DIR}/dwFrameInterval
    echo 16 > ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/uncompressed/u/bBitsPerPixel
    echo -ne \\x59\\x55\\x59\\x32\\x00\\x00\\x10\\x00\\x80\\x00\\x00\\xaa\\x00\\x38\\x9b\\x71 > ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/uncompressed/u/guidFormat
}

configure_uvc_resolution_yuyv_720p()
{
    UVC_DISPLAY_W=$1
    UVC_DISPLAY_H=$2
    UVC_DISPLAY_DIR=${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/uncompressed/u/${UVC_DISPLAY_W}_${UVC_DISPLAY_H}p
    mkdir ${UVC_DISPLAY_DIR}
    echo $UVC_DISPLAY_W > ${UVC_DISPLAY_DIR}/wWidth
    echo $UVC_DISPLAY_H > ${UVC_DISPLAY_DIR}/wHeight
    echo 1000000 > ${UVC_DISPLAY_DIR}/dwDefaultFrameInterval
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*20)) > ${UVC_DISPLAY_DIR}/dwMinBitRate
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*20)) > ${UVC_DISPLAY_DIR}/dwMaxBitRate
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*2)) > ${UVC_DISPLAY_DIR}/dwMaxVideoFrameBufferSize
    echo -e "1000000\n2000000" > ${UVC_DISPLAY_DIR}/dwFrameInterval
}

configure_uvc_resolution_yuyv_1080p()
{
    UVC_DISPLAY_W=$1
    UVC_DISPLAY_H=$2
    UVC_DISPLAY_DIR=${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/uncompressed/u/${UVC_DISPLAY_W}_${UVC_DISPLAY_H}p
    mkdir ${UVC_DISPLAY_DIR}
    echo $UVC_DISPLAY_W > ${UVC_DISPLAY_DIR}/wWidth
    echo $UVC_DISPLAY_H > ${UVC_DISPLAY_DIR}/wHeight
    echo 2500000 > ${UVC_DISPLAY_DIR}/dwDefaultFrameInterval
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*20)) > ${UVC_DISPLAY_DIR}/dwMinBitRate
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*20)) > ${UVC_DISPLAY_DIR}/dwMaxBitRate
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*2)) > ${UVC_DISPLAY_DIR}/dwMaxVideoFrameBufferSize
    echo -e "2500000\n5000000" > ${UVC_DISPLAY_DIR}/dwFrameInterval
}

configure_uvc_resolution_mjpeg()
{
    UVC_DISPLAY_W=$1
    UVC_DISPLAY_H=$2
    UVC_DISPLAY_DIR=${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/mjpeg/m/${UVC_DISPLAY_W}_${UVC_DISPLAY_H}p
    mkdir ${UVC_DISPLAY_DIR}
    echo $UVC_DISPLAY_W > ${UVC_DISPLAY_DIR}/wWidth
    echo $UVC_DISPLAY_H > ${UVC_DISPLAY_DIR}/wHeight
    echo 333333 > ${UVC_DISPLAY_DIR}/dwDefaultFrameInterval
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*20)) > ${UVC_DISPLAY_DIR}/dwMinBitRate
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*20)) > ${UVC_DISPLAY_DIR}/dwMaxBitRate
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*2)) > ${UVC_DISPLAY_DIR}/dwMaxVideoFrameBufferSize
    echo -e "333333\n666666\n1000000\n2000000" > ${UVC_DISPLAY_DIR}/dwFrameInterval
}
configure_uvc_resolution_h264()
{
    UVC_DISPLAY_W=$1
    UVC_DISPLAY_H=$2
    UVC_DISPLAY_DIR=${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/framebased/f1/${UVC_DISPLAY_W}_${UVC_DISPLAY_H}p
    mkdir ${UVC_DISPLAY_DIR}
    echo $UVC_DISPLAY_W > ${UVC_DISPLAY_DIR}/wWidth
    echo $UVC_DISPLAY_H > ${UVC_DISPLAY_DIR}/wHeight
    echo 333333 > ${UVC_DISPLAY_DIR}/dwDefaultFrameInterval
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*10)) > ${UVC_DISPLAY_DIR}/dwMinBitRate
    echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*10)) > ${UVC_DISPLAY_DIR}/dwMaxBitRate
    echo -e "333333\n400000\n500000\n666666\n1000000\n2000000" > ${UVC_DISPLAY_DIR}/dwFrameInterval
    echo -ne \\x48\\x32\\x36\\x34\\x00\\x00\\x10\\x00\\x80\\x00\\x00\\xaa\\x00\\x38\\x9b\\x71 > ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/framebased/f1/guidFormat
}
configure_uvc_resolution_h265()
{
	UVC_DISPLAY_W=$1
	UVC_DISPLAY_H=$2
	UVC_DISPLAY_DIR=${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/framebased/f2/${UVC_DISPLAY_W}_${UVC_DISPLAY_H}p
	mkdir ${UVC_DISPLAY_DIR}
	echo $UVC_DISPLAY_W > ${UVC_DISPLAY_DIR}/wWidth
	echo $UVC_DISPLAY_H > ${UVC_DISPLAY_DIR}/wHeight
	echo 333333 > ${UVC_DISPLAY_DIR}/dwDefaultFrameInterval
	echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*10)) > ${UVC_DISPLAY_DIR}/dwMinBitRate
	echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*10)) > ${UVC_DISPLAY_DIR}/dwMaxBitRate
	echo -e "333333\n400000\n500000\n666666" > ${UVC_DISPLAY_DIR}/dwFrameInterval
	echo -ne \\x48\\x32\\x36\\x35\\x00\\x00\\x10\\x00\\x80\\x00\\x00\\xaa\\x00\\x38\\x9b\\x71 > ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/framebased/f2/guidFormat
}

cdc_device_config()
{
	mkdir ${USB_FUNCTIONS_DIR}/acm.g0
   syslink_function acm.g0
}

adb_device_config()
{
  mkdir ${USB_FUNCTIONS_DIR}/ffs.adb
  syslink_function ffs.adb
  pre_run_adb
  sleep .5
}

uvc_device_config()
{
  UVC_GS=$1
  UVC_NAME=$2
  FORMAT=$3
  WIDTH=$4
  HEIGHT=$5
  echo "UVC Config Parameters:"
  echo "  UVC_GS: $UVC_GS"
  echo "  UVC_NAME: $UVC_NAME"
  echo "  FORMAT: $FORMAT"
  echo "  WIDTH: $WIDTH"
  echo "  HEIGHT: $HEIGHT"

  mkdir ${USB_FUNCTIONS_DIR}/$UVC_GS -m 0770
  echo $UVC_NAME > ${USB_FUNCTIONS_DIR}/$UVC_GS/device_name
  echo $UVC_NAME > ${USB_FUNCTIONS_DIR}/$UVC_GS/function_name
  if [ $UVC_MULTI = ONE ];then
     echo 2048 > ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming_maxpacket
  elif [ $UVC_MULTI = TWO ];then
     echo 1024 > ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming_maxpacket
  else
     echo 3072 > ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming_maxpacket
  fi
  echo 2 > ${USB_FUNCTIONS_DIR}/$UVC_GS/uvc_num_request
  #echo 1 > /sys/kernel/config/usb_gadget/rockchip/functions/$UVC_GS/streaming_bulk

  mkdir ${USB_FUNCTIONS_DIR}/$UVC_GS/control/header/h
  ln -s ${USB_FUNCTIONS_DIR}/$UVC_GS/control/header/h ${USB_FUNCTIONS_DIR}/$UVC_GS/control/class/fs/h
  ln -s ${USB_FUNCTIONS_DIR}/$UVC_GS/control/header/h ${USB_FUNCTIONS_DIR}/$UVC_GS/control/class/ss/h

  mkdir /sys/kernel/config/usb_gadget/rockchip/functions/$UVC_GS/streaming/header/h

  case "$FORMAT" in
    "YUYV")
      mkdir /sys/kernel/config/usb_gadget/rockchip/functions/$UVC_GS/streaming/uncompressed/u
      configure_uvc_resolution_yuyv $WIDTH $HEIGHT
      ln -s ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/uncompressed/u ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/header/h/u
      ;;
    "MJPEG")
      mkdir ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/mjpeg/m
      configure_uvc_resolution_mjpeg $WIDTH $HEIGHT
      ln -s ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/mjpeg/m ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/header/h/m
      ;;
    "H.264")
      mkdir ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/framebased/f1
      configure_uvc_resolution_h264 $WIDTH $HEIGHT
      ln -s ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/framebased/f1 ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/header/h/f1
      ;;
    "H.265")
      mkdir ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/framebased/f2
      configure_uvc_resolution_h265 $WIDTH $HEIGHT
      ln -s ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/framebased/f2 ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/header/h/f2
      ;;
    "NV12")
      mkdir /sys/kernel/config/usb_gadget/rockchip/functions/$UVC_GS/streaming/uncompressed/u
      configure_uvc_resolution_nv12 $WIDTH $HEIGHT
      ln -s ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/uncompressed/u ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/header/h/u
      ;;
  esac

  ln -s ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/header/h ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/class/fs/h
  ln -s ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/header/h ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/class/hs/h
  ln -s ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/header/h ${USB_FUNCTIONS_DIR}/$UVC_GS/streaming/class/ss/h
}
uac1_device_config()
{
  UAC=$1
  mkdir ${USB_FUNCTIONS_DIR}/${UAC}.gs0
  UAC_GS0=${USB_FUNCTIONS_DIR}/${UAC}.gs0
  echo 3 > ${UAC_GS0}/p_chmask
  echo 2 > ${UAC_GS0}/p_ssize
  echo 1 > ${UAC_GS0}/p_mute_present
  echo 1 > ${UAC_GS0}/p_volume_present
  echo -5120 > ${UAC_GS0}/p_volume_min #-20db min must > -96db
  echo 8000,16000,44100,48000 > ${UAC_GS0}/p_srate

  echo 3 > ${UAC_GS0}/c_chmask
  echo 2 > ${UAC_GS0}/c_ssize
  echo 4 > ${UAC_GS0}/req_number
  echo 1 > ${UAC_GS0}/c_mute_present
  echo 1 > ${UAC_GS0}/c_volume_present
  echo -3200 > ${UAC_GS0}/c_volume_min #-12.5db
  echo 0 > ${UAC_GS0}/c_volume_max   #0db
  echo 32 > ${UAC_GS0}/c_volume_res #0.125db
  echo 8000,16000,44100,48000 > ${UAC_GS0}/c_srate

  syslink_function ${UAC}.gs0
}
uac2_device_config()
{
  UAC=$1
  mkdir ${USB_FUNCTIONS_DIR}/${UAC}.gs0
  UAC_GS0=${USB_FUNCTIONS_DIR}/${UAC}.gs0
  echo 3 > ${UAC_GS0}/p_chmask
  echo 2 > ${UAC_GS0}/p_ssize
  echo 1 > ${UAC_GS0}/p_mute_present
  echo 1 > ${UAC_GS0}/p_volume_present
  echo -5120 > ${UAC_GS0}/p_volume_min #-20db min must > -96db
  # echo 8000,16000,44100,48000 > ${UAC_GS0}/p_srate
  echo 8000 > ${UAC_GS0}/p_srate

  echo 3 > ${UAC_GS0}/c_chmask
  echo 2 > ${UAC_GS0}/c_ssize
  echo 4 > ${UAC_GS0}/req_number
  echo 1 > ${UAC_GS0}/c_mute_present
  echo 1 > ${UAC_GS0}/c_volume_present
  echo -3200 > ${UAC_GS0}/c_volume_min #-12.5db
  echo 0 > ${UAC_GS0}/c_volume_max   #0db
  echo 32 > ${UAC_GS0}/c_volume_res #0.125db
  # echo 8000,16000,44100,48000 > ${UAC_GS0}/c_srate
  echo 8000 > ${UAC_GS0}/c_srate

  syslink_function ${UAC}.gs0
}
pre_run_rndis()
{
  RNDIS_STR="rndis"
  if ( echo $1 |grep -q "rndis" ); then
   #sleep 1
   IP_FILE=/data/uvc_xu_ip_save
   echo "config usb0 IP..."
   if [ -f $IP_FILE ]; then
      for line in `cat $IP_FILE`
      do
        echo "save ip is: $line"
        ifconfig usb0 $line
      done
   else
    ifconfig usb0 172.16.110.6
   fi
   ifconfig usb0 up
  fi
}

pre_run_adb()
{
   umount /dev/usb-ffs/adb
   mkdir -p /dev/usb-ffs/adb -m 0770
   mount -o uid=2000,gid=2000 -t functionfs adb /dev/usb-ffs/adb
   start-stop-daemon --start --quiet --background --exec /usr/bin/adbd
   mkdir /dev/pts -m 0770
   mount -t devpts none /dev/pts
}

##main

#init usb config
ifconfig lo up   # for adb ok
ifconfig lo 127.0.0.1
umount /sys/kernel/config
mkdir /dev/usb-ffs -m 0770
mount -t configfs none /sys/kernel/config
mkdir -p /sys/kernel/config/usb_gadget/rockchip -m 0770
mkdir -p /sys/kernel/config/usb_gadget/rockchip/strings/0x409 -m 0770
mkdir -p ${USB_CONFIGS_DIR}/strings/0x409 -m 0770
echo 0x2207 > /sys/kernel/config/usb_gadget/rockchip/idVendor
echo 0x0310 > /sys/kernel/config/usb_gadget/rockchip/bcdDevice
echo 0x0200 > /sys/kernel/config/usb_gadget/rockchip/bcdUSB
echo 239 > /sys/kernel/config/usb_gadget/rockchip/bDeviceClass
echo 2 > /sys/kernel/config/usb_gadget/rockchip/bDeviceSubClass
echo 1 > /sys/kernel/config/usb_gadget/rockchip/bDeviceProtocol

SERIAL_NUM=`cat /proc/cpuinfo |grep Serial | awk -F ":" '{print $2}'`
echo "serialnumber is $SERIAL_NUM"
echo $SERIAL_NUM > /sys/kernel/config/usb_gadget/rockchip/strings/0x409/serialnumber
echo "rockchip" > /sys/kernel/config/usb_gadget/rockchip/strings/0x409/manufacturer
echo "UVC" > /sys/kernel/config/usb_gadget/rockchip/strings/0x409/product

echo 0x1 > /sys/kernel/config/usb_gadget/rockchip/os_desc/b_vendor_code
echo "MSFT100" > /sys/kernel/config/usb_gadget/rockchip/os_desc/qw_sign
echo 500 > /sys/kernel/config/usb_gadget/rockchip/configs/b.1/MaxPower
#ln -s /sys/kernel/config/usb_gadget/rockchip/configs/b.1 /sys/kernel/config/usb_gadget/rockchip/os_desc/b.1

#Windows computers will remember the device by default.
#Changing the pid does not require re-uninstalling and loading the windows driver.
echo 0x0016 > /sys/kernel/config/usb_gadget/rockchip/idProduct
if [ $UVC_MULTI = ONE ];then
   echo 0x0018 > /sys/kernel/config/usb_gadget/rockchip/idProduct
elif [ $UVC_MULTI = TWO ];then
   echo 0x001A > /sys/kernel/config/usb_gadget/rockchip/idProduct
fi

##reset config,del default adb config
if [ -e ${USB_CONFIGS_DIR}/ffs.adb ]; then
   #for rk1808 kernel 4.4
   rm -f ${USB_CONFIGS_DIR}/ffs.adb
else
   ls ${USB_CONFIGS_DIR} | grep f[0-9] | xargs -I {} rm ${USB_CONFIGS_DIR}/{}
fi

case "$1" in
rndis)
    # config rndis
   mkdir ${USB_FUNCTIONS_DIR}/rndis.gs0
   syslink_function /rndis.gs0
   echo "config uvc and rndis..."
   ;;
uac1)
   uac1_device_config uac1
   echo "config uvc and uac1..."
   ;;
uac2)
   uac2_device_config uac2
   echo "config uvc and uac2..."
   ;;
uac1_rndis)
   #uac_device_config uac1
   mkdir ${USB_FUNCTIONS_DIR}/rndis.gs0
   syslink_function rndis.gs0
   uac1_device_config uac1
   echo "config uvc and uac1 rndis..."
   ;;
uac2_rndis)
   #uac_device_config uac2
   mkdir ${USB_FUNCTIONS_DIR}/rndis.gs0
   syslink_function rndis.gs0
   uac2_device_config uac2
   echo "config uvc and uac2 rndis..."
   ;;
*)
   echo "config uvc ..."
esac

if [ "$ENABLE_UAC2" = true ]; then
   uac2_device_config uac2
   echo "config uac2..."
fi

if [ -n "$FORMAT" ] && [ -n "$WIDTH" ] && [ -n "$HEIGHT" ]; then
   uvc_device_config uvc.gs1 "UVC RGB" $FORMAT $WIDTH $HEIGHT
   syslink_function uvc.gs1
fi

if [ $UVC_MULTI = ONE ];then
   syslink_function uvc.gs2
elif [ $UVC_MULTI = TWO ];then
   syslink_function uvc.gs2
   syslink_function uvc.gs3
fi

if [ "$CDC_ENABLE" = "YES" ];then
	cdc_device_config
fi

if [ "$ADB_ENABLE" = "YES" ];then
   adb_device_config
fi

UDC=`ls /sys/class/udc/| awk '{print $1}'`
echo $UDC > /sys/kernel/config/usb_gadget/rockchip/UDC

if [ "$1" ]; then
  pre_run_rndis $1
fi