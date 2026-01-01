#!/bin/sh

network_init()
{
	ethaddr1=`ifconfig -a | grep "eth.*HWaddr" | awk '{print $5}'`

	if [ -f /data/ethaddr.txt ]; then
		ethaddr2=`cat /data/ethaddr.txt`
		if [ $ethaddr1 == $ethaddr2 ]; then
			echo "eth HWaddr cfg ok"
		else
			ifconfig eth0 down
			ifconfig eth0 hw ether $ethaddr2
		fi
	else
		echo $ethaddr1 > /data/ethaddr.txt
	fi
	ifconfig eth0 up && udhcpc -i eth0
}

# Define a function to get the interrupt number and bind it to CPU3
bind_interrupt_to_cpu3() {
    local interrupt_line=$1
    local interrupt_number=$(echo $interrupt_line | awk '{print $1}' | sed 's/:$//')
    local cpu_mask=$((8)) # CPU3 bitmask, assuming CPU numbers start from 0

    # Check if the interrupt number is valid
    if [[ -n $interrupt_number ]]; then
        # Check if the interrupt binding file exists
        if [ -f "/proc/irq/$interrupt_number/smp_affinity" ]; then
            # Write the CPU3 bitmask to the interrupt binding file
            echo $cpu_mask > "/proc/irq/$interrupt_number/smp_affinity"
            echo "Interrupt number $interrupt_number has been bound to CPU3"
        else
            echo "Interrupt number $interrupt_number binding file does not exist"
        fi
    else
        echo "Invalid interrupt number: $interrupt_number"
    fi
}

post_chk()
{
	#TODO: ensure /userdata mount done
	cnt=0
	while [ $cnt -lt 30 ];
	do
		cnt=$(( cnt + 1 ))
		if mount | grep -w userdata; then
			break
		fi
		sleep .1
	done

	network_init &
	# if ko exist, install ko first
	default_ko_dir=/ko
	if [ -f "/usr/lib/module/insmod_ko.sh" ];then
		default_ko_dir=/usr/lib/module
	fi
	if [ -f "$default_ko_dir/insmod_ko.sh" ];then
		cd $default_ko_dir && sh insmod_ko.sh && cd -
	fi

	# if /data/rkipc not exist, cp /usr/share
	rkipc_ini=/userdata/rkipc.ini
	default_rkipc_ini=/tmp/rkipc-factory-config.ini

	ls /dev/media* | xargs -i media-ctl -pd {} | grep sc850sl
	if [ $? -eq 0 ] ;then
		ln -s -f /usr/share/rkipc.ini $default_rkipc_ini
	fi
	ls /dev/media* | xargs -i media-ctl -pd {} | grep gc8613
	if [ $? -eq 0 ] ;then
		ln -s -f /usr/share/rkipc-gc8613.ini $default_rkipc_ini
	fi
	ls /dev/media* | xargs -i media-ctl -pd {} | grep imx586
	if [ $? -eq 0 ] ;then
		ln -s -f /usr/share/rkipc-imx586.ini $default_rkipc_ini
	fi
	tmp_md5=/tmp/.rkipc-ini.md5sum
	data_md5=/userdata/.rkipc-default.md5sum
	md5sum $default_rkipc_ini > $tmp_md5
	chk_rkipc=`cat $tmp_md5|awk '{print $1}'`
	rm $tmp_md5
	if [ ! -f $data_md5 ];then
		md5sum $default_rkipc_ini > $data_md5
	fi
	grep -w $chk_rkipc $data_md5
	if [ $? -ne 0 ] ;then
		rm -f $rkipc_ini
		echo "$chk_rkipc" > $data_md5
	fi

	if [ ! -f "$default_rkipc_ini" ];then
		echo "Error: not found rkipc.ini !!!"
		exit -1
	fi
	if [ ! -f "$rkipc_ini" ]; then
		cp $default_rkipc_ini $rkipc_ini -f
	fi

	# avoid MIPI screen not displaying when startup
	export rt_vo_disable_vop=0

	killall irqbalance

	# Get interrupt numbers and bind them to CPU3, Cannot bind ISP interrupt, otherwise imu pts will fluctuate greatly
	interrupt_lines=$(cat /proc/interrupts | grep -E 'rkcifhw|spi|inv_mpu')
	echo "$interrupt_lines" | while read -r line; do
		bind_interrupt_to_cpu3 "$line"
	done
	# Disable CPU3 idle
	echo 1 > /sys/devices/system/cpu/cpu3/cpuidle/state1/disable

	echo 800 > /sys/bus/iio/devices/iio:device1/in_anglvel_sampling_frequency
	echo 800 > /sys/bus/iio/devices/iio:device1/in_accel_sampling_frequency
	echo 0.004785 > /sys/bus/iio/devices/iio:device1/in_accel_scale
	echo 0.001064225 > /sys/bus/iio/devices/iio:device1/in_anglvel_scale
	echo 121 > /sys/bus/iio/devices/iio:device1/in_accel_lpf_bw
	echo 0 > /sys/bus/iio/devices/iio:device1/in_anglvel_lpf_bw

	# swap sdi0 and sdi1
	rk_mpi_amix_test --control "SAI2 Receive PATH0 Source Select" --value "From SDI1"
	rk_mpi_amix_test --control "SAI2 Receive PATH1 Source Select" --value "From SDI0"

	export rt_vi_rawstream_en=0
	export test_vendor_rkeis_loglevel=0 # when 60fps, frames will lost if rkeis printf log
	echo userspace > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
	echo 1608000 > /sys/devices/system/cpu/cpufreq/policy0/scaling_setspeed
	# safe value for rv1126b venc
	echo 550000000 > /proc/mpp_service/rkvenc0/clk_core
	# export file_cache_env=1
	# Warning: Modified Qos just for test
	# io -4 0x20310008 0x404 #cpu
	# io -4 0x20330408 0x0 #sdmmc0

	# sdcard write performance tuning
	echo none > /sys/block/mmcblk1/queue/scheduler
	echo 30 > /proc/sys/vm/dirty_ratio
	echo 20 > /proc/sys/vm/dirty_background_ratio
	# FIXME: disable dummy buffer to avoid memory leak
	echo 0 > /sys/devices/platform/rkcif-mipi-lvds/is_use_dummybuf
	rkipc &
}

ulimit -c unlimited
echo "/data/core-%p-%e" > /proc/sys/kernel/core_pattern

post_chk &
