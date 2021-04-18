#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
//test
#include "packet.h"
#include "common.h"

#define STDIN_FD 0
#define RETRY 120 //milli second
#define PACKET_BUFFER_SIZE 100000

int next_seqno = 0;
int send_base = 0;
int window_size = 10;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer;
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;


int start_position=0;

tcp_packet *file_packet_buffer[PACKET_BUFFER_SIZE];

void resend_multiple_packets(int startIndex, int windowSize)
{
    for (int i = 0; i < windowSize; i += 1)
    {
        if ((startIndex + i) > PACKET_BUFFER_SIZE)
        {
            break;
        }
        tcp_packet *packetToSend = file_packet_buffer[startIndex + i];
        if (sendto(sockfd, packetToSend, TCP_HDR_SIZE + get_data_size(packetToSend), 0,
                   (const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
    }
}

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between
        //sendBase and nextSeqNum
        VLOG(INFO, "Timout happend");
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                   (const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
    }
}

void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}

void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}

/*
 * init_timer: Initialize timeer
 * delay: delay in milli seconds
 * sig_handler: signal handler function for resending unacknoledge packets
 */
void init_timer(int delay, void (*sig_handler)(int))
{
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = delay / 1000; // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;
    timer.it_value.tv_sec = delay / 1000; // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}

int main(int argc, char **argv)
{
    int portno, len;
    int next_seqno;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;


    int last_ack;

    /* check command line arguments */
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL)
    {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* initialize server server details */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0)
    {
        fprintf(stderr, "ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    //Stop and wait protocol

    init_timer(RETRY, resend_packets);
    next_seqno = 0;
    int pos=0;
    while (1)
    {
        len = fread(buffer, 1, DATA_SIZE, fp);
        if (len <= 0)
        {

            last_ack=next_seqno;
            VLOG(INFO, "End Of File has been reached");
            sndpkt = make_packet(0);
            file_packet_buffer[pos]=sndpkt;
            break;
        }
        send_base = next_seqno;
        next_seqno = send_base + len;
        sndpkt = make_packet(len);
        memcpy(sndpkt->data, buffer, len);
        sndpkt->hdr.seqno = send_base;
        file_packet_buffer[pos]=sndpkt;
        pos+=1;
        
    }

    VLOG(INFO,"length of the file %d",sizeof(file_packet_buffer));
        //Wait for ACK

    // File smaller than window size
    if(window_size>pos){
        VLOG(DEBUG,"Lenght of file smaller than window size");
        window_size=pos;
    }

    for (int i=0;i<window_size;i++){

         VLOG(DEBUG, "Sending packet %d to %s",
                 file_packet_buffer[i]->hdr.seqno, inet_ntoa(serveraddr.sin_addr));
            /*
             * If the sendto is called for the first time, the system will
             * will assign a random port number so that server can send its
             * response to the src port.
             */
            if (sendto(sockfd, file_packet_buffer[i], TCP_HDR_SIZE + get_data_size(file_packet_buffer[i]), 0,
                       (const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
    }
    start_timer();
    int packet_end=0;
    int completed=0;
    do
        {
            //ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
            //struct sockaddr *src_addr, socklen_t *addrlen);

            if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                         (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen) < 0)
            {
                error("recvfrom");
            }

            recvpkt = (tcp_packet *)buffer;
            printf("%d \n", recvpkt->hdr.ackno);
            assert(get_data_size(recvpkt) <= DATA_SIZE);

            //Last data packet acked. Send termination packet
            if ((packet_end==1) && (recvpkt->hdr.ackno==last_ack)){
                        if (sendto(sockfd, file_packet_buffer[start_position+window_size], TCP_HDR_SIZE + get_data_size(file_packet_buffer[start_position+window_size]), 0,
                        (const struct sockaddr *)&serveraddr, serverlen) < 0)
                        {
                            error("sendto");
                        }
                        completed=1;
            }

            else if(recvpkt->hdr.ackno==file_packet_buffer[start_position+1]->hdr.seqno){
                stop_timer();
                
                if (file_packet_buffer[start_position+window_size]->hdr.data_size==0){
                    packet_end=1;
                    VLOG(DEBUG,"Packet end reached. Last packet to be acked %d",last_ack);
                    
                }
                else{
                    VLOG(DEBUG, "Sending packet %d to %s",
                 file_packet_buffer[start_position+window_size]->hdr.seqno, inet_ntoa(serveraddr.sin_addr));
                    if (sendto(sockfd, file_packet_buffer[start_position+window_size], TCP_HDR_SIZE + get_data_size(file_packet_buffer[start_position+window_size]), 0,
                        (const struct sockaddr *)&serveraddr, serverlen) < 0)
                        {
                            error("sendto");
                        }
                    start_timer();
                    start_position+=1;
                }
            }


        } while (completed==0);

    return 0;
}