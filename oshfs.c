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

static const size_t size = 4 * 1024 * 1024 * (size_t)1024;
static void *mem[64 * 1024];//内存块

static struct filenode *root = NULL;//根文件节点

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
    struct filenode *new = (struct filenode *)malloc(sizeof(struct filenode));
    new->filename = (char *)malloc(strlen(filename) + 1);//+1是为了存放字符串末尾的\0标记
    memcpy(new->filename, filename, strlen(filename) + 1);//复制文件名
    new->st = (struct stat *)malloc(sizeof(struct stat));
    memcpy(new->st, st, sizeof(struct stat));//复制文件属性
    new->next = root;
    new->content = NULL;
    root = new;//采用头插法，新节点插在根节点之前
}

static void *oshfs_init(struct fuse_conn_info *conn)//内存初始化，函数提供了两种方法将所有的内存初始化为0
{
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
    node->content = realloc(node->content, offset + size);//重定向文件内容指针
    memcpy(node->content + offset, buf, size);//将缓冲区中的内容复制到指定位置
    return size;
}

static int oshfs_truncate(const char *path, off_t size)//缩短文件大小（删除末尾的部分内容）
{
    struct filenode *node = get_filenode(path);
    node->st->st_size = size;
    node->content = realloc(node->content, size);
    return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)//读文件，将文件内容复制到缓冲区
{
    struct filenode *node = get_filenode(path);
    int ret = size;
    if(offset + size > node->st->st_size)
        ret = node->st->st_size - offset;
    memcpy(buf, node->content + offset, ret);
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
	free((void*)node->filename);
	free((void*)node->content);
	free((void*)node->st);
	free((void*)node);//释放内存空间
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