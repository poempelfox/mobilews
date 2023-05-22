
#include <driver/rmt_tx.h>
#include "rgbled.h"

/* The RGB LED is connected to GPIO 18 on our Olimex ESP32-S2-devkit-lipo */
#define LEDGPIO 18

/*
 * Low speed mode times:
 * T0H  0 code, high voltage time     0.5 us
 * T0L  0 code, low voltage time      2.0 us
 * T1H  1 code, high voltage time     1.2 us
 * T1L  1 code, low voltage time      1.3 us
 * RES  Reset low voltage time      >50.0 us
 * Data is sent in multiples of 24 bit,
 * 8 bits per color, MSB first, color sequence green-red-blue
 */

static rmt_channel_handle_t ledtx_chan = NULL;
rmt_symbol_word_t whattosend[1 + 3 * 8];
static rmt_encoder_handle_t ledtx_copy_encoder;
static rmt_copy_encoder_config_t cec = {
};
static rmt_transmit_config_t tx_config = {
  .loop_count = 0,
};

void rgbled_init(void)
{
  rmt_tx_channel_config_t tx_chan_config = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .gpio_num = LEDGPIO,
    .mem_block_symbols = 64, // FIXME memory block size, 64 * 4 = 256Bytes
    .resolution_hz = 10 * 1000 * 1000, // 10 MHz tick resolution, i.e. 1 tick = 0.1us
    .trans_queue_depth = 4,           // set the number of transactions that can pend in the background
    .flags.invert_out = false,
    .flags.with_dma = false, /* No need for DMA */
  };
  ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &ledtx_chan));
  ESP_ERROR_CHECK(rmt_new_copy_encoder(&cec, &ledtx_copy_encoder));
  ESP_ERROR_CHECK(rmt_enable(ledtx_chan));
}

void rgbled_setled(uint8_t r, uint8_t g, uint8_t b)
{
  rmt_symbol_word_t sym0bit = {
     .level0 = 1,
     .duration0 =  4,
     .level1 = 0,
     .duration1 =  9
  };
  rmt_symbol_word_t sym1bit = {
     .level0 = 1,
     .duration0 =  9,
     .level1 = 0,
     .duration1 =  4
  };
  rmt_symbol_word_t symreset = {
     .level0 = 0,
     .duration0 = 500,
     .level1 = 0,
     .duration1 = 500
  };
  for (int i = 0; i < 8; i++) {
    if (r & 0x80) {
      whattosend[ 8 + i] = sym1bit;
    } else {
      whattosend[ 8 + i] = sym0bit;
    }
    r <<= 1;
    if (g & 0x80) {
      whattosend[ 0 + i] = sym1bit;
    } else {
      whattosend[ 0 + i] = sym0bit;
    }
    g <<= 1;
    if (b & 0x80) {
      whattosend[16 + i] = sym1bit;
    } else {
      whattosend[16 + i] = sym0bit;
    }
    b <<= 1;
  }
  whattosend[24] = symreset;
  rmt_transmit(ledtx_chan, ledtx_copy_encoder, whattosend, sizeof(whattosend), &tx_config);
  rmt_tx_wait_all_done(ledtx_chan, 100);
}

