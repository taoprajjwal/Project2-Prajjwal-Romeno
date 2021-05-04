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
#include "packet.h"
#include "common.h"

#define STDIN_FD 0
#define PACKET_BUFFER_SIZE 100000

int RETRY =1200; //milli second
float estRTT=1200;
float sampleRTT=0;
float devRTT=0;


int next_seqno = 0;
int send_base = 0;
int window_size = 1;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer;
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;
char *filePathVal;
int start_position = -1;
int last_packet_sent;

int duplicate_ack=0;
int duplicate_ack_counter=0;
int ssthresh=64;
int window_size_counter=0;
int packet_end = 0;
int completed = 0;


int retrasmit_flag=0; //flag for whether a packet was re-transmitted. Used for updating timeout using RTT.
int timeout_ack=0; //triple ack led to the last timeout. Will ignore future triple acks.


struct timeval tp;
FILE *out_file;

tcp_packet *get_packet_at_position(int index)
{
    FILE *fp;
    fp = fopen(filePathVal, "r");
    char buffer[DATA_SIZE];
    index += 1; // because index 0 would be actually 1 in the multiplication below and so on
    int len = 0;
    if (fseek(fp, (index * DATA_SIZE - DATA_SIZE), SEEK_SET) >= 0)
    {
        len = fread(buffer, 1, DATA_SIZE, fp);
    }
    tcp_packet *resultPacket = make_packet(len);
    memcpy(resultPacket->data, buffer, len);
    resultPacket->hdr.seqno = (index * DATA_SIZE - DATA_SIZE);
    fclose(fp);
    return resultPacket;
}

void resend_multiple_packets(int sig)
{

    if (sig == SIGALRM)
    {
            if ( (int) (window_size/2) > 2){
                ssthresh= (int) window_size/2;
            }

            else{
                ssthresh=2;
            }


        VLOG(DEBUG,"Before timeout window_size : %d, After timeout ssthresh: %d",window_size,ssthresh);

        window_size_counter=0;
        window_size=1;


        //clear the recv buffer in to prevent duplicate timeout triggers

        /*
        char *clear_buffer[MSS_SIZE];
        int size;
        while(size=recvfrom(sockfd, clear_buffer, MSS_SIZE, MSG_DONTWAIT,
                    (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen) < 0){

        } */

    

        for (int i=start_position+1;i<=start_position+window_size;i++)
        {
            tcp_packet *packetToSend = get_packet_at_position(i);

            if (packetToSend->hdr.data_size == 0)
            {

                VLOG(DEBUG, "Trying to send the last packet after timeout");
                if (packet_end==0){
                    break;
                }
            }

            VLOG(DEBUG, "Sending packet %d: resend",
                 packetToSend->hdr.seqno);

            if (sendto(sockfd, packetToSend, TCP_HDR_SIZE + get_data_size(packetToSend), 0,
                       (const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }

            last_packet_sent=i;
            retrasmit_flag=1;

        }
    
    }
}

void update_timer(long long int time){

    gettimeofday(&tp,NULL);
    sampleRTT= (float) ((tp.tv_sec*1000LL + (tp.tv_usec/1000)) - time);

    estRTT=((1.0 - 0.125) * estRTT) + (0.125 * sampleRTT);
    devRTT = ((1.0 - 0.25) * devRTT) + (0.25 * fabs(sampleRTT - estRTT)); // Absolute value represented by fabs()

    RETRY= (int) (estRTT+(4*devRTT));

    VLOG(DEBUG,"Current timeout %d",RETRY);

    init_timer(RETRY,resend_multiple_packets);

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
    signal(SIGALRM, resend_multiple_packets);
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

    filePathVal = argv[3];

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

    out_file=fopen("CWND.csv","w");

    init_timer(RETRY, resend_multiple_packets);
    next_seqno = 0;
    int pos = 0;
    while (1)
    {
        sndpkt = get_packet_at_position(pos);
        len = sndpkt->hdr.data_size;
        if (len <= 0)
        {
            last_ack = next_seqno;
            VLOG(INFO, "End Of   File has been reached");
            sndpkt = make_packet(0);
            break;
        }
        send_base = next_seqno;
        next_seqno = send_base + len;
        pos += 1;
    }
    //Wait for ACK

    // File smaller than window size
    if (window_size > pos)
    {
        VLOG(DEBUG, "Lenght of file smaller than window size");
        window_size = pos;
    }

    for (int i = 0; i < window_size; i++)
    {

        VLOG(DEBUG, "Sending packet %d to %s",
             get_packet_at_position(i)->hdr.seqno, inet_ntoa(serveraddr.sin_addr));
        /*
             * If the sendto is called for the first time, the system will
             * will assign a random port number so that server can send its
             * response to the src port.
             */
        tcp_packet *tmp_pkt = get_packet_at_position(i);
        if (sendto(sockfd, tmp_pkt, TCP_HDR_SIZE + get_data_size(tmp_pkt), 0,
                   (const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }

        last_packet_sent=i;
    }

    start_timer();

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
        printf("%d %d \n", recvpkt->hdr.ackno,duplicate_ack);
        assert(get_data_size(recvpkt) <= DATA_SIZE);

        


        if ((recvpkt->hdr.ackno==duplicate_ack) && (duplicate_ack!=last_ack)){
            duplicate_ack_counter++;

            if (duplicate_ack_counter>=3){

                if (duplicate_ack!=timeout_ack){
                    VLOG(DEBUG,"Triple ack for %d recieved",duplicate_ack)
                    duplicate_ack_counter=0;
                    timeout_ack=duplicate_ack;
                    resend_multiple_packets(SIGALRM);
                }
            }
        }

        else{
            
            //Congestion Avoidance
            if (window_size>ssthresh){
                //Increase CWND by 1 for every CWNDth ACK
                if (window_size_counter>window_size){
                    window_size++;
                    window_size_counter=0;
                }
                else{
                    window_size_counter++;
                }
            }

            //Slow start
            else{
                window_size++;
            }

            gettimeofday(&tp,NULL);
            fprintf(out_file,"%lu, %lu, %d \n",tp.tv_sec,tp.tv_usec,window_size);

            //Last data packet acked. Send termination packet
            if ((packet_end == 1) && (recvpkt->hdr.ackno == last_ack))
            {
                tcp_packet *tmp_pkt = get_packet_at_position(pos*DATA_SIZE);

                //Send multiple times to ensure receiver gets the last packet and closes connection.
                for (int j=0;j<10;j++){
                    if (sendto(sockfd, tmp_pkt, TCP_HDR_SIZE + get_data_size(tmp_pkt), 0,
                            (const struct sockaddr *)&serveraddr, serverlen) < 0)
                    {
                        error("sendto");
                    }

                }
                completed = 1;
            }

            
            else if (recvpkt->hdr.ackno >= duplicate_ack)
            {
                duplicate_ack_counter=0;
                duplicate_ack=recvpkt->hdr.ackno;

                start_position=ceil(recvpkt->hdr.ackno / DATA_SIZE)-1;

                VLOG(DEBUG," Start position :%d, Last packet sent : %d, Window size: %d, Sstresh: %d ",start_position,last_packet_sent,window_size,ssthresh);


                if (last_packet_sent<start_position){
                    last_packet_sent=start_position;
                }

                for (int i=last_packet_sent+1;i<=start_position+window_size;i++){

                    tcp_packet *tmp_pkt = get_packet_at_position(i);

                    if (tmp_pkt->hdr.data_size == 0)
                    {
                        packet_end = 1;
                        VLOG(DEBUG, "Packet end reached. Last packet to be acked %d", last_ack);
                    }
                    else
                    {
                        if (retrasmit_flag==0){
                        update_timer(recvpkt->hdr.time);
                        }
                        
                        stop_timer();
                        VLOG(DEBUG, "Sending packet %d to %s",
                            tmp_pkt->hdr.seqno, inet_ntoa(serveraddr.sin_addr));
                        if (sendto(sockfd, tmp_pkt, TCP_HDR_SIZE + get_data_size(tmp_pkt), 0,
                                (const struct sockaddr *)&serveraddr, serverlen) < 0)
                        {
                            error("sendto");
                        }
                        start_timer();
                        last_packet_sent =i;
                        retrasmit_flag=0;
                    }
                }
            }
        } 
    }while (completed == 0);

    close(out_file);
    return 0;
}