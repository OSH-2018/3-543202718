#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdlib.h>
#include <errno.h>
//#include <fuse.h>
#include <sys/mman.h>
#include <sys/stat.h>
struct filenode {
	int n;//文件占用的块数
	char filename[36];//文件名	
	struct stat st;//文件属性（定义在sys/stat.h中）,占用144字节
	struct filenode *next;//指向下一个节点的指针，8字节
	void *content[8168];//指向内容的指针
};//文件节点以链表形式存在，总空间占用为65536字节，即16kb
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
int main(){
	void* p=balloc(10000);
	int i;
	printf("%d\n",sizeof(struct filenode));
	for (i=0;i<1024;i++) 
		if (mem[i]) printf("%d is used.\n",i);
	blockfree(p);
	puts("Free ");
	for (i=0;i<1024;i++) 
		if (mem[i]) printf("%d is used.\n",i);
	puts("ok");
	return 0;
}


