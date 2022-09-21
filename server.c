#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <getopt.h>
#include <stdbool.h>
#include <sys/time.h>
#include <signal.h>

#define MAX_LINE 4096
#define LOG_PATH "server-log.txt"

// Global variables for termination signal handler
volatile sig_atomic_t termination_in_progress = 0;
size_t n_recv = 0;
char** recv_log;

void dump_log(char**, size_t);
void termination_handler(int);
void fatal(char*);

int
main(int argc, char* argv[])
{
  int opt;
  int port;
  bool portFnd = false;
  //
  int sockfd = 0, n = 0;
  char buffer[MAX_LINE], msg[MAX_LINE];
  struct sockaddr_in addr, cli_addr;
  //
  struct timeval tp;
  //
  size_t N_recv = 2000;

  while( (opt = getopt(argc, argv, "p:")) != -1 ) {
    switch( opt ) {
    case 'p':
      port = atoi(optarg);
      portFnd = true;
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s −p port\n", argv[0]);
      exit(EXIT_FAILURE);
    }
  }
  if( !portFnd ) {
    fprintf(stderr, "Usage: %s −p port\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  // Creating socket file descriptor (IPv4, UDP)
  if( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
    perror("server: Could not create socket");
    exit(EXIT_FAILURE);
  }

  memset(&addr, 0, sizeof(addr));
  memset(&cli_addr, 0, sizeof(cli_addr));

  // Filling server address information
  addr.sin_family = AF_INET; // IPv4
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  // Bind the socket with the server address
  if( bind(sockfd, (const struct sockaddr*)&addr, sizeof(addr)) < 0 ) {
    perror("server: bind failed");
    exit(EXIT_FAILURE);
  }

  // Wait for a message from client
  socklen_t len;
  len = sizeof(cli_addr); // len is value/result
  n = recvfrom(sockfd, (char*)buffer, MAX_LINE, MSG_WAITALL,
               (struct sockaddr*)&cli_addr, &len);
  if( n < 0 ) {
    perror("server: recvfrom");
    exit(EXIT_FAILURE);
  }
  // Send response, telling when the message was received
  gettimeofday(&tp, NULL);
  sprintf(msg, "server (response sent): %ld\n", tp.tv_sec);
  n = sendto(sockfd, (const char*)msg, strlen(msg), MSG_CONFIRM,
             (const struct sockaddr*)&cli_addr, len);
  if( n < 0 ) {
    perror("server: sendto");
    exit(EXIT_FAILURE);
  }
  // Log the same message to the console
  printf("%s", msg);

  if( !(recv_log = malloc(sizeof(char*) * N_recv)) )
    fatal("server: Virtual memory exhausted");

  // Assign the termination handler
  if( signal(SIGINT, termination_handler) == SIG_IGN )
    signal(SIGINT, SIG_IGN);
  if( signal(SIGHUP, termination_handler) == SIG_IGN )
    signal(SIGHUP, SIG_IGN);
  if( signal(SIGTERM, termination_handler) == SIG_IGN )
    signal(SIGTERM, SIG_IGN);

  while( true ) {
    // Wait for messages
    n = recvfrom(sockfd, (char*)buffer, MAX_LINE, MSG_WAITALL,
                 (struct sockaddr*)&cli_addr, &len);
    if( n < 0 ) {
      perror("server: recvfrom");
      dump_log(recv_log, n_recv);
      exit(EXIT_FAILURE);
    }
    // Log info: when the message was received; message length
    gettimeofday(&tp, NULL);
    sprintf(msg, "t = %ld.%03ld,  len = %d\n", tp.tv_sec, tp.tv_usec / 1000, n);
    if( n_recv >= N_recv ) {
      N_recv *= 2;
      if( !(recv_log = realloc(recv_log, sizeof(char*) * N_recv)) )
        fatal("server: Virtual memory exhausted");
    }
    if( !(recv_log[n_recv] = malloc(strlen(msg) + 1)) )
      fatal("server: Virtual memory exhausted");
    strcpy(recv_log[n_recv], msg);
    n_recv++;
  }

  close(sockfd);
  dump_log(recv_log, n_recv);
  return EXIT_SUCCESS;
}


/*
 * Dump the recording into a file
 */
void
dump_log(char** log, size_t size)
{
  FILE* file;
  if( !(file = fopen(LOG_PATH, "w")) ) {
    perror("server: fopen");
    exit(EXIT_FAILURE);
  }
  for( size_t i = 0; i < size; i++ ) {
    fprintf(file, "%s", log[i]);
  }
  fclose(file);
}


void
termination_handler(int signum)
{
  if( termination_in_progress )
    raise(signum);
  termination_in_progress = 1;

  dump_log(recv_log, n_recv);

  signal(signum, SIG_DFL);
  raise(signum);
}


void
fatal(char* error_msg)
{
  fprintf(stderr, "%s", error_msg);
  exit(EXIT_FAILURE);
}
