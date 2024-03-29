# Linux Network Programming Fundamental API

Three Parts:
- socket address API: socket means a pair of IP address and port
- socket fundamental API: in sys/socket.h, creating socket, naming socket, listening socket...
- network info API: netdb.h, Translation between hostname and IP address, service name and port number

## 5.1 Socket Address API

### 5.1.1 Endian

- Little Endian: 23-31bit in the high address, 0-7bit in the low address (commonly used)
- Big Endian: 23-31bit in the low address, 0-7bit in the high address

Strategy:
The sender translates the data into big endian, the receiver translates the data into its endian.

So big endian is also called network endian, little endian is called host endian.

```
#include <netinet/in.h>
unsigned long int htonl(unsigned long int hostlong); host to network, unsigned long for IP address
unsigned long int ntohl(unsigned long int netlong); network to host, unsigned long for IP address
unsigned short int htons(unsigned short int hostshort); host to network, unsigned short for port number 
unsigned short int ntohs(unsigned short int netshort); network to host, unsigned short for port number
```

### 5.1.2 General Socket Address

use `sockaddr` to represent socket address

```
#include <bits/socket.h>
struct sockaddr {
  sa_family_t sa_family;
  char sa_data[14];
};
```

- `sa_family_t`: socket address family type: AF_UNIX, AF_INET, AF_INET6
- `sa_data`: storing socket address value, not big enough

**new socket addr struct**
```
#include <bits/socket.h>
struct sockaddr_storage {
  sa_family_t sa_family;
  unsigned long int __ss_align; // for alignment
  char __ss_padding[128-sizeof(__ss_align)];
};
```

### 5.1.3 Special Socket Address

**1. UNIX Local Domain**
```
#include <sys/un.h>
struct sockaddr_un {
  sa_family_t sin_family; // AF_UNIX
  char sun_path[108]; // path name
};
```

**2. INET4**
```
struct sockaddr_in {
  sa_family_t sin_family; // AF_INET
  u_int16_t sin_port; // port number
  struct in_addr sin_addr; // IPv4 address struct
};
struct in_addr {
  u_int32_t s_addr;//IPv4 address, 4bytes, net endian
};
```

**3. INET6**
```
struct sockaddr_in6 {
  sa_family_t sin6_family; // AF_INET6
  u_int16_t sin6_port; // port number;
  u_int32_t sin6_flowinfo; // flow info
  struct in6_addr sin6_addr;
  u_int32_t sin6_scope_id;
};
struct in6_addr {
  unsigned char sa_addr[16]; // 16bytes, net endian
};
```

All the special socket address should be forcely translated into general socket address.

### 5.1.4 IP Addresss Translation Function

```
// Translation between net endian binary IP address and decimal IP address
#include <arpa/inet.h>
in_addr_t inet_addr(const char* strptr); // decimal to net endian binary, NADDR_NONE when failure
int inet_aton(const char* cp, struct in_addr* inp); // same as above, save the res into inp
char * inet_ntoa(struct in_addr in); // 
```

The `inet_ntoa` function uses a static variable to store the result, so the inet_ntoa cannot be used twice.

**Another pair of functions**
```
#include <arpa/inet.h>
int inet_pton(int af, const char* src, void* dst); // af: AF_INET, AF_INET6
const char* inet_ntop(int af, const void* src, char* dst, socklen_t cnt); // return dst memory space
```

## 5.2 Creating Socket

use socket syscall to create socket

```
#include <sys/types.h>
#include <sys/socket.h>
// return sockfd
int socket(int domain, int type, int protocol);
```

- domain: PF_INET, PF_INET6, PF_UNIX
- type: SOCK_STREAM(TCP), SOCK_DGRAM(UDP), SOCK_NONBLOCK, SOCK_CLOEXEC
- protocol: 0 for default
- return -1 when failure, socket fd when success

## 5.3 Naming Socket

Definition: Bind a socket with socket address

- In server software, we should name the socket.
- In client software, we use anomynous socket.

```
#include <sys/types.h>
#include <sys/socket.h>
// return 0 when success
int bind(int sockfd, const struct sockaddr * my_addr, socklen_t addrlen);
```

bind will give an unnamed sockfd file descriptor to the socket address of `my_addr`, bind will return 0 when success, return -1 and set `errno` when failure

Two common `error`:
- EACCES: the address is under protection, such as binding port 80, 23...
- EADDRINUSE: the address is in use, such as binding a socket which is in TIME_WAIT

## 5.4 Listening Socket

Socket cannot accept client connection after naming, it need to use syscall `listen` to create a listening queue to store the connections to be handled.

```
#include <sys/socket.h>
// return 0 when success
int listen(int sockfd, int backlog);
```

- sockfd: socket to be listened
- backlog: max length of listening queue, if over backlog, the server will not handle new connections and return ECONNREFUSED, typically 5

Complete connection(ESTABLISHED) number is a bit larger than backlog, other connections will be in state SYN_RCVD.

## 5.5 Accepting Connection

call `accept` syscall to accept a connection from the listening queue.

```
#include <sys/types.h>
#include <sys/socket.h>
// return sockfd
int accept(int sockfd, struct sockaddr * addr, socklen_t * addrlen);
```

- 监听 listening socket: have executed `listen` syscall, in LISTEN state
- 连接 connected socket: in ESTABLISHED state

`accept` function will turn a socket from LISTEN state to ESTABLISHED state

`accpet` only takes out one connection from the listening queue, don't care about the state of the connection

## 5.6 Making Connection

the client call `connect` syscall to make connection with the server

```
#include <sys/types.h>
#include <sys/socket.h>
// return 0 when success
int connect(int sockfd, const struct sockaddr* serv_addr, socklen_t addrlen);
```

errno:
- ECONNREFUSED: target port does not exist
- ETIMEOUT: time out

## 5.7 Closing Connection

close the connection by closing the file descriptor

```
#include <unistd.h>
int close(int fd);
```

close will not instantly close a connection, it will minus the reference of the fd by 1, only when the reference is 0, the connection is really closed

In multi-process programming, a `fork` will increase the reference of the socket from the parent process by 1, so we need to close in both the child process and the parent process

**Close Immediately:**
```
#include <sys/socket.h> // designed for network programming
int shutdown(int sockfd, int howto);
```

howto:
- SHUT_RD: shutdown read part, clear socket receive buffer
- SHUT_WR: shutdown write part, send out all the data in the send buffer
- SHUT_RDWR

## 5.8 Read and Write of Data

### 5.8.1 Read and Write of TCP Data

socket provides some special socket data read/write syscall.

```
#include <sys/types.h>
#include <sys/socket.h>
ssize_t recv(int sockfd, void* buf, size_t len, int flags); // return received length, might need to read multiple times
ssize_t send(int sockfd, const void* buf, size_t len, int flags); // return written length
```

set the flag as MSG_OOB to send OOB data.

The flag is only effective to the current send and recv.

### 5.8.2 Read and Write of UDP data

```
#include <sys/types.h>
#include <sys/socket.h>
ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen);
ssize_t sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen);
```

**recvfrom**:
- read data from sockfd to buf
- because UDP is connectionless, we need to specify the src_addr everytime

**sendto**:
- write data from buf to sockfd
- because UDP is connectionless, we need to specify the dest_addr everytime

We can set the last two arguments as NULL to make the UDP connection STREAM

### 5.8.3 General Read and Write Function

Can be used for bothe TCP and UDP

```
#include <sys/socket.h>
ssize_t recvmsg(int sockfd, struct msghdr* msg, int flags);
ssize_t recvmsg(int sockfd, struct msghdr* msg, int flags);
```

Definition of `msghdr`:
```
struct msghdr {
  void* msg_name; //socket address
  socklen_t msg_namelen; // length of the socket address
  
  struct iovec* msg_iov; // I/O vector
  int msg_iovlen; // the number of the memory
  
  void* msg_control; // support data position
  socklen_t msg_controllen; // size of support data
  
  int msg_flags;
}
```

To TCP, `msg_name` is meaningless because it's STREAM, should be set as NULL.
To UDP, this field should be set.

```
struct iovec {
  void* iov_base; // a piece of memory
  size_t iov_len; // length of the memory
}
```

- To `recvmsg`, the data will be read and stored in the scattered pieces of memory.
- To `sendmsg`, the data will be gathered and then sent

## 5.9 OOB Mark

In practice, we cannot anticipate when the OOB data will come.

Two common ways the kernel telling the application:
- I/O Multiplexing will generate exception
- SIGURG signal

**But the application still needs to know the position of the OOB data**

Use the following syscall:
```
#include <sys/socket.h>
//return 1 if the next data is OOB
int sockatmark(int sockfd);
```

## 5.10 Address Info Functions

Functions to know the socket addresses of the two end:
```
#include <sys/socket.h>
int getsockname(int sockfd, struct sockaddr* address, socklen_t* address_len);
int getpeername(int sockfd, struct sockaddr* address, socklen_t* address_len);
```

given the `sockfd`, put the sockaddr into `address`

## 5.11 Socket Option

Functions to read and modify the socket file descriptor property:
```
#include <sys/socket.h>
int getsockopt(int sockfd, int level, int option_name, void* option_value, socklen_t* restrict option_len);
int setsockopt(int sockfd, int level, int option_name, const void* option_value, socklen_t option_len);
```

<img src="./pics/socket_option.png">

Some socket options can only be set before calling `listen`, because connected socket can only be return by `accept` syscall, so the connections in the listening queue are at least in SYN_RCVD state.
But some socket options should be set in TCP SYN segment.

Solution: set the socket option for the listening socket, let the connected socket inherit these socket option

### 5.11.1 SO_REUSEADDR Option

Let the address be able to be reused:
```
int sock = socket(PF_INET, SOCK_STREAM, 0);
assert(sock >= 0);
int reuse = 1;
setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
struct sockaddr_in address;
bzero(&address, sizeof(address));
address.sin_family = AF_INET;
inet_pton(AF_INET, ip, &address.sin_addr);
address.sin_port = htons(port);
int ret = bind(sock, (struct sockaddr*)&address, sizeof(address));
```

### 5.11.2 SO_RCVBUF and SO_SNDBUF Option

SO_RCVBUF: TCP receiving buffer size: at least 256bytes
SO_SNDBUF: TCP sending buffer size: at least 2048bytes

When we use `setsockopt` to set TCP buffer, the system will multiply it and it should not be smaller than a mininum number.

### 5.11.3 SO_RCVLOWAT and SO_SNDLOWAT Option

Mark the lowest threshold of TCP send buffer and receive buffer.

- When readable data amount > threshold, I/O multiplexing syscall will tell the application to read from the socket.
- When vacant space amount > threshold, I/O multiplexing syscall will tell the application to write into the socket.

By default, 1byte.

### 5.11.4 SO_LINGER Option

Used to control `close` behavior when closing TCP connection

By default, when we call `close`, close will return immediately and TCP module will send the remaining data to the other side.

linger struct:
```
#include <sys/socket.h>
struct linger {
  int l_onoff; // on(1) or off(0)
  int l_linger; // linger time
};
```

- l_onoff == 0, default
- l_onoff != 0, l_linger == 0:
  - close will return immediately
  - TCP will drop the buffer data
  - send an RST to the other side
- l_onoff != 0, l_linger != 0:
  - if blocking, close will wait for l_linger time,
  - if non-blocking, close will return immediately

## 5.12 Network Info API

Two elememts of sockaddr:
- IP address: can be replaced by hostname
- port number: can be replaced by service name(80: www)

### 5.12.1 gethostbyname and gethostbyaddr

```
//return type
#include <netdb.h>
struct hostent {
  char* h_name;//hostname
  char** h_aliases; // host alias
  int h_addrtype; // address family
  int h_length; // address length
  char** h_addr_list; // IP address list in net endian
};
```

```
#include <netdb.h>
struct hostent* gethostbyname(const char* name);
// type: AF_INET, AF_INET6
struct hostend* gethostbyaddr(const void* addr, size_t len, int type);
```

### 5.12.2 getservbyname and getservbyport

```
// return type
#include <netdb.h>
struct servent {
  char* s_name; // service name
  char** s_aliases; 
  int s_port;
  char* s_proto; // tcp or udp
};
```

```
#include <netdb.h>
struct servent* getservbyname(const char* name, const char* proto);
struct servent* getservbyport(int port, const char* proto);
```

### 5.12.3 getaddrinfo

```
#include <netdb.h>
int getaddrinfo(const char* hostname, const char* service, const struct addrinfo* hint, struct addrinfo** result);
```

```
#include <netdb.h>
const char* gai_strerror(int error); // errno -> text
```









  




