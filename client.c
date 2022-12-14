#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>    // close()
#include <errno.h>
#include <arpa/inet.h> // inet_addr()
#include <getopt.h>    // getopt()
#include <stdbool.h>   // bool
#include <sys/time.h>
#include <time.h>

#define MAX_TICK   700
#define MAX_LINE   4096
#define MAX_MEMORY 25000000.0

void getCmdOptions(int argc, char* argv[], char**, int*, char**, int*);
void usageError(char*);
bool readSpecFile(char*, unsigned int**, unsigned int**, int*);
void
sendPeriodic(int, unsigned int*, unsigned int*, int, int, struct sockaddr_in);
void sendMessage(unsigned int len,
                 int sockfd,
                 struct sockaddr_in serv_addr,
                 int tick_n,
                 int i);
static char* rand_string(char*, size_t);
void fatal(char*);

int
main(int argc, char* argv[])
{
  char *serv_IP, *input_file;
  int serv_port, tick_size;
  //
  int N;
  unsigned int *periods, *lens;
  unsigned int periods_dflt[] = { 1 }, lens_dflt[] = { 1500 };
  //
  int sockfd = 0, n = 0;
  struct sockaddr_in serv_addr;
  //
  char buffer[MAX_LINE], msg[MAX_LINE];
  char* hello = "Hello from client";
  struct timeval tp;

  // Get command-line options
  getCmdOptions(argc, argv, &serv_IP, &serv_port, &input_file, &tick_size);

  // Read the specification file
  if( input_file == NULL ) {
    N = 1;
    periods = periods_dflt;
    lens = lens_dflt;
  }
  else if( !readSpecFile(input_file, &periods, &lens, &N) ) {
    fprintf(stderr, "client: Unable to read file `%s'\n", input_file);
    exit(EXIT_FAILURE);
  }

  /* Create socket
   * AF_INET        –  IPv4 (namespace / address family)
   * SOCK_DGRAM     –  UDP  (communication style)
   * 0 (IPPROTO_IP) –  IP   (protocol)
   * */
  if( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
    perror("client: Could not create socket");
    exit(EXIT_FAILURE);
  }

  /*
   * Fill server address struct
   */
  memset(&serv_addr, 0, sizeof(serv_addr)); // ??
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(serv_port);
  // Convert the IP address to long, and load in serv_addr.sin_addr
  if( inet_pton(AF_INET, serv_IP, &serv_addr.sin_addr) <= 0 ) {
    perror("client: inet_pton error occurred");
    exit(EXIT_FAILURE);
  }
  /*
   * */

  // Send message to the server
  n = sendto(sockfd, (const char*)hello, strlen(hello), MSG_CONFIRM,
             (const struct sockaddr*)&serv_addr, sizeof(serv_addr));
  if( n < 0 ) {
    perror("client: sendto (1)");
    exit(EXIT_FAILURE);
  }
  // Receive a response
  socklen_t len;
  n = recvfrom(sockfd, (char*)buffer, MAX_LINE, MSG_WAITALL,
               (struct sockaddr*)&serv_addr, &len);
  if( n < 0 ) {
    perror("client: recvfrom");
    exit(EXIT_FAILURE);
  }
  /*
   * buffer[n] = '\0';
   * printf("Server's response: %s\n", buffer);
   */
  // Send response, telling when the message was received
  gettimeofday(&tp, NULL);
  sprintf(msg, "client (received): %ld\n", tp.tv_sec);
  n = sendto(sockfd, (const char*)msg, strlen(msg), MSG_CONFIRM,
             (const struct sockaddr*)&serv_addr, sizeof(serv_addr));
  if( n < 0 ) {
    perror("client: sendto (2)");
    exit(EXIT_FAILURE);
  }
  // Log the same message to the console
  printf("%s", msg);

  // Start sending periodic messages
  sendPeriodic(tick_size, periods, lens, N, sockfd, serv_addr);

  close(sockfd);
  return EXIT_SUCCESS;
}


// Sends messages to the server periodically
void
sendPeriodic(int tick_size,
             unsigned int* periods,
             unsigned int* lens,
             int N,
             int sockfd,
             struct sockaddr_in serv_addr)
{
  struct timespec request = { (time_t)(tick_size / 1000),
                              (long int)(tick_size % 1000) * 1000000L },
                  remn;
  int tick_n, i;
  struct timeval tp;

  /*
   * main_arr [max_period x N] – 2-dimensional array
   * The arrays main_arr[main_tn_index], ..., main_arr[max_period-1],
   * main_arr[0], ..., main_arr[main_tn_index-1] correspond to the
   * tick numbers tick_n, ..., tick_n + max_period - 1.
   * They represent lists of indexes from {0,1,...,N-1}: namely, i is in
   * the list corresponding to the smallest number, not less than tick_n,
   * divisible by period[i].
   */
  size_t* main_arr;
  /*
   * main_lengths [max_period] – array of lengths of the lists
   * main_arr[0], ..., main_arr[max_period-1] (from 0 to N)
   */
  size_t* main_lengths;
  // main_tn_index – index in main_arr, corresponding to tick_n
  size_t main_tn_index = 0;
  size_t index, j, k, m;
  unsigned int max_period = 1;

  // Seed random generator
  gettimeofday(&tp, NULL);
  srand(tp.tv_usec);

  // Find the maximum period
  for( i = 0; i < N; i++ ) {
    if( periods[i] > max_period ) {
      max_period = periods[i];
    }
  }
  // Simple algorithm
  if( (float)max_period * (float)N > MAX_MEMORY ) {
    for( tick_n = 1; tick_n <= MAX_TICK; tick_n++ ) {
      nanosleep(&request, &remn);
      // Find out which periods divide the tick number
      for( i = 0; i < N; i++ ) {
        if( tick_n % periods[i] == 0 )
          sendMessage(lens[i], sockfd, serv_addr, tick_n, i);
      }
    }
    return;
  }

  // O(1) algorithm
  if( !(main_arr = malloc(sizeof(size_t) * max_period * N)) )
    fatal("client: Virtual memory exhausted");
  if( !(main_lengths = calloc(max_period, sizeof(size_t))) )
    fatal("client: Virtual memory exhausted");
  // Initial filling of main_arr
  for( j = 0; j < N; j++ ) {
    index = (size_t)(periods[j] - 1);
    main_arr[index * N + main_lengths[index]] = j;
    main_lengths[index]++;
  }

  // Main loop
  for( tick_n = 1; tick_n <= MAX_TICK; tick_n++ ) {
    nanosleep(&request, &remn);
    // Remember and zero main_lengths[main_tn_index]
    m = main_lengths[main_tn_index];
    main_lengths[main_tn_index] = 0;
    // Process indexes in main_arr[main_tn_index]
    for( k = 0; k < m; k++ ) {
      i = main_arr[main_tn_index * N + k];
      // Send a message
      sendMessage(lens[i], sockfd, serv_addr, tick_n, i);
      index = (main_tn_index + periods[i]) % max_period;
      // Update main_arr
      main_arr[index * N + main_lengths[index]] = i;
      main_lengths[index]++;
    }
    // Update main_tn_index
    main_tn_index = (main_tn_index + 1) % max_period;
  }
  free(main_arr);
  free(main_lengths);
}


// Sends a random message of length len to the server
void
sendMessage(unsigned int len,
            int sockfd,
            struct sockaddr_in serv_addr,
            int tick_n,
            int i)
{
  static char msg[MAX_LINE];
  int n;
  // Generate a message of length len
  rand_string(msg, len);
  // Send the message
  n = sendto(sockfd, (const char*)msg, strlen(msg), MSG_CONFIRM,
             (const struct sockaddr*)&serv_addr, sizeof(serv_addr));
  if( n < 0 ) {
    fprintf(stderr, "client: sendto (tick# %d, line# %d, len = %d): ", tick_n,
            i + 1, len);
    perror("");
    exit(EXIT_FAILURE);
  }
}


/*
 * Gets options from command line; prints error message and exits if options
 * are specified incorrectly
 * Output arguments: serv_IP, serv_port, input_file, tick_size
 * */
void
getCmdOptions(int argc,
              char* argv[],
              char** serv_IP,
              int* serv_port,
              char** input_file,
              int* tick_size)
{
  int opt;
  *input_file = NULL;
  *tick_size = 100;
  bool serv_IP_fnd = false, serv_port_fnd = false;

  while( (opt = getopt(argc, argv, "s:p:i:t:")) != -1 ) {
    switch( opt ) {
    case 's':
      *serv_IP = optarg;
      serv_IP_fnd = true;
      break;
    case 'p':
      *serv_port = atoi(optarg);
      serv_port_fnd = true;
      break;
    case 'i':
      *input_file = optarg;
      break;
    case 't':
      *tick_size = atoi(optarg);
      break;
    default: /* '?' */
      usageError(argv[0]);
    }
  }
  if( !serv_IP_fnd || !serv_port_fnd ) {
    usageError(argv[0]);
  }
}


void
usageError(char* program_name)
{
  fprintf(stderr, "client: Usage: %s -s <server_IP> −p <server_port> [-i\
 <input_file>] [-t <tick_size_(ms)>]\n",
          program_name);
  exit(EXIT_FAILURE);
}


/*
 * Reads specification from the input file; returns true if read successfully
 * Output arguments: periods, lens, N
 * */
bool
readSpecFile(char* file_path,
             unsigned int** periods,
             unsigned int** lens,
             int* N)
{
  FILE* fstream;
  int size = 50, i, scanned;

  if( !(fstream = fopen(file_path, "r")) ) {
    perror("client: fopen");
    return false;
  }

  if( !(*periods = malloc(sizeof(unsigned int) * size)) )
    fatal("client: Virtual memory exhausted");
  if( !(*lens = malloc(sizeof(unsigned int) * size)) )
    fatal("client: Virtual memory exhausted");

  char line[MAX_LINE];

  for( i = 0; fgets(line, MAX_LINE, fstream); i++ ) {
    if( i >= size ) {
      size *= 2;
      if( !(*periods = realloc(*periods, sizeof(unsigned int) * size)) )
        fatal("client: Virtual memory exhausted");
      if( !(*lens = realloc(*lens, sizeof(unsigned int) * size)) )
        fatal("client: Virtual memory exhausted");
    }
    scanned = sscanf(line, "%u %u\n", *periods + i, *lens + i);
    if( !(scanned == 2 && (*lens)[i] <= 1500 && (*periods)[i] >= 1) )
      break;
  }

  fclose(fstream);
  *N = size = i;
  if( !(*periods = realloc(*periods, sizeof(unsigned int) * size)) )
    fatal("client: Virtual memory exhausted");
  if( !(*lens = realloc(*lens, sizeof(unsigned int) * size)) )
    fatal("client: Virtual memory exhausted");
  return true;
}


static char*
rand_string(char* str, size_t len)
{
  const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ\
RSTUVWXYZ0123456789,.-#'?!";
  for( size_t n = 0; n < len; n++ ) {
    int key = rand() % (int)(sizeof charset - 1);
    str[n] = charset[key];
  }
  str[len] = '\0';
  return str;
}


void
fatal(char* error_msg)
{
  fprintf(stderr, "%s", error_msg);
  exit(EXIT_FAILURE);
}
