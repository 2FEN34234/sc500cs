/* Exercise the real SD command driver + unmodified FatFs + recorder against
 * a byte-accurate SPI SDHC model and an in-memory FAT32 disk, never a real disk.
 * Covers CMD0/8/55/41/58, CRC16, CMD17/24/25, saved BMP payload, restart naming,
 * cooperative service during busy waits, stop-at-frame-boundary and faults.
 */
#include <assert.h>
#include <stdlib.h>
#define RECORDER_HOST_TEST
#include "../FDwork/shared/sd_recorder_impl.h"
static uint8_t *disk;
static uint64_t timer;
static unsigned calls,cs_level=1,last_rx=255,cmdpos,queue_rd,queue_wr;
static unsigned spi_write_left,spi_lba,spi_multi,write_expected,spi_busy_cycles,spi_idle=1;
static unsigned cap_status,cap_word,fake_capture,drop_card,reject_write,commands[64];
static unsigned model_divider,corrupt_read,crc_enabled;
static uint32_t model_hw=0x00010003u;
static unsigned read_delay_ms,read_stuck,read_fault_word,read_pending,read_cancelled;
static uint64_t read_due;
static uint8_t cmdbytes[6],queue[600],writebytes[514];
static uint32_t screen;
uint64_t AlSys_GetTimerFreq(void){return 1000000;}
uint64_t AlSys_GetTimerTickCount(void){timer+=10;return timer;}
void AlSys_MDelay(unsigned ms){timer+=ms*1000;}
static unsigned next_during_prepare;
static void yield(void){
    calls++;
    if(photo_work==2) {
        uint8_t payload[64];unsigned length=0;
        assert(recorder_photo_next(payload,&length)==WF_BUSY);
        next_during_prepare++;
    }
}
static unsigned next_ready(uint8_t *payload,unsigned *length) {
    unsigned result,limit=1000;
    do {
        unsigned before=commands[17];
        result=recorder_photo_next(payload,length);
        assert(commands[17]==before); /* UART request itself does zero SD reads. */
        if(result==WF_BUSY) {timer+=5000;recorder_poll();}
    } while(result==WF_BUSY && --limit);
    assert(limit);return result;
}
static void enqueue(uint8_t b){assert(queue_wr<sizeof queue);queue[queue_wr++]=b;}
static uint8_t spi(uint8_t txv){
    unsigned i,c;uint32_t arg;uint16_t crc;
    if(cs_level || drop_card) return 255;
    if(queue_rd<queue_wr) return queue[queue_rd++];
    queue_rd=queue_wr=0;
    if(spi_busy_cycles) {spi_busy_cycles--;return 0;}
    if(spi_write_left) {
        writebytes[514-spi_write_left]=txv;
        if(--spi_write_left==0) {
            crc=0;for(i=0;i<512;i++) crc=crc16(crc,writebytes[i]);
            assert(writebytes[512]==(crc>>8) && writebytes[513]==(crc&255));
            if(reject_write) {enqueue(0x0d);return 255;}
            assert(spi_lba<131072);memcpy(disk+spi_lba*512,writebytes,512);spi_lba++;
            enqueue(5);spi_busy_cycles=4;
        }
        return 255;
    }
    if(!cmdpos && (spi_multi || write_expected)) {
        if(txv==0xfc || txv==0xfe) {spi_write_left=514;write_expected=0;return 255;}
        if(txv==0xfd && spi_multi) {spi_multi=0;spi_busy_cycles=4;return 255;}
    }
    if(!cmdpos && (txv&0xc0)!=0x40) return 255;
    cmdbytes[cmdpos++]=txv;
    if(cmdpos!=6) return 255;
    cmdpos=0;c=cmdbytes[0]&63;commands[c]++;
    // Independent, unshifted CRC7 reference (poly x^7+x^3+1).
    unsigned ref=0,bi,bj;
    for(bi=0;bi<5;bi++) for(bj=0;bj<8;bj++) {
        unsigned feedback=((ref>>6)^((cmdbytes[bi]>>(7-bj))&1))&1;
        ref=(ref<<1)&127;if(feedback) ref^=9;
    }
    assert(cmdbytes[5]==((ref<<1)|1));
    arg=((uint32_t)cmdbytes[1]<<24)|((uint32_t)cmdbytes[2]<<16)|((uint32_t)cmdbytes[3]<<8)|cmdbytes[4];
    switch(c){
    case 0:assert(cmdbytes[5]==0x95);spi_idle=1;crc_enabled=0;enqueue(1);break;
    case 8:assert(arg==0x1aa && cmdbytes[5]==0x87);enqueue(1);enqueue(0);enqueue(0);enqueue(1);enqueue(0xaa);break;
    case 55:enqueue(spi_idle?1:0);break;
    case 59:assert(arg==1 && spi_idle);crc_enabled=1;enqueue(1);break;
    case 41:assert(arg==0x40000000u && crc_enabled);spi_idle=0;enqueue(0);break;
    case 58:enqueue(0);enqueue(0xc0);enqueue(0xff);enqueue(0x80);enqueue(0);break;
    case 17:
        assert(arg<131072);enqueue(0);enqueue(255);enqueue(0xfe);crc=0;
        for(i=0;i<512;i++){uint8_t b=disk[arg*512+i];enqueue(b);crc=crc16(crc,b);}
        if(corrupt_read==2 || (corrupt_read==1 && model_divider<124)) crc^=1;
        enqueue(crc>>8);enqueue(crc);break;
    case 24:case 25:spi_lba=arg;spi_multi=c==25;write_expected=1;enqueue(0);break;
    default:fprintf(stderr,"unexpected command %u\n",c);abort();
    }
    return 255;
}
uint32_t rr(unsigned a){
    if(a==0) return 0x52454331;
    if(a==0x1c) return model_hw;
    if(a==4) return cap_status;
    if(a==0x14) return 57600u | ((read_pending && read_stuck)?(1u<<19):0);
    if(a==0x24) {
        if(read_pending) {
            timer+=10;
            if(!(read_stuck && cap_word==read_fault_word) && timer>=read_due) read_pending=0;
        }
        return read_pending;
    }
    if(a==0x4c) return last_rx<<8;
    if(a>=0x30 && a<=0x3c){
        unsigned idx=cap_word*4+(a-0x30)/4,x=idx%640,y=idx/640;
        return ((x&255)<<16)|((y&255)<<8)|((x+y)&255);
    }
    return 0;
}
void rw(unsigned a,uint32_t v){
    if(a==0x40) model_divider=v;
    if(a==4){fake_capture++;cap_status=2;}
    if(a==8) {cap_status=0;read_pending=0;read_cancelled++;}
    if(a==0x20) {assert(!read_pending);cap_word=v;read_pending=1;read_due=timer+read_delay_ms*1000u;}
    if(a==0x44){
        cs_level=v&1;
        if(cs_level){cmdpos=queue_rd=queue_wr=0;spi_multi=0;spi_write_left=0;}
    }
    if(a==0x48) last_rx=spi(v);
    if(a==0x60) screen=v;
}
static void le16(uint8_t *p,unsigned v){p[0]=v;p[1]=v>>8;}
static void le32(uint8_t *p,uint32_t v){le16(p,v);le16(p+2,v>>16);}
static uint32_t ld32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void format_test_disk(void){
    disk=calloc(131072,512);assert(disk);
    uint8_t *b=disk;b[0]=0xeb;b[1]=0x58;b[2]=0x90;memcpy(b+3,"TESTFAT ",8);
    le16(b+11,512);b[13]=1;le16(b+14,32);b[16]=2;b[21]=0xf8;
    le32(b+32,131072);le32(b+36,1024);le32(b+44,2);le16(b+48,1);le16(b+50,6);
    b[66]=0x29;memcpy(b+82,"FAT32   ",8);b[510]=0x55;b[511]=0xaa;
    memcpy(disk+6*512,b,512);
    b=disk+512;le32(b,0x41615252);le32(b+484,0x61417272);le32(b+488,0xffffffff);le32(b+492,0xffffffff);le32(b+508,0xaa550000);
    unsigned f;for(f=0;f<2;f++) {b=disk+(32+1024*f)*512;le32(b,0x0ffffff8);le32(b+4,0xffffffff);le32(b+8,0x0fffffff);}
}
static void finish_photo(void){
    unsigned limit=2000,before=g_rec_saved;
    do {recorder_poll();timer+=1000;} while(--limit && g_rec_saved==before && !g_rec_error);
    if(g_rec_error) printf("debug CMD17=%u CMD24=%u CMD25=%u lba=%u idle=%u left=%u\n",commands[17],commands[24],commands[25],spi_lba,spi_idle,spi_write_left);
    assert(limit && !g_rec_error && g_rec_saved==before+1);
}
static void validate_bmp(unsigned slot){
    /* Every new image now has a persistent WFM sidecar after its BMP entry. */
    uint8_t *entry=disk+2080*512+slot*64;
    assert(ld32(entry+28)==691254 && entry[0]=='T');
    unsigned cluster=((unsigned)entry[20]<<16)|((unsigned)entry[21]<<24)|entry[26]|((unsigned)entry[27]<<8);
    unsigned remaining=691254,offset=0;
    while(remaining){
        assert(cluster>=2 && cluster<129000);
        uint8_t *s=disk+(2080+cluster-2)*512;
        unsigned n=remaining>512?512:remaining,i;
        for(i=0;i<n;i++,offset++){
            if(offset==0) assert(s[i]=='B');
            if(offset==1) assert(s[i]=='M');
            if(offset>=54){unsigned px=(offset-54)/3,c=(offset-54)%3,x=px%640,y=px/640;
                assert(s[i]==(uint8_t)(c==0?x+y:c==1?y:x));}
        }
        remaining-=n;cluster=ld32(disk+32*512+cluster*4)&0x0fffffff;
    }
    assert(cluster>=0x0ffffff8);
}
int main(void){
    // Standard CRC-16/XMODEM check vector, independent of the card model.
    const char *vector="123456789";uint16_t check_crc=0;unsigned vi;
    for(vi=0;vi<9;vi++) check_crc=crc16(check_crc,(uint8_t)vector[vi]);
    assert(check_crc==0x31c3);
    format_test_disk();
    model_hw=0;assert(!recorder_init(yield));
    recorder_action(REC_PHOTO,0);recorder_poll();assert(fake_capture==0 && commands[0]==0);
    model_hw=0x00010002u;assert(!recorder_init(yield));
    model_hw=0x00010003u;assert(recorder_init(yield));
    // CRC error at fast clock: same sector must be retried at slow clock.
    uint8_t check[1024];
    assert(disk_initialize(0)==0);corrupt_read=1;
    unsigned reads=commands[17];
    assert(disk_read(0,check,0,2)==RES_OK);
    assert(commands[17]==reads+3 && model_divider==124);
    assert(memcmp(check,disk,1024)==0);
    // Persistent CRC errors must remain errors, not accepted corrupt data.
    assert(disk_initialize(0)==0);corrupt_read=2;reads=commands[17];
    assert(disk_read(0,check,0,1)==RES_ERROR && !card_ready);
    assert(commands[17]==reads+2);
    corrupt_read=0;
    recorder_action(REC_PHOTO,0);finish_photo();validate_bmp(0);
    // Upload uses the real FatFs file, stable metadata, exact payload CRC and
    // durable commit; it must never expose an open/partial capture.
    uint8_t meta[64],id[16],chunk[512];unsigned meta_len=0;
    assert(next_ready(meta,&meta_len)==WF_OK && meta_len==46);
    memcpy(id,meta+1,16);assert(wf_get32(meta+17)==691254);
    assert(next_during_prepare); // slow preparation still services incoming UART

    uint32_t upload_crc=0xffffffffu;
    for(unsigned offset=0;offset<691254;) {
        unsigned amount=691254-offset;if(amount>512) amount=512;
        assert(recorder_photo_read(id,offset,amount,chunk)==WF_OK);
        upload_crc=wf_crc_update(upload_crc,chunk,amount);offset+=amount;
    }
    assert(~upload_crc==wf_get32(meta+21));
    assert(recorder_photo_read(id,691254,1,chunk)==WF_BAD_ARGUMENT);
    assert(recorder_photo_read(id,0,513,chunk)==WF_BAD_ARGUMENT);
    assert(recorder_photo_commit(id,691254,~upload_crc^1)==WF_CONFLICT);
    assert(recorder_photo_commit(id,691254,~upload_crc)==WF_OK);
    assert(recorder_photo_commit(id,691254,~upload_crc)==WF_OK);
    assert(next_ready(meta,&meta_len)==WF_NO_PHOTO);
    photo_selected=photo_dir_open=photo_reader_open=0; // MCU restart
    assert(next_ready(meta,&meta_len)==WF_NO_PHOTO);
    validate_bmp(0);
    uint8_t multi_data[1024]={0};
    assert(disk_write(0,multi_data,100000,2)==RES_OK);
    assert(commands[25]>0 && calls>0);
    // Simulate restart naming: preexisting image must remain unchanged.
    seq=1;recorder_action(REC_PHOTO,0);finish_photo();validate_bmp(0);validate_bmp(1);
    assert(next_ready(meta,&meta_len)==WF_OK);
    assert(memcmp(meta+1,id,16));memcpy(id,meta+1,16);
    photo_selected=photo_dir_open=photo_reader_open=0;
    assert(next_ready(meta,&meta_len)==WF_OK && !memcmp(meta+1,id,16));
    // An interrupted commit tail must not mark a valid closed BMP uploaded.
    FIL sidecar;UINT written;uint8_t broken_tail[3]={0x57,0x4d,0x43};
    assert(f_open(&sidecar,"T0000002.WFM",FA_WRITE)==FR_OK);
    assert(f_lseek(&sidecar,40)==FR_OK);
    assert(f_write(&sidecar,broken_tail,3,&written)==FR_OK && written==3);
    assert(f_close(&sidecar)==FR_OK);
    photo_selected=photo_dir_open=photo_reader_open=0;
    assert(next_ready(meta,&meta_len)==WF_OK && !memcmp(meta+1,id,16));
    assert(recorder_photo_commit(id,691254,wf_get32(meta+21))==WF_OK);
    photo_selected=photo_dir_open=photo_reader_open=0;
    assert(next_ready(meta,&meta_len)==WF_NO_PHOTO);
    validate_bmp(0);validate_bmp(1);
    recorder_action(REC_START,0);recorder_poll();recorder_poll();
    recorder_action(REC_STOP,0);finish_photo();assert(!running);validate_bmp(2);
    unsigned saved=g_rec_saved;timer+=10000000;recorder_poll();assert(g_rec_saved==saved);
    // Long directories progress in background without waiting five seconds
    // for each new ESP request, and NEXT never rereads a full BMP for its CRC.
    assert(next_ready(meta,&meta_len)==WF_OK);
    assert(recorder_photo_commit(meta+1,wf_get32(meta+17),wf_get32(meta+21))==WF_OK);
    for(unsigned i=0;i<20;i++) {
        char junk[13];snprintf(junk,sizeof junk,"J%07u.TMP",i);
        FIL f;assert(f_open(&f,junk,FA_WRITE|FA_CREATE_NEW)==FR_OK);
        assert(f_close(&f)==FR_OK);
    }
    if(photo_dir_open) f_closedir(&photo_dir);
    photo_dir_open=photo_work=0;
    unsigned scan_reads=commands[17],turns=0;
    assert(recorder_photo_next(meta,&meta_len)==WF_BUSY && commands[17]==scan_reads);
    while(photo_work==1 && turns<20) {recorder_poll();turns++;}
    assert(turns>=3 && turns<20 && photo_work==3);
    assert(recorder_photo_next(meta,&meta_len)==WF_NO_PHOTO);
    assert(commands[17]-scan_reads<40); // metadata only, not 1351 BMP sectors
    FILE *f=fopen("build/sd_test_fat32.img","wb");assert(f);assert(fwrite(disk,512,131072,f)==131072);fclose(f);
    // A bounded slow DDR reply is accepted, including across timer rollover.
    read_delay_ms=70;unsigned old_calls=calls;
    assert(snapshot_read(0) && calls>old_calls);
    timer=((uint64_t)0xffffffffu-20)*1000u;
    assert(snapshot_read(0));read_delay_ms=0;
    // First-word and mid-image stalls must cancel without reporting SAVED.
    for(unsigned fault=0;fault<2;fault++) {
        read_stuck=1;read_fault_word=fault?130:0;
        unsigned cancelled=read_cancelled,limit=1000;old_calls=calls;
        recorder_action(REC_PHOTO,0);
        while(!g_rec_error && --limit) recorder_poll();
        assert(limit && g_rec_error==35 && g_rec_saved==saved && !running && !opened);
        assert(read_cancelled==cancelled+1 && !read_pending && calls>old_calls);
        read_stuck=0;
    }
    recorder_action(REC_PHOTO,0);finish_photo(); // retry after cancellation
    reject_write=1;recorder_action(REC_PHOTO,0);unsigned limit=1000;
    while(!g_rec_error && --limit) recorder_poll();
    assert(limit && g_rec_error && !running);
    reject_write=0;drop_card=1;recorder_action(REC_PHOTO,0);recorder_poll();assert(g_rec_error && !running);
    drop_card=0;recorder_action(REC_PHOTO,0);recorder_poll();
    assert(mounted && opened);
    fs.free_clst=0; // simulate a full volume: short write must be an error
    recorder_poll();assert(g_rec_error==0x30 && !running);
    printf("PASS SDHC command sequence, CRC16, FAT32, full BMP pixels, no overwrite, stop, service, I/O failure, missing card (%u yields)\n",calls);
    free(disk);return 0;
}
