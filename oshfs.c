#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>

struct filenode {
	char *filename;//文件名
	void *content;//指向内容的指针
	struct stat *st;//文件属性（定义在sys/stat.h中）
	struct filenode *next;//指向下一个节点的指针
};//文件节点以链表形式存在
struct block {
	int num;//对应的mem的下标
	int padding;//填充字段
	void *next;//如果若干个块是连续的，则指向下一个块
	char data[1];//虚拟字段，表示数据区的第一个字节（不计入结构体大小）
};//块属性,结构体大小为16字节
static const int meta = 16;
static const size_t size = 1024 * (size_t)1024;
static const size_t blocksize = (size_t)1024;
static const int blocknr = 1024;
static void *mem[1024];//内存块

static struct filenode *root = NULL;//根文件节点

void *balloc(int size)//块分配
{
	int n=size/1008+1;//分配数目（每个块的前12个字节用于记录块属性）
    int i=0,j=0;
    struct block temp;
	struct block *pre=NULL;
	struct block *root=NULL;
    for (i=0;i<n;i++){
        while (j<blocknr){
            if (mem[j]==NULL) {//寻找未分配的块
                mem[j] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                temp.num=j;
                temp.next=NULL;
				memcpy((struct block*)mem[j],&temp,meta);//编写块属性
				if (root==NULL) root=(struct block*)mem[j];//返回首个块
				if (pre) pre->next=(struct block*)mem[j];//修改前一个块的next属性
				pre=(struct block*) mem[j];
				break;
			}
			else j++;
		}
	}    
	return (void*)root->data;       
}
void blockfree(void* p)//块释放
{
	struct block *temp=(struct block*)(p-16);
	while (temp){
		int addr=temp->num;//记录当前块的编号
		temp=temp->next;//temp指向下一块		
		munmap(mem[addr], blocksize);//解除映射
		mem[addr]=NULL;//mem中标记为未分配
	}
}
void blockread(void* src,void* buf,int offset,int size)//块读取
{
	int n=size;//待复制的字节数
	int m=0;//缓冲区偏移量
	int off=offset;
	int k;
	struct block *temp=(struct block*)(src-16);
	while (n>0) {
		if (n+off<=1008){//最后一次复制
			memcpy(buf+m,temp->data+off,n);
			n=0;
			off=0;
		}
		else {//复制
			k=1008-off;
			memcpy(buf+m,temp->data+off,k);
			n-=k;
			m+=k;
			off=0;
			temp=temp->next;
		}
	}
}
void blockwrite(void* buf,void* dst,int offset,int size)//块写入
{
	int n=size;//待复制的字节数
	int m=0;//缓冲区偏移量
	int off=offset;
	int k;
	struct block *temp=(struct block*)(dst-16);
	while (n>0) {
		if (n+off<=1008){//最后一次复制
			memcpy(temp->data+off,buf+m,n);
			n=0;
			off=0;
		}
		else {//复制
			k=1008-off;
			memcpy(temp->data+off,buf+m,k);
			n-=k;
			m+=k;
			off=0;
			temp=temp->next;
		}
	}	
}
void *reballoc(void* p,int size)//块重新分配
{
	int n1=0,n2=size/1008+1;
	int i;
	struct block *temp1,*temp2,*root;
	temp1=(struct block*)p;
	while (temp1) {
		n1++;
		temp1=temp1->next;
	}//计算p连接的块数
	if (n1>=n2) {//如果要求的空间比已有的小，释放多余的空间
		temp1=(struct block*)p;
		for (i=1;i<n1;i++) temp1=temp1->next;
		blockfree(temp1);
		return p+16;
	}
	else {//如果要求的空间比已有的大,新建一块空间后进行内存复制
		root=balloc(size);
		temp1=(struct block*)p;
		temp2=(struct block*)root;
		while(temp1){
			memcpy((void*)temp2->data,(void*)temp1->data,1008);
			temp1=temp1->next;
			temp2=temp2->next;
		}
		return (void*)root->data;
	}	
}
static struct filenode *get_filenode(const char *name)//按照文件名获取文件节点
{
	struct filenode *node = root;
	while(node) {
		if(strcmp(node->filename, name + 1) != 0)//似乎是用来跳过开头的/符号
			node = node->next;
		else
			return node;
	}
	return NULL;
}

static void create_filenode(const char *filename, const struct stat *st)//创建文件节点
{
	struct filenode *new = (struct filenode *)balloc(sizeof(struct filenode));
	new->filename = (char *)balloc(strlen(filename) + 1);//+1是为了存放字符串末尾的\0标记
	memcpy(new->filename, filename, strlen(filename) + 1);//复制文件名
	new->st = (struct stat *)balloc(sizeof(struct stat));
	memcpy(new->st, st, sizeof(struct stat));//复制文件属性
	new->next = root;
	new->content = NULL;
	root = new;//采用头插法，新节点插在根节点之前
}

static void *oshfs_init(struct fuse_conn_info *conn)//内存初始化，函数提供了两种方法将所有的内存初始化为0
{	/*
	size_t blocknr = sizeof(mem) / sizeof(mem[0]);//文件块的数量64k
	size_t blocksize = size / blocknr;//文件块的大小64KB
	// Demo 1
	for(int i = 0; i < blocknr; i++) {
		mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		memset(mem[i], 0, blocksize);//内存初始化为0
	}
	for(int i = 0; i < blocknr; i++) {
		munmap(mem[i], blocksize);
	}//解除内存映射
	// Demo 2
	mem[0] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	for(int i = 0; i < blocknr; i++) {
		mem[i] = (char *)mem[0] + blocksize * i;
		memset(mem[i], 0, blocksize);
	}
	for(int i = 0; i < blocknr; i++) {
		munmap(mem[i], blocksize);
	}
*/
	return NULL;
}
/*mmap函数说明
void* mmap(void* start,size_t length,int prot,int flags,int fd,off_t offset);
start：映射区的开始地址，设置为0时表示由系统决定映射区的起始地址。 
length：映射区的长度。//长度单位是 以字节为单位，不足一内存页按一内存页处理 
prot：期望的内存保护标志，不能与文件的打开模式冲突。是以下的某个值，可以通过or运算合理地组合在一起 
	PROT_EXEC //页内容可以被执行 
	PROT_READ //页内容可以被读取 
	PROT_WRITE //页可以被写入 
	PROT_NONE //页不可访问 
flags：指定映射对象的类型，映射选项和映射页是否可以共享。它的值可以是一个或者多个以下位的组合体 
	MAP_FIXED //使用指定的映射起始地址，如果由start和len参数指定的内存区重叠于现存的映射空间，重叠部分将会被丢弃。
				如果指定的起始地址不可用，操作将会失败。并且起始地址必须落在页的边界上。 
	MAP_SHARED //与其它所有映射这个对象的进程共享映射空间。对共享区的写入，相当于输出到文件。直到msync()或者munmap()被调用，文件实际上不会被更新。 
	MAP_PRIVATE //建立一个写入时拷贝的私有映射。内存区域的写入不会影响到原文件。这个标志和以上标志是互斥的，只能使用其中一个。 
	MAP_DENYWRITE //这个标志被忽略。 
	MAP_EXECUTABLE //同上 
	MAP_NORESERVE //不要为这个映射保留交换空间。当交换空间被保留，对映射区修改的可能会得到保证。当交换空间不被保留，
					同时内存不足，对映射区的修改会引起段违例信号。 
	MAP_LOCKED //锁定映射区的页面，从而防止页面被交换出内存。 
	MAP_GROWSDOWN //用于堆栈，告诉内核VM系统，映射区可以向下扩展。 	
	MAP_ANONYMOUS //匿名映射，映射区不与任何文件关联。 
	MAP_ANON //MAP_ANONYMOUS的别称，不再被使用。 
	MAP_FILE //兼容标志，被忽略。 
	MAP_32BIT //将映射区放在进程地址空间的低2GB，MAP_FIXED指定时会被忽略。当前这个标志只在x86-64平台上得到支持。 
	MAP_POPULATE //为文件映射通过预读的方式准备好页表。随后对映射区的访问不会被页违例阻塞。 
	MAP_NONBLOCK //仅和MAP_POPULATE一起使用时才有意义。不执行预读，只为已存在于内存中的页面建立页表入口。 
fd：有效的文件描述词。一般是由open()函数返回，其值也可以设置为-1，此时需要指定flags参数中的MAP_ANON,表明进行的是匿名映射。 
off_toffset：被映射对象内容的起点。
*/
static int oshfs_getattr(const char *path, struct stat *stbuf)//返回文件属性
{
	int ret = 0;
	struct filenode *node = get_filenode(path);
	if(strcmp(path, "/") == 0) {
		memset(stbuf, 0, sizeof(struct stat));
		stbuf->st_mode = S_IFDIR | 0755;
	} else if(node) {
		memcpy(stbuf, node->st, sizeof(struct stat));
	} else {
		ret = -ENOENT;
	}
	return ret;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)//读目录
{
	struct filenode *node = root;
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	while(node) {
		filler(buf, node->filename, node->st, 0);
		node = node->next;
	}
	return 0;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)//创建特殊文件
{
	struct stat st;
	st.st_mode = S_IFREG | 0644;
	st.st_uid = fuse_get_context()->uid;
	st.st_gid = fuse_get_context()->gid;
	st.st_nlink = 1;
	st.st_size = 0;
	create_filenode(path + 1, &st);
	return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)
{
	return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)//修改文件内容（支持追加）
{
	struct filenode *node = get_filenode(path);
	node->st->st_size = offset + size;//修改文件大小
	node->content = reballoc(node->content, offset + size);//重定向文件内容指针
	blockwrite(buf,node->content,offset,size);//将缓冲区中的内容复制到指定位置
	return size;
}

static int oshfs_truncate(const char *path, off_t size)//缩短文件大小（删除末尾的部分内容）
{
	struct filenode *node = get_filenode(path);
	node->st->st_size = size;
	node->content = reballoc(node->content, size);
	return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)//读文件，将文件内容复制到缓冲区
{
	struct filenode *node = get_filenode(path);
	int ret = size;
	if(offset + size > node->st->st_size)
		ret = node->st->st_size - offset;
	blockread(node->content,buf,offset,size);
	return ret;
}

static int oshfs_unlink(const char *path)//删除文件
{
	struct filenode *node=get_filenode(path);//找到文件
	struct filenode *t=root;
	if (node==NULL) return -1;//异常处理，未找到文件
	if (root==node) root=node->next;//如果是根节点，将root指针指向下一个节点
	else {
		while (t->next!=node) t=t->next;//找到前一个节点
		t->next=node->next;
	}
	blockfree((void*)node->filename);
	blockfree((void*)node->content);
	blockfree((void*)node->st);
	blockfree((void*)node);//释放内存空间
	return 0;
}

/*Assignment: 
Rewrite the following function : malloc(),realloc(),free();
Struct filenode should be stored in the specific block.
*/
static const struct fuse_operations op = {
	.init = oshfs_init,
	.getattr = oshfs_getattr,
	.readdir = oshfs_readdir,
	.mknod = oshfs_mknod,
	.open = oshfs_open,
	.write = oshfs_write,
	.truncate = oshfs_truncate,
	.read = oshfs_read,
	.unlink = oshfs_unlink,
};

int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &op, NULL);
}
