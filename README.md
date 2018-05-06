# OSHFS
## 综述
我实现的文件系统主要是在示例程序的基础上修改而成，补全了删除功能，并对修改和读取功能做了一定改变，对于其他函数也在细节上有一定改动。源文件是oshfs.c。
## 文件节点
文件节点和示例程序基本相同，也是使用链表实现。但为了支持以块为单位的存储，对content属性的类型做了改变，并新增了几个属性。对于文件名，增加了长度的限制。
```C
struct filenode {
    int amount;//文件占用的块数,4字节
    int num;//文件节点对应的内存块编号,4字节
    char filename[32];//文件名，要求不多于32字节
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
对于内存的分配而言，块是基本的单位。对块的分配采用了一个极其朴素的算法，遍历所有的块，找到第一个空闲的块，做内存映射，并返回块的地址（实际是mem的下标）。如果要分配多个块，就要多次执行这个函数（或许可以做一个修改，再传入一个参数，从这个参数开始寻找空闲块，即默认之前的块都是已经使用的，可以把O(mn)的时间复杂度降低到O(m+n)）。
```C
int balloc()//块分配
{
	int i;
	for (i=0;i<blocknr;i++)	{
		if (mem[i]==NULL) {
			mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);//映射一块空间
			break;
		}
	}
	if (i>=blocknr) i=-1;//所有的块都是满的，没有空闲的块
	return i;
}
```
对块的释放更加简单，munmap这个块，并将mem设置成NULL就可以了。
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