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

volatile int STOP = FALSE;

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
    while(1)
    {
        switch(st){
            case start:
                if (cur == 0x7E) st = FLAG_RCV;
                break;

            case FLAG_RCV:
                if (cur == 0x03){
                    a_rcv = cur;
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
                        send_RR(fd, 0);
                        st = FLAG_RCV;
                    } else{
                        send_REJ(fd, j);
                        st = FLAG_RCV;
                        }
                    }else if (c_rcv == 0x00){
                        if(j == 0){
                            st = information;
                        }
                        else{
                            send_REJ(fd, 0);
                            st = FLAG_RCV;
                        }
                    } else if (c_rcv == 0x40){
                        if(j == 1){
                            st = information;
                        }
                        else{
                            send_REJ(fd, 1);
                            st = FLAG_RCV;
                        }
                    
                    }

                
                break;

            case information:
            //end of information packet
                if (cur == 0x7E) {
                    if(c_rcv == 0x00){
                        //trama 0
                        //tirar o bcc2 do buf e comparar com o xor dos dados
                        if(buf[i - 2] == 0x7D){
                            if(buf[i - 1] == 0x5E){
                                if(t_bcc2 == 0x7E){
                                    //distuffing
                                int size = distuffing(buf, i - 3, destuffed);
                                print_hex(destuffed, size);
                                    send_RR(fd, 1);
                                    st = FLAG_RCV;


                                }else{
                                    send_REJ(fd, 0);
                                    st = FLAG_RCV;
                                }
                            }else if(buf[i - 1] == 0x5D){
                                if(t_bcc2 == 0x7D){
                                int size = distuffing(buf, i - 3, destuffed);
                                print_hex(destuffed, size);
                                    send_RR(fd, 1);
                                    st = FLAG_RCV;
                                }else{
                                    send_REJ(fd, 0);
                                    st = FLAG_RCV;
                                }
                            }
                        }else{
                            if(t_bcc2 == buf[i - 1]){
                                //distuffing
                                int size = distuffing(buf, i - 2, destuffed);
                                print_hex(destuffed, size);
                                send_RR(fd, 1);
                                st = FLAG_RCV;
                            }else{
                                send_REJ(fd, 0);
                                st = FLAG_RCV;
                            }

                        }

                    }else{
                        //trama 1
                        if(buf[i - 2] == 0x7D){
                            if(buf[i - 1] == 0x5E){
                                if(t_bcc2 == 0x7E){
                                    //
                                int size = distuffing(buf, i - 3, destuffed);
                                print_hex(destuffed, size);
                                    send_RR(fd, 0);
                                    st = FLAG_RCV;


                                }else{
                                    send_REJ(fd, 1);
                                    st = FLAG_RCV;
                                }
                            }else if(buf[i - 1] == 0x5D){
                                if(t_bcc2 == 0x7D){
                                int size = distuffing(buf, i - 3, destuffed);
                                print_hex(destuffed, size);
                                    send_RR(fd, 0);
                                    st = FLAG_RCV;
                                }else{
                                    send_REJ(fd, 1);
                                    st = FLAG_RCV;
                                }
                            }
                        }else{
                            if(t_bcc2 == buf[i - 1]){
                                //distuffing
                                int size = distuffing(buf, i - 2, destuffed);
                                print_hex(destuffed, size);
                                send_RR(fd, 0);
                                st = FLAG_RCV;
                            }else{
                                send_REJ(fd, 1);
                                st = FLAG_RCV;
                            }

                        }
                    }
                
                    i = 0;
                    st = FLAG_RCV;

                }else{
                    //save data
                    buf[i] = cur;
                    t_bcc2 ^= cur;
                    i++;
                    }
                

                break;
                

            case stop:
                send_DISC(fd);
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
