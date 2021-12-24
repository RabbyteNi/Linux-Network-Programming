/* HTTP request reading and analysis
*/
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include <libgen.h>

#define BUFFER_SIZE 4096 // read buffer size

// two states: analysing request line, analysing header
enum CHECK_STATE{CHECK_STATE_REQUESTLINE=0, CHECK_STATE_HEADER};

// LINK_OK: a complete line
// LINK_BAD: wrong line
// LINE_OPEN: incomplete line
enum LINE_STATUS{LINE_OK=0, LINE_BAD, LINE_OPEN};

// parsing result:
// NO_REQUEST: incomplete, continue to read
// GET_REQUEST: complete request
// BAD_REQUEST: syntax error
// FORBIDDEN_REQUEST: client has no previledge to access
// INTERNAL_ERROR
// CLOSED_CONNECTION: the client has closed

enum HTTP_CODE{NO_REQUEST, GET_REQUEST, BAD_REQUEST, FORBIDDEN_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};

// simplified return message
static const char* szret[] = {"I get a correct result\n", "Something wrong\n"};

LINE_STATUS parse_line(char* buffer, int& checked_index, int& read_index) {
  char temp;
  // bytes before checked_index have been analyzed, bytes after checked_index before read_index are waiting for being checked
  while (checked_index < read_index) {
    temp = buffer[checked_index];
    if (temp == '\r') {
      // might be a complete line
      if (checked_index+1 == read_index) {
        // not a complete line
        return LINE_OPEN;
      }
      else if (buffer[checked_index+1] == '\n') {
        // complete line
        buffer[checked_index++] = '\0';
        buffer[checked_index++] = '\0';
        return LINE_OK;
      }
      else {
        return LINE_BAD;
      }
    }
    else if (temp == '\n') {
      // might be a complete line
      if (checked_index > 1 && buffer[checked_index-1] == '\r') {
        // is a complete line
        buffer[checked_index-1] = '\0';
        buffer[checked_index++] = '\0';
        return LINE_OK;
      }
      else {
        return LINE_BAD;
      }
    }
    checked_index++;
  }
  return LINE_OPEN;
}


// parse request
// GET \t http://www.baidu.com/index.html \t HTTP/1.0
HTTP_CODE parse_requestline(char* temp, CHECK_STATE& checkstate) {
  // there needs to be a \t in the request line
  // strpbrk: return the first occurence in the first argument of the second argument
  char* url = strpbrk(temp, "\t");
  if (!url) {
    return BAD_REQUEST;
  }
  *url++ = '\0';
  char* method = temp;
  if (strcasecmp(method, "GET") == 0) {
    printf("The request method is GET\n");
  }
  else {
    return BAD_REQUEST;
  }
  // return the count of \t in the url
  url += strspn(url, "\t");
  char* version = strpbrk(url, "\t");
  if (!version) {
    return BAD_REQUEST;
  }
  *version++ = '\0';
  version += strspn(version, "\t");
  if (strcasecmp(version, "HTTP/1.1") != 0) {
    return BAD_REQUEST;
  }

  // check url
  if (strncasecmp(url, "http://", 7) == 0) {
    url += 7;
    // return the first occurence of /
    url = strchr(url, '/');
  }

  if (!url || url[0] != '/') {
    return BAD_REQUEST;
  }

  printf("The request URL is: %s\n", url);

  // check header
  checkstate = CHECK_STATE_HEADER;
  return NO_REQUEST;
}

// Host:www.baidu.com   
HTTP_CODE parse_headers(char* temp) {
  // empty line
  if (temp[0] == '\0') {
    return GET_REQUEST;
  }
  else if(strncasecmp(temp, "Host:", 5) == 0) {
    temp += 5;
    temp += strspn(temp, "\t");
    printf("the request host is: %s\n", temp);
  }
  else {
    printf("I cannot handle this header\n");
  }
  return NO_REQUEST;
}


HTTP_CODE parse_content(
    char* buffer, 
    int& checked_index, 
    CHECK_STATE& checkstate, 
    int& read_index, 
    int& start_line) { // the start point in the buffer
  LINE_STATUS linestatus = LINE_OK;
  HTTP_CODE retcode = NO_REQUEST;

  while((linestatus = parse_line(buffer, checked_index, read_index)) == LINE_OK) {
    char* temp = buffer + start_line;
    start_line = checked_index; // start of the next line

    switch (checkstate) {
      case CHECK_STATE_REQUESTLINE: 
        retcode = parse_requestline(temp, checkstate);
        if (retcode == BAD_REQUEST) {
          return BAD_REQUEST;
        }
        break;
      case CHECK_STATE_HEADER:
        retcode = parse_headers(temp);
        if (retcode == BAD_REQUEST) {
          return BAD_REQUEST;
        }
        else if (retcode == GET_REQUEST) {
          return GET_REQUEST;
        }
        break;
      default:
        return INTERNAL_ERROR;
    }
  }

  if (linestatus == LINE_OPEN) {
    return NO_REQUEST;
  }
  else {
    return BAD_REQUEST;
  }
}


int main(int argc, char* argv[]) {
  if (argc <= 2) {
    printf("usage: %s ip_address port_number\n", basename(argv[0]));
    return EXIT_FAILURE;
  }

  const char* ip = argv[1];
  int port = atoi(argv[2]);

  struct sockaddr_in address;
  bzero(&address, sizeof(address));
  address.sin_port = AF_INET;
  inet_pton(AF_INET, ip, &address.sin_addr);
  address.sin_port = htons(port);

  int sock = socket(PF_INET, SOCK_STREAM, 0);
  assert(sock >= 0);

  int ret = bind(sock, (struct sockaddr*)&address, sizeof(address));
  assert(ret != -1);

  ret = listen(sock, 5);
  assert(ret != -1);

  struct sockaddr_in client;
  socklen_t client_addrlength = sizeof(client);

  int fd = accept(sock, (struct sockaddr*)&client, &client_addrlength);
  if (fd < 0) {
    printf("errno is:%d\n", errno);
    return EXIT_FAILURE;
  }
  char buffer[BUFFER_SIZE];
  memset(buffer, '\0', BUFFER_SIZE);
  int data_read = 0;
  int read_index = 0; // have read data
  int checked_index = 0; // have analyzed
  int start_line = 0;

  CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE;
  while (true) {
    data_read = recv(fd, buffer+read_index, BUFFER_SIZE-read_index, 0);
    if (data_read == -1) {
      printf("reading failed\n");
      break;
    }
    else if(data_read == 0) {
      printf("remote client has closed the connection\n");
      break;
    }
    read_index += data_read;
    HTTP_CODE result = parse_content(buffer, checked_index, checkstate, read_index, start_line);
    if (result == NO_REQUEST) {
      continue;
    }
    else if (result == GET_REQUEST) {
      send(fd, szret[0], strlen(szret[0]), 0);
      break;
    }
    else { // error
      send(fd, szret[1], strlen(szret[1]), 0);
      break;
    }
  }
  close(fd);
  close(sock);
  return EXIT_SUCCESS;
}












