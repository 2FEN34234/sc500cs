/* Link the MCU owner's actual C transport into the ESP native integration test. */
#include "../../../FDwork/shared/wifi_transport.h"
static WifiTransport transport;
extern int integration_dispatch(unsigned,const uint8_t*,unsigned,uint8_t*,uint8_t*);
void integration_reset(void) { memset(&transport,0,sizeof transport); }
unsigned integration_exchange(const uint8_t* packet,unsigned n,uint32_t now,uint8_t* output) {
    for(unsigned i=0;i<n;i++) wifi_transport_byte(&transport,packet[i],now,integration_dispatch);
    unsigned count=transport.tx_len;
    if(count) memcpy(output,transport.error_tx?transport.error:transport.reply,count);
    transport.tx_len=0;transport.tx_pos=0;
    return count;
}
