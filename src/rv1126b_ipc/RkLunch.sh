#!/bin/sh

rcS()
{
	for i in /oem/usr/etc/init.d/S??* ;do

		# Ignore dangling symlinks (if any).
		[ ! -f "$i" ] && continue

		case "$i" in
			*.sh)
				# Source shell script for speed.
				(
					trap - INT QUIT TSTP
					set start
					. $i
				)
				;;
			*)
				# No sh extension, so fork subprocess.
				$i start
				;;
		esac
	done
}

check_linker()
{
        [ ! -L "$2" ] && ln -sf $1 $2
}

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
	# ifconfig eth0 up && udhcpc -i eth0
	ifconfig eth0 192.168.1.168 netmask 255.255.255.0
	route add default gw 192.168.1.1
	echo "nameserver 202.96.128.86" > /etc/resolv.conf
	echo "nameserver 202.96.134.133" >> /etc/resolv.conf
	ifconfig lo up
	ifconfig lo 127.0.0.1
}

ssh_init()
{
	# 开启ssh服务
	ssh_user=`cat /etc/passwd | grep "sshd"`

	echo $ssh_user
	if [ -z "$ssh_user" ]; then
		echo "sshd:x:74:74:Privilege-separated SSH:/var/empty/sshd:/sbin/nologin" >> /etc/passwd
	fi

	if [ ! -d "/var/empty/sshd" ]; then
		rm -rf /var/empty
		mkdir -p /var/empty/sshd
	fi

	/usr/bin/sshd -f /etc/sshd_config
}

gpio_init() {
	# 初始化GPIO
    echo 5   > /sys/class/gpio/export
	echo 22  > /sys/class/gpio/export
    echo 25  > /sys/class/gpio/export
	echo 183 > /sys/class/gpio/export

    echo out > /sys/class/gpio/gpio5/direction
	echo out > /sys/class/gpio/gpio22/direction
	echo out > /sys/class/gpio/gpio25/direction
	echo out > /sys/class/gpio/gpio183/direction
}

gpio_deinit(){
	# 反初始化GPIO
    echo 183 > /sys/class/gpio/unexport
	echo 25  > /sys/class/gpio/unexport
	echo 22  > /sys/class/gpio/unexport
	echo 5   > /sys/class/gpio/unexport
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

	# if ko exist, install ko first
	default_ko_dir=/ko
	if [ -f "/oem/usr/ko/insmod_ko.sh" ];then
		default_ko_dir=/oem/usr/ko
	fi
	if [ -f "$default_ko_dir/insmod_ko.sh" ];then
		cd $default_ko_dir && sh insmod_ko.sh && cd -
	fi

	network_init &
	ssh_init &
	gpio_init &
	check_linker /userdata   /oem/usr/www/userdata
	check_linker /media/usb0 /oem/usr/www/usb0
	check_linker /mnt/sdcard /oem/usr/www/sdcard
	# if /data/rkipc not exist, cp /usr/share
	rkipc_ini=/userdata/rkipc.ini
	default_rkipc_ini=/tmp/rkipc-factory-config.ini

	if [ ! -f "/oem/usr/share/rkipc.ini" ]; then
		resolution=$(grep -o "Size:[0-9]*x[0-9]*" /proc/rkisp-vir0 | cut -d':' -f2)
		if [ "$resolution" = "2688x1520" ] ;then
			ln -s -f /oem/usr/share/rkipc-2688x1520.ini $default_rkipc_ini
		fi
		if [ "$resolution" = "3200x1800" ] ;then
			ln -s -f /oem/usr/share/rkipc-3200x1800.ini $default_rkipc_ini
		fi
		if [ "$resolution" = "3840x2160" ] ;then
			ln -s -f /oem/usr/share/rkipc-3840x2160.ini $default_rkipc_ini
		fi
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
	if [ ! -s "$rkipc_ini" ]; then
		cp $default_rkipc_ini $rkipc_ini -f
	fi

	# avoid MIPI screen not displaying when startup
	export rt_vo_disable_vop=0

	if [ ! -f "/userdata/image.bmp" ]; then
		cp -fa /oem/usr/share/image.bmp /userdata/
	fi

	# if [ -d "/oem/usr/share/iqfiles" ];then
	# 	rkipc -a /oem/usr/share/iqfiles &
	# else
	# 	rkipc &
	# fi

	sleep 1 # avoid rockti dumpsys connect fail

	# swap sdi0 and sdi1
	rk_mpi_amix_test --control "SAI2 Receive PATH0 Source Select" --value "From SDI1"
	rk_mpi_amix_test --control "SAI2 Receive PATH1 Source Select" --value "From SDI0"
	if [ -f "/oem/usr/share/speaker_test.wav" ];then
		rk_mpi_ao_test -i /oem/usr/share/speaker_test.wav --sound_card_name=default --device_ch=2 --device_rate=8000 --input_rate=8000 --input_ch=2 --set_volume 50
	fi
}

ulimit -c unlimited
echo "/data/core-%p-%e" > /proc/sys/kernel/core_pattern

post_chk &

sleep 5

rcS # fcgi and nginx
