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
#include "link_layer.h"


// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 1024

FILE *f_file;
enum state {start, FLAG_RCV, A_RCV, C_RCV, information, BCC_OK, stop};

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
    unsigned char buf[BUF_SIZE];
    unsigned char cur;
    unsigned char destuffed[BUF_file_SIZE];
    int i = 0;
    int j = 0;
    unsigned char a_rcv; // +1: Save space for the final '\0' char
    unsigned char c_rcv;
    unsigned char t_bcc2 = 0x00;
    int b = 0;
    while(b==0)
    {
        int res = read(fd, &cur, 1);

        if (res <= 0) continue;
        switch(st){
            case start:
                if (cur == 0x7E) st = FLAG_RCV;
                break;

            case FLAG_RCV:
                if (cur == 0x03){
                    a_rcv = cur;
                    t_bcc2 = 0x00;
                    st = A_RCV;
                } else if (cur == A_RX){
                    a_rcv = A_RX;
                    st = A_RCV;
                }
                else if (cur == 0x7E) st = FLAG_RCV;
                break;
            
            case A_RCV:
                if (cur == 0x7E) st = FLAG_RCV;
                else{
                    c_rcv = cur;
                    st = C_RCV;
                }
                break;
            
            case C_RCV:
                if (cur == (a_rcv ^ c_rcv)) st = BCC_OK;
                else if (cur == 0x7E) st = FLAG_RCV;
                break;
            
            case BCC_OK:
                if (cur == 0x7E) {
                    if (c_rcv == 0x03) {
                        printf("0x7E \n 0x03 \n 0x00 \n 0x03 \n 0x7E\n");

                        send_RR(fd, 0);
                        st = FLAG_RCV;
                    } else 
                    {
                        printf("uhhhhhhhhhhhhhhhhhhhhhhh\n");
                        send_REJ(fd, j);
                        st = FLAG_RCV;
                        }
                }else if (c_rcv == 0x00 || c_rcv == 0x40){
                            buf[i++] = cur;
                            t_bcc2 ^= cur;
                            printf("ahhhhhhhhhhhhhhhhhhhhhhh\n");
                            st = information;

                }
                
                break;

case information:

    if (cur == 0x7E) {

        if (i < 1) {
            st = FLAG_RCV;
            break;
        }
        if (buf[0] == 0x03) {
                    printf("END packet\n");
                    st = stop;
                    break;
        }

        // 🔴 1. DESTUFF PRIMEIRO (IMPORTANTE)
        long size = distuffing(buf, i, destuffed);
        print_hex(buf, i);


        // 🔴 2. SEPARAR BCC2 (já destuffed)
        unsigned char received_bcc2 = destuffed[size - 1];
        unsigned char link_app[503];
        // 🔴 3. CALCULAR BCC2
        unsigned char calc_bcc2 = 0x00;
        long size_app = 0;
        for (int k = 0; k < size - 1; k++) {
            calc_bcc2 ^= destuffed[k];
        }

        printf("BCC2 Tx: %d\nBCC2 Rx: %d\n", received_bcc2, calc_bcc2);

        // DEBUG (opcional)
        // printf("BCC calc=%02X recv=%02X\n", calc_bcc2, received_bcc2);
        if (calc_bcc2 == received_bcc2) {

            print_hex(destuffed, size);

            // 🔥 sequência correta
            if ((c_rcv == 0x00 && j == 0) || (c_rcv == 0x40 && j == 1)) {

                // 🔥 APPLICATION LAYER
                if (destuffed[0] == 0x02) {
                    printf("START packet\n");
                    f_file = handle_start_packet(destuffed, size);
                }
                else if (destuffed[0] == 0x01) {
                    printf("DATA packet\n");
                    if (f_file)
                        handle_data_packet(destuffed, f_file);
                }

                // 🔥 RR
                if (j == 0) {
                    printf("OK! RR1\n");
                    send_RR(fd, 1);
                    j = 1;
                } else {
                    printf("OK! RR0\n");
                    send_RR(fd, 0);
                    j = 0;
                }

            } else {
                // 🔁 duplicada
                if (j == 0) {
                    printf("RR1 DUP\n"); send_RR(fd, 1);}
                else {
                    printf("RR0 DUP\n"); send_RR(fd, 0);}
            }

        } else {
            // ❌ erro BCC2
            if (c_rcv == 0x00){
                    printf("ERRO! 0\n"); send_REJ(fd, 0);
                                    printf("END packet\n");
                    st = stop;
                    break;}
            else{
                    printf("ERRO! 1\n"); send_REJ(fd, 1);
                                    printf("END packet\n");
                    st = stop;
                    break;}
        }

        // reset
        i = 0;
        st = FLAG_RCV;
    }

    else {
        if (i < BUF_SIZE) {
            buf[i++] = cur;
        }
    }

    break;

    break;

            case stop:
                send_DISC(fd);
                handle_end_packet(f_file);
                b = 1;
                break;
        }
    }





        /* -Pseudo
        if (cur == FLAG_RCV) {st = FLAG_RCV;}
        if (cur == A_RCV) {st = A_RCV;}
        if ((st == A_RCV) && (C_RCV == 1)) {st = C_RCV;}
        if ((A && C) == BCC) {st = BCC_OK;}
        if (cur == FLAG_RCV) {st = stop;}
        if (cur == Other_RCV) {st = start;}
        */

        

        
        
        
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
