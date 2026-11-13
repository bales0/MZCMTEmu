#include <assert.h>
#include <stdio.h>
#include "drivers/buttons.h"
#include "driver/gpio.h"
#include "freertos/task.h"
static TickType_t now;
static int levels[48];
TickType_t xTaskGetTickCount(void){return now;
}
esp_err_t gpio_config(const gpio_config_t *cfg){assert(cfg->pin_bit_mask==((1ULL<<8)|(1ULL<<9)|(1ULL<<10)|(1ULL<<11)|(1ULL<<42)));
return ESP_OK;
}
int gpio_get_level(int pin){return levels[pin];
}
static btn_event_t poll(unsigned ms){now+=ms;
return buttons_poll();
}
static void reset(void){for(int i=0;i<48;++i)levels[i]=1;
now=0;
buttons_init();
}
int main(void){
 reset();
 levels[8]=0;
 assert(poll(0)==BTN_NONE);
 levels[8]=1;
 assert(poll(10)==BTN_NONE);
 levels[8]=0;
 assert(poll(10)==BTN_NONE);
 assert(poll(29)==BTN_NONE);
 assert(poll(1)==BTN_PLAY);
 assert(poll(1000)==BTN_NONE);
 levels[8]=1;
 assert(poll(0)==BTN_NONE);
 assert(poll(30)==BTN_NONE);
 levels[8]=0;
 assert(poll(0)==BTN_NONE);
 assert(poll(30)==BTN_PLAY);
 reset();
 levels[10]=0;
 assert(poll(0)==BTN_NONE);
 assert(poll(30)==BTN_NEXT);
 assert(!buttons_last_event_was_repeat());
 assert(poll(399)==BTN_NONE);
 assert(poll(1)==BTN_NEXT && buttons_last_event_was_repeat());
 assert(poll(119)==BTN_NONE);
 assert(poll(1)==BTN_NEXT);
 buttons_delay_repeat(BTN_NEXT,700);
 assert(poll(699)==BTN_NONE);
 assert(poll(1)==BTN_NEXT);
 /* STOP wins simultaneous PLAY/REC and blocks held navigation after screen transition. */
 levels[9]=levels[8]=levels[42]=0;
 assert(poll(0)==BTN_NONE);
 assert(poll(30)==BTN_NONE);
 assert(poll(799)==BTN_NONE);
 assert(poll(1)==BTN_STOP_LONG);
 assert(poll(1000)==BTN_NONE);
 levels[9]=1;
 assert(poll(0)==BTN_NONE);
 assert(poll(30)==BTN_NONE);
 assert(poll(1000)==BTN_NONE);
 levels[10]=1;
 assert(poll(0)==BTN_NONE);
 assert(poll(30)==BTN_NONE);
 levels[10]=0;
 assert(poll(0)==BTN_NONE);
 assert(poll(30)==BTN_NEXT);
 reset();
 levels[9]=0;
 assert(poll(0)==BTN_NONE);
 assert(poll(30)==BTN_NONE);
 levels[9]=1;
 assert(poll(100)==BTN_NONE);
 assert(poll(30)==BTN_STOP);
 assert(poll(1000)==BTN_NONE);
 reset();
 levels[9]=levels[10]=0;
 buttons_init();
 assert(poll(2000)==BTN_NONE);
 levels[9]=levels[10]=1;
 assert(poll(0)==BTN_NONE);
 assert(poll(30)==BTN_NONE);
 /* Tick wrap is unsigned elapsed time, also for repeats. */
 reset();
 now=(TickType_t)-50;
 levels[11]=0;
 assert(poll(0)==BTN_NONE);
 assert(poll(30)==BTN_PREV);
 assert(poll(400)==BTN_PREV && buttons_last_event_was_repeat());
 puts("PASS real buttons: 30ms debounce, startup-held suppression, STOP chord priority, one short/long event, repeat delay/rate and tick wrap");
 return 0;
}
