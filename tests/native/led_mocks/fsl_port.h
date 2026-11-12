#define PORT0 0
#define PORT1 1
enum {kPORT_PullDisable,kPORT_SlowSlewRate,kPORT_LowDriveStrength,kPORT_MuxAsGpio,kPORT_InputBufferDisable};
typedef struct { unsigned pullSelect,slewRate,driveStrength,mux,inputBuffer; } port_pin_config_t;
static unsigned configured_pads;
static void PORT_SetPinConfig(unsigned bank,unsigned pin,const port_pin_config_t *config) {
    assert((bank==0 && (pin==10 || pin==27)) || (bank==1 && pin==2));
    assert(config->mux==kPORT_MuxAsGpio); configured_pads++;
}
