// Read from serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>


// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 4096

volatile int STOP = FALSE;

enum state {start, FLAG_RCV, A_RCV, C_RCV, BCC_OK, stop};

char xor(unsigned char array[], int cont){
    char test = 0x00;
    for(int i = 4; i <= cont; i++){
        test = test ^ array[i];    
    }
    return test;
}

int main(int argc, char *argv[])
{
    //STATES

    /* -Pseudo
    if (cur == FLAG_RCV) {st = FLAG_RCV;}
    if (cur == A_RCV) {st = A_RCV;}
    if ((st == A_RCV) && (C_RCV == 1)) {st = C_RCV;}
    if ((A && C) == BCC) {st = BCC_OK;}
    if (cur == FLAG_RCV) {st = stop;}
    if (cur == Other_RCV) {st = start;}
    */

    enum state st = start;

    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];

    if (argc < 2)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort>\n"
               "Example: %s /dev/ttyS1\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Open serial port device for reading and writing and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
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
    newtio.c_cc[VMIN] = 1;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    // Loop for input
    unsigned char buf[BUF_SIZE + 1] = {0}; // +1: Save space for the final '\0' char
    unsigned char cur;
    int cont = 0;
    while (st != stop)
    {
        // Returns after 5 chars have been input
        int bytes = read(fd, &cur, 1);
        buf[BUF_SIZE] = '\0'; // Set end of string to '\0', so we can printf

        switch (st) {

            case start:
                if (cur == 0x7E /*FLAG_RCV*/) {
                    st = FLAG_RCV;
                    buf[cont] = cur;
                }
                else {cont--;}
                break;

            case FLAG_RCV:
                if (cur == /*A_RCV*/ 0x03) {
                    st = A_RCV;
                    buf[cont] = cur;
                }
                else if (cur == 0x7E /*FLAG_RCV*/) {cont = 0;}
                else {cont = -1; st = start;}
                break;
            
            case A_RCV:
                if (cur == /*C_RCV*/ 0x03) {
                    st = C_RCV;
                    buf[cont] = cur;
                }
                else if (cur == 0x7E /*FLAG_RCV*/) {cont = 0; st = FLAG_RCV;}
                else {cont = -1; st = start;}
                break;
            
            case C_RCV:
                if (cur == (buf[1] ^ buf[2])) {
                    st = BCC_OK;
                    buf[cont] = cur;
                }
                else if (cur == 0x7E /*FLAG_RCV*/) {cont = 0; st = FLAG_RCV;}
                else {cont = -1; st = start;}
                break;
            
            case BCC_OK:
                if (cur == 0x7E) {
                    buf[cont] = cur;
                    if(xor(buf, cont - 2) == buf[cont - 1]){
                        st = stop;
                    }
                    else {cont = 0; st = FLAG_RCV;}

                }
                else {buf[cont] = cur;}
                break;

            case stop:
            break;
            
        }
        
        printf("%d\n", cont);

        cont++;

        //printf(":%s:%d\n", buf, bytes);
        //if (buf[0] == 0x7E)
    }

        /* -Pseudo
        if (cur == FLAG_RCV) {st = FLAG_RCV;}
        if (cur == A_RCV) {st = A_RCV;}
        if ((st == A_RCV) && (C_RCV == 1)) {st = C_RCV;}
        if ((A && C) == BCC) {st = BCC_OK;}
        if (cur == FLAG_RCV) {st = stop;}
        if (cur == Other_RCV) {st = start;}
        */

        

        
        
        for (int i = 0; i < cont; i++){
            printf("buf = 0x%02X\n", buf[i]);
        }
        buf[2] = 0x01;
        int bytes_sent = write(fd, buf, BUF_SIZE);
        printf("%d bytes written\n", cont);
        sleep(1);
    // The while() cycle should be changed in order to respect the specifications
    // of the protocol indicated in the Lab guide

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}
