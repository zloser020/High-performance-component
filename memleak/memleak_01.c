// 编译:gcc -o memleak_01 memleak_01.c -ldl
// 执行前先创建 mem 文件夹用于存放检测信息

#define _GNU_SOURCE
#include <dlfcn.h>
 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <link.h>
 
 
// 方式一：宏定义
 
void *_malloc(size_t size, char *filename,int line) {
    void *ptr = malloc(size);
    
    char file[128] = {0};
    sprintf(file ,"./mem/%p.mem" ,ptr);
    FILE *fp = fopen(file ,"w");//w小写
 
    fprintf(fp, "[+]addr: %p filename: %s line: %d\n",ptr,filename,line);
 
    fflush(fp);
    fclose(fp);
 
    return ptr;
}
 
void _free(void *ptr, char *filename,int line) {
 
    char file[128] = {0};
    sprintf(file,"./mem/%p.mem",ptr);
 
    if(unlink(file) < 0){//unlink用于在文件系统中删除指定的文件
        printf("double free %p\n",ptr);
        return;
    }
 
    return free(ptr);
 
}
 
// __FILE__ 获取文件名
// __LINE__ 获取函数执行的行号
 
#define malloc(size)   _malloc(size, __FILE__,__LINE__)
#define free(ptr)      _free(ptr, __FILE__,__LINE__)
 
int main(void) {
 
    //init_hook();
 
    void *p1 = malloc(8);
    void *p2 = malloc(16);
    void *p3 = malloc(32);
 
    free(p1);
    free(p2);
 
    return 0;
}