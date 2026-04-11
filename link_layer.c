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

volatile int STOP = FALSE;

int alarmEnabled = FALSE;
int alarmCount = 0;

// -------------------- DEBUG HEX --------------------
void print_hex(unsigned char *buf, int size) {
  for (int i = 0; i < size; i++) {
    printf("%02X ", buf[i]);
  }
  printf("\n");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////
/// transmitter///////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////

char *choose_file() {
  static char path[1024] = {0};
  FILE *fp = NULL;

#if defined(_WIN32)

  fp = _popen(
      "powershell -command \"Add-Type -AssemblyName System.Windows.Forms; "
      "$f = New-Object System.Windows.Forms.OpenFileDialog; "
      "if($f.ShowDialog() -eq 'OK'){ $f.FileName }\"",
      "r");

#elif defined(__APPLE__)

  fp = popen("osascript -e 'POSIX path of (choose file)'", "r");

#elif defined(__linux__)

  fp = popen("zenity --file-selection 2>/dev/null", "r");

  if (fp == NULL || fgets(path, sizeof(path), fp) == NULL) {
    if (fp)
      pclose(fp);
    fp = popen("kdialog --getopenfilename 2>/dev/null", "r");
  }

#else
  // fallback simples
  printf("Enter file path: ");
  fgets(path, sizeof(path), stdin);
  path[strcspn(path, "\n")] = 0;
  return path;
#endif

  if (fp == NULL) {
    return NULL;
  }

  if (path[0] == '\0') {
    if (fgets(path, sizeof(path), fp) == NULL) {
#if defined(_WIN32)
      _pclose(fp);
#else
      pclose(fp);
#endif
      return NULL;
    }
  }

  // remover newline
  path[strcspn(path, "\n")] = 0;

#if defined(_WIN32)
  _pclose(fp);
#else
  pclose(fp);
#endif

  return path;
}

int llopen(const char *serialPortName) {

  int fd = open(serialPortName, O_RDWR | O_NOCTTY);

  if (fd < 0) {
    perror("Error opening serial port");
    return -1;
  }

  // printf("Serial port opened: %s\n", serialPortName);

  return fd;
}

FILE *open_file(const char *filePath) {
  FILE *fp = fopen(filePath, "rb");
  if (fp == NULL) {
    perror("Error opening file");
    return NULL;
  }
  // printf("File opened: %s\n", filePath);
  return fp;
}

int build_start_packet(unsigned char *packet, long filesize, char *filename) {

  int index = 0;

  // -------------------
  // C field (START)
  packet[index++] = 0x02;

  // -------------------
  // T1 = file size
  packet[index++] = 0x00;

  // calcular quantos bytes são necessários para o filesize
  unsigned char size_bytes[8];
  int size_len = 0;

  long temp = filesize;

  do {
    size_bytes[size_len++] = temp & 0xFF;
    temp >>= 8;
  } while (temp > 0);

  // L1
  packet[index++] = size_len;

  // V1 (big endian)
  for (int i = size_len - 1; i >= 0; i--) {
    packet[index++] = size_bytes[i];
  }

  // -------------------
  // T2 = filename
  packet[index++] = 0x01;

  int name_len = strlen(filename);

  // L2
  packet[index++] = name_len;

  // V2
  memcpy(&packet[index], filename, name_len);
  index += name_len;

  return index;
}

int send_CONN(unsigned char *frame) {
  frame[0] = FLAG;
  frame[1] = A_RX;
  frame[2] = 0x03;
  frame[3] = frame[1] ^ frame[2];
  frame[4] = FLAG;
  return 5;
}

int build_end_packet(unsigned char *packet, long filesize, char *filename) {

  int index = 0;

  // -------------------

  // C field (START)

  packet[index++] = 0x03;

  packet[index++] = 0x00;

  return index;
}
//-------------------------------------------------------------alarm

// Alarm function handler.
// This function will run whenever the signal SIGALRM is received.
void alarmHandler() {
  alarmEnabled = FALSE;
  alarmCount++;

  // printf("Alarm #%d received\n", alarmCount);
}

// -------------------- SEND WITH RETRIES --------------------
int send_with_retry(int fd, unsigned char *frame, int size) {

  alarmCount = 0;
  alarmEnabled = FALSE;

  while (alarmCount < 3) {


    if (!alarmEnabled) {
      alarm(3);
      alarmEnabled = TRUE;

      printf("Sending...\n");
      print_hex(frame, size);

      write(fd, frame, size);
    }

    unsigned char resp[5];
    int r = read(fd, resp, 5);
printf("%02X\n", resp[2]);


      if (resp[2] == 0x05 || resp[2] == 0x85 || resp[2] == 0x0B) {
        printf("RR received ✅\n");
        alarm(0);
        return 1;
      }
    
  }

  printf("❌ No response after 3 tries. Exiting.\n");
  return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  frames
/// creater/////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////

// -------------------- BUILD FRAME --------------------
int build_frame(unsigned char *frame, unsigned char *data, int data_size,
                int j) {

  unsigned char stuffed[BUF_SIZE];
  int stuffed_size = 0;
  unsigned char bcc2 = 0;

  // stuffing + BCC2
  for (int i = 0; i < data_size; i++) {

    bcc2 ^= data[i];

    if (data[i] == 0x7E) {
      stuffed[stuffed_size++] = 0x7D;
      stuffed[stuffed_size++] = 0x5E;
    } else if (data[i] == 0x7D) {
      stuffed[stuffed_size++] = 0x7D;
      stuffed[stuffed_size++] = 0x5D;
    } else {
      stuffed[stuffed_size++] = data[i];
    }
  }

  // header
  frame[0] = FLAG;
  frame[1] = A_TX;
  if (j % 2 == 0) {
    frame[2] = 0x00;
  } else {
    frame[2] = 0x40;
  }
  frame[3] = frame[1] ^ frame[2];

  memcpy(&frame[4], stuffed, stuffed_size);

  int idx = 4 + stuffed_size;
  if (bcc2 == 0x7E) {
    frame[idx++] = 0x7D;
    frame[idx++] = 0x5E;
  } else if (bcc2 == 0x7D) {
    frame[idx++] = 0x7D;
    frame[idx++] = 0x5D;
  } else {
    frame[idx++] = bcc2;
  }
  frame[idx++] = FLAG;

  return idx;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////
/// receiver/////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////

void send_RR(int fd, int r) {

  unsigned char frame[5];

  frame[0] = FLAG;
  frame[1] = A_RX;

  if (r == 0)
    frame[2] = 0x05;
  else
    frame[2] = 0x85;

  frame[3] = frame[1] ^ frame[2];
  frame[4] = FLAG;

  write(fd, frame, 5);
}

void send_REJ(int fd, int r) {

  unsigned char frame[5];

  frame[0] = FLAG;
  frame[1] = A_RX;

  if (r == 0)
    frame[2] = 0x01;
  else
    frame[2] = 0x81;

  frame[3] = frame[1] ^ frame[2];
  frame[4] = FLAG;

  write(fd, frame, 5);
}

void send_DISC(int fd) {

  unsigned char frame[5];

  frame[0] = FLAG;
  frame[1] = A_RX;
  frame[2] = 0x0B;

  frame[3] = frame[1] ^ frame[2];
  frame[4] = FLAG;

  write(fd, frame, 5);
}

long distuffing(unsigned char *buf, int size, unsigned char *destuffed) {
  int j = 0;
  for (int i = 0; i < size; i++) {
    if (buf[i] == 0x7D) {
      if (buf[i + 1] == 0x5E) {
        destuffed[j++] = 0x7E;
        i++;
      } else if (buf[i + 1] == 0x5D) {
        destuffed[j++] = 0x7D;
        i++;
      }
    } else {
      destuffed[j++] = buf[i];
    }
  }
  return j;
}

FILE *handle_start_packet(unsigned char *buf, int size) {

  FILE *fp = NULL;
  char filename[256] = {0};

  int i = 1;

  while (i < size) {

    unsigned char T = buf[i++];
    unsigned char L = buf[i++];

    if (i + L > size)
      break;

    if (T == 1) { // filename

      memcpy(filename, &buf[i], L);
      filename[L] = '\0';

      printf("Filename: %s\n", filename);

      fp = fopen(filename, "wb");

      if (!fp) {
        perror("fopen");
        exit(1);
      }
    }

    i += L;
  }

  return fp;
}
void handle_data_packet(unsigned char *buf, FILE *fp) {

  int data_size = buf[1] * 256 + buf[2];

  if (data_size <= 0)
    return;

  if (fp) {
    fwrite(&buf[3], 1, data_size, fp);
  }

  printf("Wrote %d bytes\n", data_size);
}
void handle_end_packet(FILE *fp) {

  if (fp) {
    fclose(fp);
  }

  printf("File transfer complete\n");
}


