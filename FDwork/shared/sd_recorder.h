#ifndef SD_RECORDER_H
#define SD_RECORDER_H
#include <stdint.h>
enum { REC_PHOTO=1, REC_START=2, REC_STOP=3, REC_INTERVAL=4, REC_RETRY=5, REC_UI=6 };
int recorder_init(void (*service)(void));
void recorder_action(unsigned action, unsigned value);
void recorder_poll(void);
extern volatile uint32_t g_rec_state, g_rec_error, g_rec_saved;
unsigned recorder_wifi_flags(void);
unsigned recorder_interval(void);
int recorder_available(void);
void recorder_last_photo_id(uint8_t id[16]);
/* NEXT is I/O-free: BUSY schedules preparation in recorder_poll. */
unsigned recorder_photo_next(uint8_t *payload,unsigned *length);
/* READ/COMMIT must run outside the cooperative SD hook. */
unsigned recorder_photo_read(const uint8_t id[16],uint32_t offset,unsigned length,uint8_t *data);
unsigned recorder_photo_commit(const uint8_t id[16],uint32_t size,uint32_t crc);
#endif
