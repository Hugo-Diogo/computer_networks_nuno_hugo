// Write to serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 5

volatile int STOP = FALSE;

int alarmEnabled = FALSE;
int alarmCount = 0;
unsigned char buf_temp = {0};

//-------------------------------------------------------------functions

// Alarm function handler.
// This function will run whenever the signal SIGALRM is received.
void alarmHandler(int signal) {
  alarmEnabled = FALSE;
  alarmCount++;

  printf("Alarm #%d received\n", alarmCount);
}

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
  int fd = open(serialPortName, O_RDWR | O_NOCTTY);

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

  printf("New termios structure set\n");

  // Create string to send
  unsigned char buf[BUF_SIZE] = {0};

  /*for (int i = 0; i < BUF_SIZE; i++)
  {
      buf[i] = 'a' + i % 26;
  } */
  // Definition of buffer
  buf[0] = 0x7E;
  buf[1] = 0x03;
  buf[2] = 0x03;
  buf[3] = buf[1] ^ buf[2];
  buf[4] = buf[0];

  buf_temp = buf[2];

  while (alarmCount < 4) {
    if (alarmEnabled == FALSE) {
      alarm(3); // Set alarm to be triggered in 3s
      alarmEnabled = TRUE;

      printf("Alarm configured\n");

      // In non-canonical mode, '\n' does not end the writing.
      // Test this condition by placing a '\n' in the middle of the buffer.
      // The whole buffer must be sent even with the '\n'.
      // buf[5] = '\n';

      int bytes = write(fd, buf, BUF_SIZE);
      printf("%d bytes written\n", bytes);
    }
    int bytes_write = read(fd, buf, BUF_SIZE);

    

    if (bytes_write != -1) {
      alarm(0);
      alarmCount = 4;
      for (int i = 0; i < 5; i++) {
        printf("var = 0x%02X\n", buf[i]);
      }
      printf("Got an answer\n");
    }else{
        printf("Got NO answer\n");
    }
  }

  // Wait until all bytes have been written to the serial port
  sleep(1);

  // Restore the old port settings
  if (tcsetattr(fd, TCSANOW, &oldtio) == -1) {
    perror("tcsetattr");
    exit(-1);
  }

  close(fd);

  return 0;
}
