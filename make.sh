#! /bin/bash
if mount|grep mountpoint -n;then  # 解除挂载
sudo umount -l mountpoint
fi
if [ $1x == "-f"x ]; then # 以前台模式运行程序
rm -r mountpoint
gcc -D_FILE_OFFSET_BITS=64 -o oshfs oshfs.c -lfuse
mkdir mountpoint
./oshfs -f mountpoint
elif [ $1x == "-g"x ]; then # 以后台模式运行程序
rm -r mountpoint
gcc -D_FILE_OFFSET_BITS=64 -o oshfs oshfs.c -lfuse
mkdir mountpoint
./oshfs  mountpoint
elif [ $1x == "-u"x ]; then # 清理残余文件
rm -r mountpoint
fi
