#include <stdlib.h>
#include"packet.h"
#include <time.h>

struct timeval tp;


static tcp_packet zero_packet = {.hdr={0}};
/*
 * create tcp packet with header and space for data of size len
 */
tcp_packet* make_packet(int len)
{
    tcp_packet *pkt;
    pkt = malloc(TCP_HDR_SIZE + len);

    *pkt = zero_packet;
    gettimeofday(&tp,NULL);
    pkt->hdr.data_size = len;
    pkt->hdr.time=(tp.tv_sec*1000LL + (tp.tv_usec/1000));
    return pkt;
}

int get_data_size(tcp_packet *pkt)
{
    return pkt->hdr.data_size;
}

