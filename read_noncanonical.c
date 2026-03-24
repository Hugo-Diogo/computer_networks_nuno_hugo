// Read from serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]



/*
Things to change:

For the application layer we need to send a START packet with name and size of the file. THIS IS A CONTROL PACKET.    
The we need to divide the file into smaller parts and include a header in each one;
Then we send that DATA packet.

After sending the last data packet we need to announce to the recveiver that the transfer is finalised
->Send another CONTROL packet indicating that the transfer has ended 

*/
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include "libcom.h"


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
    unsigned char cur1;
    int cont1 = 0;
    unsigned char frame1 = 0x00;
    unsigned char frame2 = 0x40;
    int data_flag = 0;
    
    while (st != stop)
    {
        // Returns after 5 chars have been input
        int bytes = read(fd, &cur1, 1);
        buf[BUF_SIZE] = '\0'; // Set end of string to '\0', so we can printf

        switch (st) {

            case start:
                if (cur1 == 0x7E /*FLAG_RCV*/) {
                    st = FLAG_RCV;
                    buf[cont1] = cur1;
                }
                else {cont1--;}
                break;

            case FLAG_RCV:
                if (cur1 == /*A_RCV*/ 0x03) {
                    st = A_RCV;
                    buf[cont1] = cur1;
                }
                else if (cur1 == 0x7E /*FLAG_RCV*/) {cont1 = 0;}
                else {cont1 = -1; st = start;}
                break;
            
            case A_RCV:
                if (cur1 == /*C_RCV*/ 0x03) {
                    st = C_RCV;
                    buf[cont1] = cur1;
                }
                else if (cur1 == 0x7E /*FLAG_RCV*/) {cont1 = 0; st = FLAG_RCV;}
                else {cont1 = -1; st = start;}
                break;
            
            case C_RCV:
                if (cur1 == (buf[1] ^ buf[2])) {
                    st = BCC_OK;
                    buf[cont1] = cur1;
                }
                else if (cur1 == 0x7E /*FLAG_RCV*/) {cont1 = 0; st = FLAG_RCV;}
                else {cont1 = -1; st = start; printf("BCC1 ERROR!");}
                break;
            
            case BCC_OK:
                if (cur1 == 0x7E) {
                    buf[cont1] = cur1;
                    if(xor(buf, cont1 - 2) == buf[cont1 - 1]){
                        if (buf[2] == 0x00) {write(fd, buf_RR1, 5);}
                        if (buf[2] == 0x40) {write(fd, buf_RR0, 5);}
                        st = stop;
                    }
                    else {
                        if ((frame1 != buf[2])) {
                            if (buf[2] == 0x00) {write(fd, buf_REJ0, 5);}
                            if (buf[2] == 0x40) {write(fd, buf_REJ1, 5);}
                        }
                        else {
                            if (buf[2] == 0x00) {write(fd, buf_RR1, 5);}
                            if (buf[2] == 0x40) {write(fd, buf_RR0, 5);}
                            st = stop;
                            break;
                        }
                        cont1 = 0; 
                        st = FLAG_RCV;
                    }

                }

                if((cur1 == 0x5E) && (data_flag = 1))
                {
                    data_flag = 0;
                    buf[cont1 - 1] = 0x7E;
                    cont1--;
                }

                if((cur1 == 0x5D) && (data_flag = 1))
                {
                    data_flag = 0;
                    cont1--;
                }

                if ((cur1 == 0x7D) && (data_flag = 0)) 
                {
                    data_flag = 1;
                    buf[cont1] = cur1;
                }

                else {buf[cont1] = cur1;}
                break;

            case stop:
                if (frame1 == 0x00) {frame1 = 0x40;}
                if (frame1 == 0x40) {frame1 = 0x00;}
                write(fd, buf_DISC, 5);
                break;
            
        }
        
        printf("%d\n", cont1);

        cont1++;

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


        
        for (int i = 0; i < cont1; i++){
            printf("buf = 0x%02X\n", buf[i]);
        }
        buf[2] = 0x01;
        int bytes_sent = write(fd, buf, BUF_SIZE);
        printf("%d bytes written\n", cont1);
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

    
    // Application decoder

    st = start;

    unsigned char appbuf[BUF_SIZE + 1] = {0};
    unsigned char cur2;
    int cont2 = 0;



    return 0;
}
