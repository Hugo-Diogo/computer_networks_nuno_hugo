// Wite to serial port in non-canonical mode
//
//  Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]
#define _POSIX_SOURCE 1 // POSIX compliant source
#define _POSIX_C_SOURCE 199309L
#include "link_layer.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

extern volatile int STOP;
extern int alarmEnabled;
extern int alarmCount;

unsigned char buf_temp = {0};

//--------------------------------------------------------------------main
int main(int argc, char *argv[]) {

  // Set alarm function handler.
  // Install the function signal to be automatically invoked when the timer
  // expires, invoking in its turn the user function alarmHandler
  struct sigaction act = {0};
  act.sa_handler = &alarmHandler;
  if (sigaction(SIGALRM, &act, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  char *path = choose_file();
  FILE *file = open_file(path);

  // tamanho
  fseek(file, 0, SEEK_END);
  long filesize = ftell(file);
  fseek(file, 0, SEEK_SET);

  // nome do ficheiro
  char *filename = strrchr(path, '/');

  if (filename != NULL)
    filename++;
  else
    filename = path;

  // Program usage: Uses either COM1 or COM2
  const char *serialPortName = argv[1];

  if (argc < 2) {
    printf("Incorrect program usage\n"
           "Usage: %s <SerialPort>\n"
           "Example: %s /dev/ttyS1\n",
           argv[0], argv[0]);
    exit(1);
  }

  // Open serial port device for reading and writing, and not as controlling tty
  // because we don't want to get killed if linenoise sends CTRL-C.
  int fd = llopen(serialPortName);

  if (fd < 0) {
    perror(serialPortName);
    exit(-1);
  }

  struct termios oldtio;
  struct termios newtio;

  // Save current port settings
  if (tcgetattr(fd, &oldtio) == -1) {
    perror("tcgetattr");
    exit(-1);
  }

  // Clear struct for new port settings
  memset(&newtio, 0, sizeof(newtio));

  newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
  newtio.c_iflag = IGNPAR;
  newtio.c_oflag = 0;

  // Set input mode (non-canonical, no echo,...)
  newtio.c_lflag = 0;
  newtio.c_cc[VTIME] = 0; // Inter-character timer unused
  newtio.c_cc[VMIN] = 5;  // Blocking read until 5 chars received

  // VTIME e VMIN should be changed in order to protect with a
  // timeout the reception of the following character(s)

  // Now clean the line and activate the settings for the port
  // tcflush() discards data written to the object referred to
  // by fd but not transmitted, or data received but not read,
  // depending on the value of queue_selector:
  //   TCIFLUSH - flushes data received but not read.
  tcflush(fd, TCIOFLUSH);

  // Set new port settings
  if (tcsetattr(fd, TCSANOW, &newtio) == -1) {
    perror("tcsetattr");
    exit(-1);
  }
  unsigned char frame_begin[5];

  int s_begin = send_CONN(frame_begin);
  if (!send_with_retry(fd, frame_begin, s_begin)) {
    close(fd);
    return -1;
  }

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////. start
  /// packet.///////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  
    struct timespec start, end;
    
    
    // Create string to send
  unsigned char frame[BUF_SIZE];

    clock_gettime(CLOCK_MONOTONIC, &start);

  unsigned char start_packet[512];
  memset(start_packet, 0, 512);
  int start_size = build_start_packet(start_packet, filesize, filename);

  int size = build_frame(frame, start_packet, start_size, 0);
  print_hex(start_packet, start_size);

  if (!send_with_retry(fd, frame, size)) {
    close(fd);
    return -1;
  }

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////. data
  /// packet.///////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  int bytes = 0;
  unsigned char buf_file[BUF_file_SIZE] = {0};
  unsigned char buf_application[BUF_file_SIZE] = {0};
  int j = 1;
  while ((bytes = fread(buf_file, 1, BUF_file_SIZE - 3, file)) != 0) {

    buf_application[0] = 1;
    buf_application[1] = (bytes >> 8) & 0xFF;
    buf_application[2] = bytes & 0xFF;

    memcpy(&buf_application[3], buf_file, bytes);

    size = build_frame(frame, buf_application, bytes + 3, j);
    print_hex(frame, size);
    j++;
    int res_send = send_with_retry(fd, frame, size);

    }

  
  printf("last packet sent.\n");
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////. end
  /// packet.///////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  unsigned char end_packet[512];
  memset(end_packet, 0, 512);
  int end_size = build_end_packet(end_packet, filesize, filename);
  size = build_frame(frame, end_packet, end_size, j);

  send_with_retry(fd, frame, size);

  print_hex(frame, end_size);

   clock_gettime(CLOCK_MONOTONIC, &end);
    
    double time = 
            (end.tv_sec - start.tv_sec) + 
            (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Transfer time: %f seconds.\n", time);
    
    double R = (filesize * 8) / time;

    printf("Rb (Bitrate): %f bps\n", R);

    double C = BAUDRATE;
    double S = R / C;

    printf("Efficiency S = %f\n", S);

  tcsetattr(fd, TCSANOW, &oldtio);
  close(fd);

  return 0;
}
