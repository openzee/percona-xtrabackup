#include <stdio.h>
#include <iostream>
#include <string>
#include <unistd.h>
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
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <cassert>

#define UINT64PF	"%" PRIu64
#define LSN_PF UINT64PF
#define GROUP_DIRNAME_PERFIX "backup-group-"
#define GROUP_DIRNAME_PERFIX_LEN strlen( GROUP_DIRNAME_PERFIX )
#define BACKUPFILE_SUFFIX ".backup"

typedef uint64_t ib_uint64_t;
typedef	ib_uint64_t		lsn_t;

int backup_group_count = 3;    //备份组最多保留的组数，包括 group-0
int backup_incr_count = 3;     //每组备份中的增量备份的最大数量
lsn_t g_innodb_to_lsn = 0;
char xtrabackup_host[64]={0};
char mysql_host[64];
char mysql_root_password[64];
int xtrabackup_compress = 0;
int compress = 0;

/*
 * return: success: 0 fails:-1
 */
int snprintf_ex( std::string & rst_str, size_t buf_size, const char * format, ... ){

    int nwrite;
    va_list ap;
    char * pbuf = NULL;

    rst_str = "";

    if( format == NULL ){
        fprintf( stderr, "invalid  args: format is NULL\n" );
        return -1;
    }

    if( ( pbuf = (char*)malloc( buf_size ) ) == NULL ){
        fprintf( stderr, "malloc buf fails. len:%zu\n", buf_size );
        return -1;
    }

    va_start (ap, format);
    nwrite = vsnprintf ( pbuf, buf_size, format, ap);
    va_end (ap);

    if( nwrite < (int) buf_size ){
        rst_str = pbuf;
        free( pbuf );
        return 0;
    }

    //buf_size不够，输出内容被截断，需要重新分配内存
    //分配的内存大小为: nwrite ，不包括最后的\0
    free( pbuf );
    buf_size = nwrite + 1;
    if( ( pbuf = (char*)malloc( buf_size ) ) == NULL ){
        fprintf( stderr, "malloc buf fails. len:%zu\n", buf_size );
        return -1;
    }

    va_start (ap, format);
    nwrite = vsnprintf ( pbuf, buf_size, format, ap);
    va_end (ap);

    //第二次失败，直接报错
    if( nwrite >= (int)buf_size ){
        fprintf( stderr, "vsnprintf fails\n" );
        free( pbuf );
        return -1;
    }

    rst_str = pbuf;
    free( pbuf );
    pbuf = NULL;

    return 0;
}

size_t write_file(void* ptr, size_t size, size_t nmemb, void* userdata) {
    int *pfp = (int*)userdata;
    return write( *pfp, ptr, size*nmemb);
}

size_t write_header(void *ptr, size_t size, size_t nmemb, void *userdata) {
    fprintf(stderr, "%.*s", (int)(size * nmemb), (char*) ptr);
    fflush( stderr );

#define INNODB_TO_LSN "innodb_to_lsn"
    if( strncmp( (const char*)ptr,  INNODB_TO_LSN, strlen( INNODB_TO_LSN ) ) == 0 ){
        char *nptr = strndup( (const char *)ptr, size*nmemb );
        sscanf( nptr, "innodb_to_lsn: " LSN_PF, &g_innodb_to_lsn );
        free( nptr );
    }

    return size * nmemb;
}

#if 0
struct ProgressData {
    CURL *curl;
};

int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    struct ProgressData *progress = (struct ProgressData *) clientp;

    curl_off_t download_time;
    curl_easy_getinfo(progress->curl, CURLINFO_TOTAL_TIME_T, &download_time); //获取的时间为微妙
    download_time = download_time/100000;   //微妙转为秒

    double down_total =dlnow/1024.0/1024.0;

    //speed MBps
    double speed = download_time ? (double)(down_total) / (double)download_time : 0;

    //回车并擦除当前行
    fprintf( stderr, "\r\033[2K" );
    fflush( stderr );

    fprintf( stderr, "down: [%.2f]MB, speed: [%05.2f]MBps", down_total, speed );
    fflush( stderr );

    return CURLE_OK;
}
#endif

/*
 * in: from_lsn: >0 启动增量备份
 * int fd: 
 * return: success: 0 fails:-1
 */
int download_backup( int fd_downtarget, lsn_t from_lsn ){

    CURL* curl;
    CURLcode res;
    std::string url;

    if( snprintf_ex( url, 128, "http://%s:9000/backup?--password=%s&--host=%s", xtrabackup_host, mysql_root_password, mysql_host ) == -1 ){
        return -1;
    }

    if( compress ){
        url += "&--compress&--compress-threads=8";
    }

    if( from_lsn ){
        std::string str;
        if( snprintf_ex( str, 64, "&--incremental-lsn=" LSN_PF, from_lsn) == -1 ){
            return -1;
        }
        url += str;
     }

    fprintf( stderr, "url:[%s]\n", url.c_str() );
    // 初始化 libcurl 库
    curl_global_init(CURL_GLOBAL_ALL);

    // 初始化 easy handle 对象
    if( ( curl = curl_easy_init() ) == NULL ){
        curl_global_cleanup();
        return -1;
    }

    // 设置要下载的 URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str() );

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fd_downtarget);

    // 设置写入响应头的回调函数和文件指针
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, stderr );

#if 0
    struct ProgressData progress = {0};
    fprintf( stderr, "\r\n" );
    progress.curl = curl;
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
#endif

    g_innodb_to_lsn = 0;
    if( ( res = curl_easy_perform(curl) ) != CURLE_OK ){
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return -1;
    }

    curl_off_t dl;
    if( curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &dl) == CURLE_OK ){
      fprintf( stderr, "Downloaded %" CURL_FORMAT_CURL_OFF_T " bytes\n", dl);
    }

    curl_off_t speed;
    if( curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD_T, &speed) == CURLE_OK ){
        fprintf( stderr, "Download speed %" CURL_FORMAT_CURL_OFF_T " bytes/sec\n", speed);
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return 0;
}

/*
 * in: workdir :工作目录
 * in: group_number: 在该组进行搜索
 * out: max_lsn: 在该组中的最大lsn
 * out: pcount: 该组中的备份文件数量
 * return: success: 0 fails:-1
 */
int find_max_lsn_in_group( const char * workdir, int group_number, lsn_t & max_lsn, int *pcount = NULL ){
    int backup_count = 0;
    DIR *dir;
    struct dirent *entry;
    std::string group_path;
    
    max_lsn = 0;

    if( snprintf_ex( group_path, 64, "%s/" GROUP_DIRNAME_PERFIX "%d", workdir, group_number ) == -1 ){
        return -1;
    }

    if ((dir = opendir(group_path.c_str())) == NULL) {
        fprintf( stderr, "opendir:[%s] fails. errno:%d\n", group_path.c_str(), errno );
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if( strcmp( entry->d_name, "." ) == 0 || strcmp( entry->d_name, ".." ) == 0 ){
            continue;
        }

        //跳过非正常文件
        if( ( entry->d_type & DT_REG ) != DT_REG ){
            fprintf( stderr, "skip unknow file:[%s]\n", entry->d_name );
            continue;
        }
   
        char *endptr = NULL;
        errno=0;
        long long int val = strtoll( entry->d_name, &endptr, 10 );      //entry->d_name 期望格式：82335669364441.backup

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

        if( (lsn_t)val > max_lsn ){
            max_lsn = val;
        }

        backup_count++;
    }

    closedir(dir);

    if( pcount != NULL ){
        *pcount = backup_count;
    }

    return 0;
}

/**
 * 根据名字，查找最索引号最大的备份组的索引
 * in: path 工作目录
 * out: parray 数组指针，保存所有的备份组的索引
 * return : success: oldest_group_index > 0 fails:-1
 **/
int find_oldest_group_dir( const std::string & path, int **parray = NULL ) {
    DIR *dir;
    struct dirent *entry;
    int *plist;
    int slist = 0;

    if( parray ){
        *parray = NULL;
    }

#define PLIST_MAX_SIZE 100

    //plist 存放有序的index
    if( ( plist = (int *)malloc( PLIST_MAX_SIZE * sizeof(int)) ) == NULL ){
        return -1;
    }

    if ( ( dir = opendir( path.c_str() )) == NULL) {
        fprintf( stderr, "opendir:[%s] fails. errno:%d\n", path.c_str(), errno );
        free( plist );
        return -1;
    }

    while ( ( entry = readdir(dir)) != NULL ) {
        if( strcmp( entry->d_name, "." ) == 0 || strcmp( entry->d_name, ".." ) == 0 ){
            continue;
        }

        if( ( entry->d_type & DT_DIR ) != DT_DIR ){
            fprintf( stderr, "warning: [%s] not directory, skip\n", entry->d_name );
            continue;
        }

        //entry->d_name 期望格式 backup-group-9 backup-group-10
        if( strncmp( entry->d_name, GROUP_DIRNAME_PERFIX, GROUP_DIRNAME_PERFIX_LEN ) != 0 ){
            fprintf( stderr, "[%s] invalid name, skip\n", entry->d_name );
            continue;
        }

        char *endptr = NULL;
        const char *nptr = entry->d_name + GROUP_DIRNAME_PERFIX_LEN;

        //数值溢出
        errno = 0;
        long int number = strtol( nptr, &endptr, 10 );
        if( errno == ERANGE && ( number == LONG_MIN || number == LONG_MAX ) ){
            fprintf( stderr, "invalid backup item:%s, skip\n", entry->d_name );
            continue;
        }

        //没有找到有效的数值
        if( endptr == nptr || *endptr != '\0'  ){
            fprintf( stderr, "invalid backup item:%s, skip\n", entry->d_name );
            continue;
        }

        //索引号为非负数
        if( number < 0 ){
            fprintf( stderr, "invalid backup item:%s, skip\n", entry->d_name );
            continue;
        }

        assert( slist +1 < PLIST_MAX_SIZE );
        //将number按照从大到小的顺序，插入有序数组plist中
        int i = 0;
        for( ; i< slist; i++){
            if( plist[i] >= number )
                continue;

            //slist[i] < number
            for( int j = slist -1; j >= i; j--){
                plist[j+1] = plist[j];
            }

            break;
        }
        plist[i] = (int)number;
        slist++;
    }
    closedir(dir);

    assert( slist < PLIST_MAX_SIZE );
    plist[slist] = -1; //结尾标志 ,工作目录未找到合法的备份组，slist=0 返回-1

    int oldest_index = plist[0];

    //for( int i=0; plist[i] != -1;i++ ){
    //    printf( "%d\n", plist[i] );
    //}

    if( parray != NULL){
        *parray = plist;
    }else{
        free( plist );
        plist = NULL;
    }

    return oldest_index;
}

struct stack_item{
    std::string path;
    DIR * dir; 
};

/*
 * in: workdir: 工作目录
 * in: number: 删除的备份组号 backup-group-xx
 * return: success: 0 fails: -1
 * */
int delete_backup_group(  const char * workdir, int number ){

    fprintf( stderr, "clean backup group:%d\n", number );

#define STACK_SIZE 200  //栈的最大深度，对应目录的最大深度
    int stack_top = -1;
    struct dirent *entry;
    //struct stack_item *pstack = (struct stack_item *)malloc( STACK_SIZE * sizeof(struct stack_item));
    struct stack_item *pstack = new struct stack_item[ STACK_SIZE ];

    stack_top++;

    if( snprintf_ex( pstack[stack_top].path, 64, "%s/" GROUP_DIRNAME_PERFIX "%d", workdir, number ) == -1 ){
        return -1;
    }

    if( ( pstack[stack_top].dir = opendir( pstack[stack_top].path.c_str() ) ) == NULL ){
        fprintf( stderr, "opendir:[%s] fails. errno:%d\n", pstack[stack_top].path.c_str(), errno );
        goto ERROR;
    }

    while(stack_top >= 0 ){
CONTINUE:
        while( ( entry = readdir(pstack[stack_top].dir) ) != NULL) {
            if( strcmp( entry->d_name, "." ) == 0 || strcmp( entry->d_name, ".." ) == 0 ){
                continue;
            }

            if( ( entry->d_type & DT_DIR ) == DT_DIR ){
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                if( snprintf_ex( pstack[stack_top+1].path, 64, "%s/%s", pstack[stack_top].path.c_str(), entry->d_name ) == -1 ){
                    return -1;
                }
#pragma GCC diagnostic pop
                if( ( pstack[stack_top+1].dir = opendir( pstack[stack_top+1].path.c_str() ) ) == NULL ){
                    fprintf( stderr, "opendir:[%s] fails. errno:%d\n", pstack[stack_top+1].path.c_str(), errno );
                    goto ERROR;
                }
                stack_top+=1;
                goto CONTINUE;
            }

            if( unlinkat( dirfd(pstack[stack_top].dir), entry->d_name, 0 ) == -1 ){
                fprintf( stderr, "unlinkat:[%s] fails. errno:%d\n", entry->d_name, errno );
                goto ERROR;
            }
        }

        if( rmdir( pstack[stack_top].path.c_str() ) == -1 ){
            fprintf( stderr, "rmdir :[%s] fails. errno:%d\n", pstack[stack_top].path.c_str(), errno );
        }

        closedir( pstack[stack_top].dir );
        stack_top-=1;
    }

    delete []pstack;
    return 0;
ERROR:
    printf( "error\n" );
    //close dir
    //free pstack
    return -1;
}

/*
 * 备份组轮转
 * in: workdir 工作目录
 * return: success:0 fails:-1
 * */
int backup_group_rotate( const std::string & workdir ){

    int *parray = NULL;

    if( find_oldest_group_dir( workdir.c_str(), &parray ) == -1 ){
        return -1;
    }

    for( int i = 0; parray[i] != -1 ;i++  ){
        std::string old_name;
        std::string new_name;

        if( snprintf_ex( old_name, 64, "%s/" GROUP_DIRNAME_PERFIX "%d", workdir.c_str(), parray[i] ) == -1 ||
            snprintf_ex( new_name, 64, "%s/" GROUP_DIRNAME_PERFIX "%d", workdir.c_str(), parray[i] + 1 ) == -1 ){
            return -1;
        }

        if( rename( old_name.c_str(), new_name.c_str() ) == -1 ){
            fprintf( stderr, "mv %s=>%s fails. errno:%d\n", old_name.c_str(), new_name.c_str(), errno );
            return -1;
        }
    }

    return 0;
}

/*
 * in: workdir: 工作目录
 * return: success: 0 fails: -1
 * */
int run_backup( const std::string & workdir ){
    std::string group_name = workdir + "/" + GROUP_DIRNAME_PERFIX "0";
    if( mkdir( group_name.c_str(),  0755 ) == -1 && errno != EEXIST ){
        fprintf( stderr, "mkdir path:[%s] fails. errno:%d\n", GROUP_DIRNAME_PERFIX "0", errno );
        return -1;
    }

    int exist_backup_count = 0;
    lsn_t fromlsn = 0;

    if( find_max_lsn_in_group( workdir.c_str(), 0, fromlsn, &exist_backup_count ) == -1 ){
        return -1;
    }

    if( fromlsn != 0 && exist_backup_count - 1 >= backup_incr_count ){
        fprintf( stderr, "group-0 full, go rotate\n");
        if( backup_group_rotate( workdir ) == -1 ){
            fprintf( stderr, "backup_group_rotate fails\n");
            return -1;
        }
        return run_backup( workdir );
    }

#define TMP_BACKUP_NAME GROUP_DIRNAME_PERFIX "0/tmp" BACKUPFILE_SUFFIX
    std::string tmp_fpath = workdir + "/" + TMP_BACKUP_NAME;
    int fd_downtarget = open( tmp_fpath.c_str() , O_RDWR|O_CREAT|O_TRUNC,  0644 );
    if ( fd_downtarget == -1){
        fprintf( stderr, "open file:[%s] fails. errno:%d errmsg:%s\n", GROUP_DIRNAME_PERFIX "0/tmp.backup", errno, strerror( errno ));
        return -1;
    }

    int rst = download_backup(fd_downtarget, fromlsn );
    close(fd_downtarget);

    if( rst == -1 ){
        fprintf( stderr, "download_backup from:" LSN_PF " fails", fromlsn );
        return -1;
    }

    //g_innodb_to_lsn是本地下载，xtrabackup返回lsn检查点，如果fromlsn == g_innodb_to_lsn，说明本次备份到上次备份期间
    //数据库处于静止状态，没有数据变化
    if( fromlsn >= g_innodb_to_lsn ){
        fprintf( stderr, "bakcup invalid: fromlsn:[" LSN_PF "] to:[" LSN_PF "]\n", fromlsn, g_innodb_to_lsn);
        unlink( tmp_fpath.c_str() );
        return 0;
    }

    std::string buf;
    if( snprintf_ex( buf, 128, GROUP_DIRNAME_PERFIX "0/" LSN_PF ".backup", g_innodb_to_lsn  ) == -1 ){
        unlink( tmp_fpath.c_str() );
        return -1;
    }

    std::string dst = std::string( workdir + "/" + buf );
    if( rename( tmp_fpath.c_str(),  dst.c_str() ) == -1 ){
        fprintf( stderr, "rename %s => %s fails. errno:%d\n", tmp_fpath.c_str(), dst.c_str(), errno );
        unlink( tmp_fpath.c_str() );
        return -1;
    }

    return 0;
}

int main(int argc, char **argv ) {

    int opt;
    int digit_optind = 0;
    char workdir[1024]={0};
    int backup = 0;
    int clean = 0;

    struct option long_options[] = {
        {"work-dir", required_argument, NULL, 1},
        {"xtrabackup-host", required_argument, NULL, 2},
        {"mysql-host", required_argument, NULL, 3},
        {"mysql-root-password", required_argument, NULL, 4},
        {"backup-group-count", required_argument, NULL, 5},
        {"backup-incr-count", required_argument, NULL, 6},
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
                backup_group_count = atoi( optarg );
                break;
            case 6:
                backup_incr_count = atoi( optarg );
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
    std::cout << "backup-group-count:" << backup_group_count << std::endl;
    std::cout << "backup-incr-count:" << backup_incr_count << std::endl;
    std::cout << "backup:" << backup << std::endl;

    //backup_group_count备份保留的个数
    if( backup_group_count < 0 ){
        fprintf( stderr, "invalid args: backup_group_count:%d\n", backup_group_count );
        return -1;
    }

    //backup_incr_count 最小为0，即仅进行全量备份
    if( backup_incr_count < 0 ){
        fprintf( stderr, "invalid args: backup_incr_count :%d\n", backup_incr_count );
        return -1;
    }

    //先执行备份轮转
    //在执行备份
    //在执行过期备份删除
    if( backup ){
        if( run_backup( workdir ) == -1 ){
            fprintf( stderr, "backup fails\n");
            return -1;
        }

        while( 1){
            int oldest_index = find_oldest_group_dir( workdir );
            if( oldest_index >= backup_group_count ){
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
            fprintf( stderr, "del backup group-:%d\n", oldest_index );
        }else{
            fprintf( stderr, "can't find backup group\n" );
        }
    }
}

