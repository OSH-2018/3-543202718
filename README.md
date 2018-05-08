# OSHFS
## 综述
我实现的文件系统主要是在示例程序的基础上修改而成，补全了删除功能，并对修改和读取功能做了一定改变，对于其他函数也在细节上有一定改动。源文件是oshfs.c。
* 文件系统大小为256MB
* 文件名不多于31字节
* 支持文件最大内容为255.25MB
* 最多支持16384个文件

测试时，可以使用下列指令(最好在特权模式下)：
> ./make.sh -g\
> ./test.sh

测试结束后，可以取消挂载:
> sudo umount -l mountpoint
## 文件节点
文件节点和示例程序基本相同，也是使用链表实现。但为了支持以块为单位的存储，对content属性的类型做了改变，并新增了几个属性。对于文件名，增加了长度的限制。
```C
struct filenode {
    int amount;//文件占用的块数,4字节
    int num;//文件节点对应的内存块编号,4字节
    char filename[32];//文件名，要求不多于31字节（一个\0）
    struct stat st;//文件属性（定义在sys/stat.h中）,占用144字节
    struct filenode *next;//指向下一个节点的指针，8字节
    int content[16336];//指向内容的虚拟指针,支持最大的内容为255.25MB
};
```
修改后的文件节点大小刚好为16KB，和块大小相等，也就是说每一个文件的属性和指向内容的指针都存储在一个块里。
## 内存管理
我设置的总内存是256MB，内存被分成16k个块，每个块的大小是16KB。文件节点中存储着指向内容的所有块的虚拟指针，通过文件节点可以随机访问文件的任意内容。
```C
static const size_t size =256 * 1024 * (size_t)1024;//size = 256MB
static const size_t blocksize = 16 * (size_t)1024;//blocksize = 16KB
static const int blocknr =16 * 1024;//blocknr = 16k
static void *mem[16*1024];//内存块
```
对于内存的分配而言，块是基本的单位。对块的分配采用了一个极其朴素的算法，遍历所有的块，找到第一个空闲的块，做内存映射，并返回块的地址（实际是mem的下标）。如果要分配多个块，就要多次执行这个函数。在这里，我使用了一个优化，用全局变量blockused记录了一个整数值，确保在这个数之前的所有块都已经被分配。事实上，由于空闲块大多是连续的，多次执行该函数分配的块很可能是连续的，使用该优化可以大大减少分配时间。
```C
int balloc()//块分配
{
	int i;
	for (i=blockused;i<blocknr;i++)	{
		if (mem[i]==NULL) {
			mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);//映射一块空间
			break;
		}
	}
	if (i>=blocknr) i=-1;//所有的块都是满的，没有空闲的块
	if (i+1>blockused) blockused=i+1;//修改blockused
	return i;
}
```
对块的释放更加简单，munmap这个块，并将mem设置成NULL就可以了。
```C
void bfree(int k)//块释放
{
	munmap(mem[k], blocksize);//解除映射
	mem[k]=NULL;
	if (k<blockused) blockused=k;//修改blockused
}
```
## 文件读写
根据偏移量找到开始的点，一块一块的进行memcpy，只要注意内存复制的大小、进行下一块复制的缓冲区地址就可以了。
```C
static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)//修改文件内容（支持追加）
{
	struct filenode *node = get_filenode(path);
	int i,j,k,m,n,temp,sum;
	node->st.st_size = offset + size;//修改文件大小
	n=(offset+size-1)/blocksize+1;//计算新的大小所需要的块数（取上整）
	k=ralloc(node,n);//重定向文件内容指针
	if (k<0) {
		puts("No enough space.Modification failed.");
		return -1;
	}//错误处理 
	m=offset/blocksize;//偏移位置所在的块
	offset=offset%blocksize;//块内偏移量
	sum=0;//已经复制的字节数
	while (size>sum){
		i=node->content[m];
		if (size-sum>blocksize-offset) k=blocksize-offset;
		else k=size-sum;//计算当前块要复制的字节数
		memcpy(mem[i]+offset,buf+sum,k);
		sum+=k;
		offset=0;
		m++;
	}
	return size;
}
```
文件读取也是类似的。
## 复杂度分析
记块的总数为m，涉及（读写、重新分配等）的块数为n，块的大小为blocksize。
* 块分配 O(m)
* 块释放 O(1)
* 块的重新分配（需要增加块） O(m+n) 
* 块的重新分配（需要减少块） O(n) 
* 块的读写 O(n*blocksize)
## 问题
我的文件系统可以执行test.sh中的指令，得到的结果都符合预期。但如果写入超过64MB的文件，就会报错(这个时候的仍然有超过10000个空闲块)。
> unlock_path : node -> treelock != 0 failed
```sh
#! /bin/bash
cd mountpoint
ls -al
echo helloworld > testfile
ls -l testfile # 查看是否成功创建文件
cat testfile # 测试写入和读取是否相同
dd if=/dev/zero of=testfile bs=1M count=50
ls -l testfile # 测试50MiB大文件写入
dd if=/dev/urandom of=testfile bs=1M count=1 seek=10
ls -l testfile # 此时应为11MiB
dd if=testfile of=/dev/null # 测试文件读取
rm testfile
ls -al # testfile是否成功删除
```
## 附录
mmap函数说明:

void* mmap(void* start,size_t length,int prot,int flags,int fd,off_t offset);
* start：映射区的开始地址，设置为0时表示由系统决定映射区的起始地址。 
* length：映射区的长度。//长度单位是 以字节为单位，不足一内存页按一内存页处理 
* prot：期望的内存保护标志，不能与文件的打开模式冲突。是以下的某个值，可以通过or运算合理地组合在一起 
	* PROT_EXEC //页内容可以被执行 
	* PROT_READ //页内容可以被读取 
	* PROT_WRITE //页可以被写入 
	* PROT_NONE //页不可访问 
* flags：指定映射对象的类型，映射选项和映射页是否可以共享。它的值可以是一个或者多个以下位的组合体 
	* MAP_FIXED //使用指定的映射起始地址，如果由start和len参数指定的内存区重叠于现存的映射空间，重叠部分将会被丢弃。如果指定的起始地址不可用，操作将会失败。并且起始地址必须落在页的边界上。 
	* MAP_SHARED //与其它所有映射这个对象的进程共享映射空间。对共享区的写入，相当于输出到文件。直到msync()或者munmap()被调用，文件实际上不会被更新。 
	* MAP_PRIVATE //建立一个写入时拷贝的私有映射。内存区域的写入不会影响到原文件。这个标志和以上标志是互斥的，只能使用其中一个。 
	* MAP_DENYWRITE //这个标志被忽略。 
	* MAP_EXECUTABLE //同上 
	* MAP_NORESERVE //不要为这个映射保留交换空间。当交换空间被保留，对映射区修改的可能会得到保证。当交换空间不被保留，同时内存不足，对映射区的修改会引起段违例信号。 
	* MAP_LOCKED //锁定映射区的页面，从而防止页面被交换出内存。 
	* MAP_GROWSDOWN //用于堆栈，告诉内核VM系统，映射区可以向下扩展	
	* MAP_ANONYMOUS //匿名映射，映射区不与任何文件关联。 
	* MAP_ANON //MAP_ANONYMOUS的别称，不再被使用。 
	* MAP_FILE //兼容标志，被忽略。 
	* MAP_32BIT //将映射区放在进程地址空间的低2GB，MAP_FIXED指定时会被忽略。当前这个标志只在x86-64平台上得到支持。 
	* MAP_POPULATE //为文件映射通过预读的方式准备好页表。随后对映射区的访问不会被页违例阻塞。 
	* MAP_NONBLOCK //仅和MAP_POPULATE一起使用时才有意义。不执行预读，只为已存在于内存中的页面建立页表入口。 
* fd：有效的文件描述词。一般是由open()函数返回，其值也可以设置为-1，此时需要指定flags参数中的MAP_ANON,表明进行的是匿名映射。 
* off_toffset：被映射对象内容的起点。

(上述内容摘自百度百科)