#include <stdio.h>
#include <iostream>
#include <string>

#include <curl/curl.h>
#include <dirent.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>


#define LSN_PF "%llu"
#define GROUP_DIRNAME_PERFIX "backup-group-"
#define BACKUPFILE_SUFFIX ".backup"

typedef uint64_t ib_uint64_t;
typedef	ib_uint64_t		lsn_t;

int max_group_count = 3;    //备份组最多保留的组数，包括 group-0
int max_incr_count = 3;     //每组备份中的增量备份的最大数量
lsn_t g_innodb_to_lsn = 0;
char xtrabackup_host[64]={0};
char mysql_host[64];
char mysql_root_password[64];
int xtrabackup_compress = 0;
int compress = 0;

size_t write_file(void* ptr, size_t size, size_t nmemb, void* userdata) {
    FILE* fp = (FILE*)userdata;
    size_t written = fwrite(ptr, size, nmemb, fp);
    return written;
}

size_t write_header(void *ptr, size_t size, size_t nmemb, void *userdata) {
    printf("%.*s", (int)(size * nmemb), (char*) ptr);

#define INNODB_TO_LSN "innodb_to_lsn"
    if( strncmp( (const char*)ptr,  INNODB_TO_LSN, strlen( INNODB_TO_LSN ) ) == 0 ){
        char *nptr = strndup( (const char *)ptr, size*nmemb );
        sscanf( nptr, "innodb_to_lsn: " LSN_PF, &g_innodb_to_lsn );
        free( nptr );
    }

    return size * nmemb;
}

int download_backup( int fd, lsn_t from_lsn ){

    CURL* curl;
    FILE* fp;
    CURLcode res;
    //char url[128] = "http://bj-cpu065.aibee.cn:9000/backup?--password=root_password&--host=rds-mysql-rep-ms-1";
    char url[256];

    snprintf( url, sizeof(url),  "http://%s:9000/backup?--password=%s&--host=%s", xtrabackup_host, mysql_root_password, mysql_host );

    if( compress ){
        strcat( url, "&--compress&--compress-threads=8" );
    }

    if( from_lsn ){
        char b[32];
        snprintf( b, sizeof(b), "&--incremental-lsn=" LSN_PF, from_lsn);
        strcat( url, b );
     }

    fprintf( stderr, "url:[%s]\n", url );
    // 初始化 libcurl 库
    curl_global_init(CURL_GLOBAL_ALL);

    // 初始化 easy handle 对象
    curl = curl_easy_init();
    if (curl) {
        // 设置要下载的 URL
        curl_easy_setopt(curl, CURLOPT_URL, url);

        // 设置写入数据的回调函数和文件指针
        fp = fdopen( fd, "wb" );
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

        // 设置写入响应头的回调函数和文件指针
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, stderr );

        g_innodb_to_lsn = 0;
        // 执行 curl 操作
        res = curl_easy_perform(curl);

        // 关闭文件指针
        fclose(fp);

        // 检查是否出错
        if (res != CURLE_OK){
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return -1;
        }

        // 释放 easy handle 对象
        curl_easy_cleanup(curl);
    }

    // 释放 libcurl 库
    curl_global_cleanup();

    return 0;
}

lsn_t find_max_lsn_in_group( const char * workdir, int group_number, int *pcount = NULL ){
    int count = 0;
    DIR *dir;
    struct dirent *entry;
    char path[64];
    lsn_t max_lsn= 0;
    snprintf( path, sizeof(path), "%s/" GROUP_DIRNAME_PERFIX "%d", workdir, group_number );

    if ((dir = opendir(path)) == NULL) {
        fprintf( stderr, "opendir:[%s] fails. errno:%d\n", path, errno );
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        if( strcmp( entry->d_name, "." ) == 0 || strcmp( entry->d_name, ".." ) == 0 ){
            continue;
        }

        if( ( entry->d_type & DT_REG ) != DT_REG ){
            fprintf( stderr, "skip unknow file:[%s]\n", entry->d_name );
            continue;
        }
   
        char *endptr = NULL;
        errno=0;
        long long int val = strtoll( entry->d_name, &endptr, 10 );
        if ((errno == ERANGE && (val == LLONG_MAX || val == LLONG_MIN)) || (errno != 0 && val == 0)) {
            fprintf( stderr, "skip: parse dirname:[%s] to lsn fails. errno:%d\n", entry->d_name, errno );
            continue;
        }

        if (endptr == entry->d_name || *endptr != '.' ) {
            fprintf( stderr, "skip: invalid dirname:[%s]\n", entry->d_name );
            continue;
        }

        if( std::string( BACKUPFILE_SUFFIX ) != std::string(endptr)  ){
            fprintf( stderr, "skip: invalid filetype:[%s]\n", entry->d_name );
            continue;
        }

        count++;
        if( val > max_lsn ){
            max_lsn = val;
        }
    }

    if( pcount != NULL ){
        *pcount = count;
    }

    return max_lsn;
}


int find_oldest_group_dir( const char *path, int **parray = NULL ) {
    int oldest_index = -1;
    DIR *dir;
    struct dirent *entry;
    int *plist;
    int slist = 0;

    plist = (int *)malloc( 100 * sizeof(int));

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

        long int number = strtol( nptr, &endptr, 10 );
        if( endptr == nptr || *endptr != '\0'  ){
            printf( "[%s] invalid name, skip\n", entry->d_name );
            continue;
        }

        int i = 0;
        for( ; i< slist; i++){
            if( plist[i] >= number )
                continue;

            for( int j = slist -1; j >= i; j--){
                plist[j+1] = plist[j];
            }

            break;
        }
        plist[i] = (int)number;
        slist++;

        if( number > oldest_index ){
            oldest_index = number ;
        }
    }

    plist[slist] = -1; //结尾标志

    if( parray != NULL){
        *parray = plist;
    }else{
        free( plist );
        plist = NULL;
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
        fprintf( stderr, "opendir:[%s] fails. errno:%d\n", pstack[top].path, errno );
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
                    fprintf( stderr, "opendir:[%s] fails. errno:%d\n", pstack[top+1].path, errno );
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

        if( rmdir( pstack[top].path ) == -1 ){
            fprintf( stderr, "rmdir :[%s] fails. errno:%d\n", pstack[top].path, errno );
        }

        closedir( pstack[top].dir );
        top-=1;
    }

    free(pstack);
    return 0;
ERROR:
    printf( "error\n" );
    //close dir
    //free pstack
    return -1;
}

int backup_group_rotate( const std::string & workdir ){

    int *parray = NULL;

    find_oldest_group_dir( workdir.c_str(), &parray );

    for( int i = 0; parray[i] != -1 ;i++  ){
        char old_name[64];
        char new_name[64];

        sprintf( old_name, "%s/" GROUP_DIRNAME_PERFIX "%d", workdir.c_str(), parray[i] );
        sprintf( new_name, "%s/" GROUP_DIRNAME_PERFIX "%d", workdir.c_str(), parray[i] + 1 );
        rename( old_name, new_name );
    }

    return 0;
}

int run_backup( const std::string & workdir ){
    std::string group_name = workdir + "/" + GROUP_DIRNAME_PERFIX "0";
    if( mkdir( group_name.c_str(),  0755 ) == -1 && errno != EEXIST ){
        fprintf( stderr, "mkdir path:[%s] fails. errno:%d\n", GROUP_DIRNAME_PERFIX "0", errno );
        return -1;
    }

    int exist_backup_count = 0;
    lsn_t fromlsn = find_max_lsn_in_group( workdir.c_str(), 0, &exist_backup_count );

    if( fromlsn != 0 && exist_backup_count - 1 >= max_incr_count ){
        backup_group_rotate( workdir );
        return run_backup( workdir );
    }

#define TMP_BACKUP_NAME GROUP_DIRNAME_PERFIX "0/tmp" BACKUPFILE_SUFFIX
    std::string tmp_fpath = workdir + "/" + TMP_BACKUP_NAME;
    int fd = open( tmp_fpath.c_str() , O_RDWR|O_CREAT|O_TRUNC,  0644 );
    if ( fd == -1){
        fprintf( stderr, "open file:[%s] fails. errno:%d errmsg:%s\n", GROUP_DIRNAME_PERFIX "0/tmp.backup", errno, strerror( errno ));
        return -1;
    }

    int rst = download_backup(fd, fromlsn );
    close(fd);

    if( rst == 0 ){
        char buf[32];

        if( fromlsn >= g_innodb_to_lsn ){
            fprintf( stderr, "bakcup invalid: fromlsn:[" LSN_PF "] to:[" LSN_PF "]\n", fromlsn, g_innodb_to_lsn);
            unlink( tmp_fpath.c_str() );
            return 0;
        }

        snprintf( buf, sizeof(buf), GROUP_DIRNAME_PERFIX "0/" LSN_PF ".backup", g_innodb_to_lsn  );
        rename( tmp_fpath.c_str(), std::string( workdir + "/" + buf ).c_str() );
    }

    return 0;
}

int main(int argc, char **argv ) {

    int opt;
    int digit_optind = 0;
    char workdir[1024];
    const char *p;
    int fd_log = -1;
    int backup = 0;
    int clean = 0;

    struct option long_options[] = {
        {"work-dir", required_argument, NULL, 1},
        {"xtrabackup-host", required_argument, NULL, 2},
        {"mysql-host", required_argument, NULL, 3},
        {"mysql-root-password", required_argument, NULL, 4},
        {"max-group-count", required_argument, NULL, 5},
        {"max-incr-count", required_argument, NULL, 6},
        {"backup", no_argument, NULL, 7},
        {"clean", no_argument, NULL, 8},
        {"compress", no_argument, NULL, 9},
        {NULL, 0, NULL, 0}
    };

    while ((opt = getopt_long(argc, argv, "", long_options, &digit_optind)) != -1) {
        switch (opt) {
            case 1:
                strncpy( workdir, optarg, sizeof(workdir ));
                break;
            case 2:
                strncpy( xtrabackup_host, optarg, sizeof( xtrabackup_host ));
                break;
            case 3:
                strncpy( mysql_host, optarg, sizeof( mysql_host ));
                break;
            case 4:
                strncpy( mysql_root_password, optarg, sizeof( mysql_root_password ));
                break;
            case 5:
                max_group_count = atoi( optarg );
                break;
            case 6:
                max_incr_count = atoi( optarg );
                break;
            case 7:
                backup = 1;
                break;
            case 8:
                clean = 1;
                break;
            case 9:
                compress = 1;
                break;
            default:
                printf("Unknown option: %c\n", opt);
                return -1; 
        }
    }

    std::cout << "xtrabackup_host:" << xtrabackup_host << std::endl;
    std::cout << "mysql_host:" << mysql_host << std::endl;
    std::cout << "mysql_root_password:" << mysql_root_password<< std::endl;
    std::cout << "workdir:" << workdir << std::endl;
    std::cout << "max-group-count:" << max_group_count << std::endl;
    std::cout << "max-incr-count:" << max_incr_count << std::endl;
    std::cout << "backup:" << backup << std::endl;

    if( backup ){
        run_backup( workdir );

        while( 1){
            int oldest_index = find_oldest_group_dir( workdir );
            if( oldest_index >= max_group_count ){
                delete_backup_group( workdir, oldest_index );
                continue;
            }

            break;
        }
    }

    if( clean ){
        int oldest_index = find_oldest_group_dir( workdir );
        if( oldest_index >= 0 ){
            delete_backup_group( workdir, oldest_index );
        }
    }
}


