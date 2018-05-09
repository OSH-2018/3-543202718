#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>

struct filenode {
	int amount;//文件占用的块数,4字节
	int num;//文件节点对应的内存块编号,4字节
	char filename[32];//文件名，要求不多于32字节（包括\0）
	struct stat st;//文件属性（定义在sys/stat.h中）,占用144字节
	struct filenode *next;//指向下一个节点的指针，8字节
	int content[8144];//指向内容的指针,支持最大的内容为254.5MB
};//文件节点以链表形式存在，总空间占用为32KB

static const size_t size =256 * 1024 * (size_t)1024;//size = 256MB
static const size_t blocksize = 32 * (size_t)1024;//blocksize = 32KB
static const int blocknr =8 * 1024;//blocknr = 8k
static void *mem[8*1024];//内存块
static struct filenode *root = NULL;//根文件节点
int blockused=0;//确信blockused之前的所有块都被使用

int getfbnum()//获取当前空闲块的数量
{
	int i,n=0;
	for (i=0;i<blocknr;i++)
		if (mem[i]==NULL) n++;
	return n;
}

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
	if (i+1>blockused) blockused=i+1;//当前分配了第i块，说明从0到i的块都不是空闲的
	return i;
}

void bfree(int k)//块释放
{
	munmap(mem[k], blocksize);//解除映射
	mem[k]=NULL;
	if (k<blockused) blockused=k;//释放之前从0到blockused的块都不是空闲的，释放之后第k块是第一个空闲的块
}

int ralloc(struct filenode *node,int n2)//块重新分配
{
	int n1=node->amount;
	int i,k=0;
	if (n1>=n2) {//如果要求的空间比已有的小，释放多余的空间
		for (i=n2;i<n1;i++) bfree(node->content[i]);
	}
	else {//如果要求的空间比已有的大,分配新增的块
		k=getfbnum();//获取空闲块的数量
		if (k<n2-n1) {
			return -1;
		}//错误处理
		for (i=n1;i<n2;i++) {
			k=balloc();
			node->content[i]=k;
		}//分配新增的块
	}	
	node->amount=n2;
	return 0;
}

static struct filenode *get_filenode(const char *name)//按照文件名获取文件节点
{
	struct filenode *node = root;
	while(node) {
		if(strcmp(node->filename, name + 1) != 0)//用来跳过开头的/符号
			node = node->next;
		else
			return node;
	}//遍历链表找到所要求的文件节点
	return NULL;
}

static void create_filenode(const char *filename, const struct stat *st)//创建文件节点
{	
	int k=balloc();//分配块存放文件属性
	if (k<0) {
		return;
	}//错误处理
	struct filenode *new = (struct filenode *)mem[k];	
	strcpy(new->filename, filename);//复制文件名
	memcpy(&(new->st), st, sizeof(struct stat));//复制文件属性
	new->amount = 0;
	new->num = k;
	new->next = root;	
	root = new;//采用头插法，新节点插在根节点之前
}

static void *oshfs_init(struct fuse_conn_info *conn)//内存初始化，函数提供了两种方法将所有的内存初始化为0
{	
	return NULL;
}
static int oshfs_getattr(const char *path, struct stat *stbuf)//返回文件属性
{
	int ret = 0;
	struct filenode *node = get_filenode(path);
	if(strcmp(path, "/") == 0) {
		memset(stbuf, 0, sizeof(struct stat));
		stbuf->st_mode = S_IFDIR | 0755;
	} else if(node) {
		memcpy(stbuf, &(node->st), sizeof(struct stat));
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
		filler(buf, node->filename, &(node->st), 0);
		node = node->next;
	}
	return 0;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)//创建文件
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
	int i,j,k,m,n,temp,sum;
	node->st.st_size = offset + size;//修改文件大小
	n=(offset+size-1)/blocksize+1;//计算新的大小所需要的块数（取上整）
	k=ralloc(node,n);//重定向文件内容指针
	if (k<0) {
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

static int oshfs_truncate(const char *path, off_t size)//缩短文件大小（删除末尾的部分内容）
{
	struct filenode *node = get_filenode(path);
	node->st.st_size = size;
	int n=(size-1)/blocksize+1;//计算新的大小所需要的块数（取上整）
	ralloc(node,n);//重定向文件内容指针
	return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)//读文件，将文件内容复制到缓冲区
{
	struct filenode *node = get_filenode(path);
	int ret = size;
	int i,j,k,m,n,temp,sum;
	if(offset + size > node->st.st_size)
		ret = node->st.st_size - offset;
	m=offset/blocksize;//偏移位置所在的块
	offset=offset%blocksize;//块内偏移量
	sum=0;//已经复制的字节数
	while (ret>sum){
		i=node->content[m];
		if (ret-sum>blocksize-offset) k=blocksize-offset;
		else k=ret-sum;//计算当前块要复制的字节数
		memcpy(buf+sum,mem[i]+offset,k);
		sum+=k;
		offset=0;
		m++;
	}
	return ret;
}

static int oshfs_unlink(const char *path)//删除文件
{
	struct filenode *node=get_filenode(path);//找到文件
	struct filenode *t=root;
	int i;
	if (node==NULL) return -1;//异常处理，未找到文件
	if (root==node) root=node->next;//如果是根节点，将root指针指向下一个节点
	else {
		while (t->next!=node) t=t->next;//找到前一个节点
		t->next=node->next;
	}
	for (i=0;i<node->amount;i++)	bfree(node->content[i]);
	bfree(node->num);//释放内存空间
	return 0;
}

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
