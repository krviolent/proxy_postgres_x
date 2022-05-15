#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <cstring>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/tcp.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <map>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <vector>

#define bzero(ptr, size) memset(ptr, 0, size)
using namespace std;

#define TRUE             1
#define FALSE            0
#define BUF_SIZE 200000

#define EPOLL_PROXY_CLIENT 1
#define EPOLL_PROXY_SERVER 2
#define MAX_EVENTS 500
#define MAX_SESSIONS 500

#define PROXY_PORT 8888
#define LOCALHOST "127.0.0.1"
#define MAX_HOST_LEN 100
int debug_flag = 0;
int SERVER_PORT= 5432;
int conn_count = 0;
