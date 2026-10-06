#include "platform.h"
#include "transport.h"
int main(void){board_init();for(;;){transport_poll();board_service();}}
