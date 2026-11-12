enum {kCLOCK_Port0,kCLOCK_Port1,kCLOCK_Gpio0,kCLOCK_Gpio1};
static unsigned enabled_clocks;
static void CLOCK_EnableClock(unsigned clock) { enabled_clocks |= 1U<<clock; }
