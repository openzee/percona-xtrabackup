#########################################################################
# File Name: build_docker.sh
# Created Time: 2023-06-14 18:41:12
# Last modified: 2023-06-14 18:41:41
#########################################################################
#!/bin/bash

docker build -t harbor.aibee.cn/platform/xtrabackup:2.4.26.my  -f Dockerfile_for_my_xtrabackup .

