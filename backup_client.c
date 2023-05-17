#include <stdio.h>
#include <curl/curl.h>
#include <dirent.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

size_t write_file(void* ptr, size_t size, size_t nmemb, void* userdata) {
    FILE* fp = (FILE*)userdata;
    size_t written = fwrite(ptr, size, nmemb, fp);
    return written;
}

size_t write_header(void *ptr, size_t size, size_t nmemb, void *userdata) {
    printf("%.*s", (int)(size * nmemb), (char*) ptr);
    return size * nmemb;
}

int download_backup( const char *filename ){

    CURL* curl;
    FILE* fp;
    CURLcode res;
    const char* url = "http://bj-cpu065.aibee.cn:9000/backup?--password=root_password&--host=rds-mysql-rep-ms-1";

    // 初始化 libcurl 库
    curl_global_init(CURL_GLOBAL_ALL);

    // 初始化 easy handle 对象
    curl = curl_easy_init();
    if (curl) {
        // 设置要下载的 URL
        curl_easy_setopt(curl, CURLOPT_URL, url);

        // 设置写入数据的回调函数和文件指针
        fp = fopen(filename, "wb");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

        // 设置写入响应头的回调函数和文件指针
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, stderr );

        // 执行 curl 操作
        res = curl_easy_perform(curl);

        // 关闭文件指针
        fclose(fp);

        // 检查是否出错
        if (res != CURLE_OK){
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            remove( filename );
        }

        // 释放 easy handle 对象
        curl_easy_cleanup(curl);
    }

    // 释放 libcurl 库
    curl_global_cleanup();

    return 0;
}

#define GROUP_DIRNAME_PERFIX "backup-group-"

int find_oldest_group_dir(char *path) {
    int oldest_index = -1;
    DIR *dir;
    struct dirent *entry;

    if ((dir = opendir(path)) == NULL) {
        fprintf( stderr, "opendir:[%s] fails. errno:%d\n", path, errno );
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if( strcmp( entry->d_name, "." ) == 0 || strcmp( entry->d_name, ".." ) == 0 ){
            continue;
        }

        if( ( entry->d_type & DT_DIR ) != DT_DIR ){
            printf( "[%s] not directory, skip\n", entry->d_name );
            continue;
        }

        if( strncmp( entry->d_name, GROUP_DIRNAME_PERFIX, strlen( GROUP_DIRNAME_PERFIX) ) != 0 ){
            printf( "[%s] invalid name, skip\n", entry->d_name );
            continue;
        }

        char *endptr = NULL;
        const char *nptr = entry->d_name+strlen( GROUP_DIRNAME_PERFIX);

        long int index = strtol( nptr, &endptr, 10 );
        if( endptr == nptr || *endptr != '\0'  ){
            printf( "[%s] invalid name, skip\n", entry->d_name );
            continue;
        }

        if( index > oldest_index ){
            oldest_index = index;
        }

    }

    closedir(dir);
    return oldest_index;
}

struct stack_item{
    char path[64];
    DIR * dir; 
};

int delete_backup_group(  const char * workdir, int number ){
#define STACK_SIZE 200
    int top = -1;
    struct dirent *entry;
    struct stack_item *pstack = (struct stack_item *)malloc( STACK_SIZE * sizeof(struct stack_item));

    top++;
    snprintf( pstack[top].path, sizeof(pstack[top].path), "%s/" GROUP_DIRNAME_PERFIX "%d", workdir, number );
    if( ( pstack[top].dir = opendir( pstack[top].path ) ) == NULL ){
        goto ERROR;
    }

    while(top >= 0 ){
CONTINUE:
        while( ( entry = readdir(pstack[top].dir) ) != NULL) {
            if( strcmp( entry->d_name, "." ) == 0 || strcmp( entry->d_name, ".." ) == 0 ){
                continue;
            }

            if( ( entry->d_type & DT_DIR ) == DT_DIR ){
                snprintf( pstack[top+1].path, sizeof( pstack[top+1].path), "%s/%s", pstack[top].path, entry->d_name );
                if( ( pstack[top+1].dir = opendir( pstack[top+1].path ) ) == NULL ){
                    goto ERROR;
                }
                top+=1;
                goto CONTINUE;
            }

            if( unlinkat( dirfd(pstack[top].dir), entry->d_name, 0 ) == -1 ){
                fprintf( stderr, "unlinkat:[%s] fails. errno:%d\n", entry->d_name, errno );
                goto ERROR;
            }
        }

        rmdir( pstack[top].path );
        closedir( pstack[top].dir );
        top-=1;
    }

    free(pstack);
    return 0;
ERROR:
    //close dir
    //free pstack
    return -1;
}

int main(int argc, char **argv ) {

    int opt;
    int digit_optind = 0;
    char workdir[1024];
    const char *p;
    int fd_log = -1;

    struct option long_options[] = {
        {"work-dir", required_argument, NULL, 'w'},
        {NULL, 0, NULL, 0}
    };

    while ((opt = getopt_long(argc, argv, "w:", long_options, &digit_optind)) != -1) {
        switch (opt) {
            case 'w':
                strncpy( workdir, optarg, 1024 );
                break;
            case '?':
                break;
            default:
                printf("Unknown option: %c\n", opt);
                return -1; 
        }
    }

    delete_backup_group( workdir, 3 );
    return 0;

    int oldest_index = find_oldest_group_dir( workdir );
    printf( "%d\n", oldest_index );
}


