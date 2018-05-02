if mount|grep mountpoint -n;then
sudo umount -l mountpoint
fi
if [ $1x == "-g"x ]; then
rm -r mountpoint
gcc -D_FILE_OFFSET_BITS=64 -o oshfs oshfs.c -lfuse
mkdir mountpoint
./oshfs mountpoint
echo "mount succeed"
fi
