#include <stdio.h>

#define LINK_LAYER_H
#define FLAG 0x7E
#define A_TX 0x03
#define FLAG 0x7E
#define A_RX 0x01
// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 1024
#define BUF_file_SIZE 500

extern volatile int STOP;
extern int alarmEnabled;
extern int alarmCount;

void print_hex(unsigned char *buf, int size);
char *choose_file();
int llopen(const char *serialPortName);

FILE *open_file(const char *filePath);

int build_start_packet(unsigned char *packet, long filesize, char *filename);
void alarmHandler();

int send_with_retry(int fd, unsigned char *frame, int size);
int build_frame(unsigned char *frame, unsigned char *data, int data_size,
                int j);

long distuffing(unsigned char *buf, int size, unsigned char *destuffed);

void send_RR(int fd, int r);

void send_REJ(int fd, int r);
int send_CONN(unsigned char *frame);
void send_DISC(int fd);
void handle_data_packet(unsigned char *buf, FILE *fp);
FILE *handle_start_packet(unsigned char *buf, int size);
void handle_end_packet(FILE *fp);
int build_end_packet(unsigned char *packet, long filesize, char *filename);