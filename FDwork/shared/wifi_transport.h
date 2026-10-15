/* Portable stop-and-wait transport. Caller supplies time, UART and operations.
 * The largest defined v1 request is 24 payload bytes. Oversized requests are
 * dropped through the delimiter; responses retain the full 544-byte budget.
 */
#ifndef WIFI_TRANSPORT_H
#define WIFI_TRANSPORT_H
#include "wifi_wire.h"
typedef struct {
    uint8_t rx[64],reply[WF_WIRE],last[42],error[24];
    uint32_t session,sequence,last_byte;
    unsigned used,reply_len,tx_len;
    volatile unsigned tx_pos; /* USART0 IRQ drains immutable reply bytes */
    uint8_t last_len,drop,pending,error_tx;
} WifiTransport;
/* dispatch returns payload length, or -1 for an asynchronous operation.
 * It may change response type (default request|0x80). */
typedef int (*WifiDispatch)(unsigned,const uint8_t *,unsigned,uint8_t *,uint8_t *);
static void wifi_reply(WifiTransport *t,unsigned type,unsigned n) {
    uint8_t *p=t->reply;
    memcpy(p,t->last,14);p[3]=type;wf_put16(p+12,n);
    wf_put32(p+14+n,wf_crc(p,14+n));
    t->reply_len=wf_encode_in_place(p,18+n);
    t->tx_pos=0;t->tx_len=t->reply_len;t->error_tx=0;t->pending=0;
}
static void wifi_transport_error(WifiTransport *t,const uint8_t *request,unsigned code) {
    uint8_t raw[20];memcpy(raw,request,14);raw[3]=255;wf_put16(raw+12,2);
    raw[14]=request[3];raw[15]=code;wf_put32(raw+16,wf_crc(raw,16));
    t->tx_len=wf_encode(t->error,raw,20);t->tx_pos=0;t->error_tx=1;
}
static void wifi_transport_reset_rx(WifiTransport *t) {t->used=0;t->drop=1;}
static void wifi_transport_byte(WifiTransport *t,uint8_t b,uint32_t ms,WifiDispatch dispatch) {
    if(t->used && (uint32_t)(ms-t->last_byte)>250) wifi_transport_reset_rx(t);
    t->last_byte=ms;
    if(b) {
        if(!t->drop) {
            if(t->used<sizeof t->rx) t->rx[t->used++]=b;
            else wifi_transport_reset_rx(t);
        }
        return;
    }
    unsigned n=t->drop?0:wf_decode(t->rx,t->used);t->used=0;t->drop=0;
    if(!n) return;
    uint8_t *p=t->rx;unsigned len=wf_get16(p+12),type=p[3];
    uint32_t session=wf_get32(p+4),seq=wf_get32(p+8);
    if(!session || !seq) {wifi_transport_error(t,p,WF_BAD_ARGUMENT);return;}
    if(session==t->session && seq==t->sequence && t->last_len) {
        if(n-4==t->last_len && !memcmp(p,t->last,n-4)) {
            t->tx_pos=0;t->tx_len=t->reply_len;t->error_tx=0;
        } else wifi_transport_error(t,p,WF_CONFLICT);
        return;
    }
    if(session!=t->session && (type!=1 || len)) {
        wifi_transport_error(t,p,WF_UNKNOWN_ID);return;
    }
    if(session==t->session && seq<=t->sequence) {
        wifi_transport_error(t,p,WF_CONFLICT);return;
    }
    if(len>24) {wifi_transport_error(t,p,WF_BAD_ARGUMENT);return;}
    t->session=session;t->sequence=seq;t->last_len=n-4;memcpy(t->last,p,n-4);
    uint8_t response=type|128;
    /* Keep the request for retry comparison; dispatch fills reply payload,
     * which wifi_reply subsequently encodes in place (rx is input only). */
    int count=dispatch(type,t->last+14,len,t->reply+14,&response);
    if(count<0) {t->pending=1;t->reply_len=0;return;}
    if((unsigned)count>WF_PAYLOAD) {response=255;t->reply[14]=type;t->reply[15]=WF_IO_ERROR;count=2;}
    wifi_reply(t,response,count);
}
#endif
