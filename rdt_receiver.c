#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>

#include "common.h"
#include "packet.h"

#define BUFFER_PACKET_ARRAY_SIZE 5

/*
 * You ar required to change the implementation to support
 * window size greater than one.
 * In the currenlt implemenetation window size is one, hence we have
 * onlyt one send and receive packet
 */
tcp_packet *recvpkt;
tcp_packet *sndpkt;
int expected_seq_no = 0; //next expected sequence no. For task 1 we will discard packets if this is not matched

tcp_packet *bufferedPackets[BUFFER_PACKET_ARRAY_SIZE];

void initPacketBuffer()
{
    tcp_packet *tmp_packet = make_packet(0);
    //memcpy(sndpkt->data, buffer, len);
    for (int i = 0; i < BUFFER_PACKET_ARRAY_SIZE; i++)
    {
        tcp_packet *tmp_packet = make_packet(0);
        bufferedPackets[i] = tmp_packet;
        bufferedPackets[i]->hdr.seqno = -1;
    }
}

int checkIfHigherSeqNumberInPacketBuffer(int seqNumber)
{

    int foundIndex = -1;
    for (int i = 0; i < BUFFER_PACKET_ARRAY_SIZE; i++)
    {
        if (bufferedPackets[i]->hdr.seqno > seqNumber)
        {
            foundIndex = i;
            break;
        }
    }
    return foundIndex;
}

int tryToAddToPacketBuffer(tcp_packet *tmpPacket)
{
    tcp_packet *newPacket;
    char b[DATA_SIZE];
    int insertIndex = -1;
    for (int i = 0; i < BUFFER_PACKET_ARRAY_SIZE; i++)
    {
        if (bufferedPackets[i]->hdr.seqno == -1)
        {
            newPacket = make_packet(get_data_size(tmpPacket));
            newPacket->hdr.seqno = tmpPacket->hdr.seqno;
            newPacket->hdr.data_size = tmpPacket->hdr.data_size;
            newPacket->hdr.ctr_flags = tmpPacket->hdr.ctr_flags;
            newPacket->hdr.ackno = tmpPacket->hdr.ackno;
            memcpy(&tmpPacket->data, &newPacket->data, get_data_size(tmpPacket));
            bufferedPackets[i] = newPacket;
            insertIndex = i;
            return insertIndex;
        }
    }
    return insertIndex;
}

void cleanPacketBuffer()
{

    VLOG(DEBUG, "start clean buf");
    for (int i = 0; i < BUFFER_PACKET_ARRAY_SIZE; i++)
    {
        if (bufferedPackets[i]->hdr.seqno < expected_seq_no)
        {
            bufferedPackets[i]->hdr.seqno = -1;
        }
        VLOG(DEBUG, "buffered seq - %d", bufferedPackets[i]->hdr.seqno);
    }
    VLOG(DEBUG, "end clean buf");
}

int main(int argc, char **argv)
{
    int sockfd;                    /* socket */
    int portno;                    /* port to listen on */
    int clientlen;                 /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int optval;                    /* flag value for setsockopt */
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;
    initPacketBuffer();
    /* 
     * check command line arguments 
     */
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    fp = fopen(argv[2], "w");
    if (fp == NULL)
    {
        error(argv[2]);
    }

    /* 
     * socket: create the parent socket 
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets 
     * us rerun the server immediately after we kill it; 
     * otherwise we have to wait about 20 secs. 
     * Eliminates "ERROR on binding: Address already in use" error. 
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval, sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /* 
     * bind: associate the parent socket with a port 
     */
    if (bind(sockfd, (struct sockaddr *)&serveraddr,
             sizeof(serveraddr)) < 0)
        error("ERROR on binding");

    /* 
     * main loop: wait for a datagram, then echo it
     */
    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);
    while (1)
    {
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        //VLOG(DEBUG, "waiting from server \n");
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                     (struct sockaddr *)&clientaddr, (socklen_t *)&clientlen) < 0)
        {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *)buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);

        if (recvpkt->hdr.data_size == 0)
        {
            //VLOG(INFO, "End Of File has been reached");
            fclose(fp);
            break;
        }
        /* 
         * sendto: ACK back to the client 
         */
        gettimeofday(&tp, NULL);
        VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);

        if (expected_seq_no == recvpkt->hdr.seqno)
        {
            fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
            fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
            expected_seq_no = recvpkt->hdr.seqno + recvpkt->hdr.data_size;
        }
        else
        {
            VLOG(DEBUG, "NON EXPECTED PACKET SEQ NUMBER RECIEVED");
            //if not expected seq number then add to buffer
            tryToAddToPacketBuffer(recvpkt);
            cleanPacketBuffer();
        }

        int checkIndex = -1;
        while (checkIndex = checkIfHigherSeqNumberInPacketBuffer(expected_seq_no) != -1)
        {
            fseek(fp, bufferedPackets[checkIndex]->hdr.seqno, SEEK_SET);
            fwrite(bufferedPackets[checkIndex]->data, 1, bufferedPackets[checkIndex]->hdr.data_size, fp);
            expected_seq_no += bufferedPackets[checkIndex]->hdr.seqno + bufferedPackets[checkIndex]->hdr.data_size;
        }

        sndpkt = make_packet(0);
        sndpkt->hdr.ackno = expected_seq_no;
        sndpkt->hdr.ctr_flags = ACK;
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                   (struct sockaddr *)&clientaddr, clientlen) < 0)
        {
            error("ERROR in sendto");
        }
        cleanPacketBuffer();
    }

    return 0;
}
