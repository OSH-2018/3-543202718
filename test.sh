#! /bin/bash
cd mountpoint
ls -al
echo helloworld > test2
ls -l test2 # 查看是否成功创建文件
cat test2 # 测试写入和读取是否相同
dd if=/dev/zero of=test2 bs=1M count=250
ls -l test2 # 测试250MiB大文件写入
dd if=/dev/urandom of=test2 bs=1M count=1 seek=10
ls -l test2 # 此时应为11MiB
dd if=test2 of=/dev/null # 测试文件读取
rm test2
ls -al # testfile是否成功删除
cd ..
dd if=/dev/urandom of=test1 bs=1M count=100 # 创建随机数文件
tar -cf test1.tar test1 # 打包压缩
cp test1.tar mountpoint # 复制到文件系统
cd mountpoint
tar -xf test1.tar # 解压缩
md5sum test1 # 计算md5
rm test1
rm test1.tar
cd ..
md5sum test1 # 计算md5
rm test1
rm test1.tar
