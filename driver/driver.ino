/*
  Open GBC LCD Driver
  v0.1
  2025
  LeggoMyFroggo
*/
 
#include <TB_TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "lcd_pio.h"

TFT_eSPI tft = TFT_eSPI(); // Display library

// GPIO pin designators
const int pinSPS = 27;
const int pinSPL = 26;
const int pinDCK = 22;
const int pinTE = 23;
const int pinRed = 0;

// LCD signal properties
const uint16_t ROW_SIZE = 160;
const uint16_t COL_SIZE = 144;

// Screen buffer properties
const uint16_t BUFFER_WIDTH  = 240;
const uint16_t BUFFER_HEIGHT = 144;
const uint16_t BUFFER_SIZE   = BUFFER_WIDTH * BUFFER_HEIGHT;

// PIO assignments for LCD signal reading
PIO pio = pio0;
uint8_t v_sm = 0;
uint8_t h_sm = 0;

// Image is double buffered in format 565 RGB
uint8_t dmaChannels[2];
uint16_t dmaBuffers[2][BUFFER_SIZE];
bool frameReady = false;
uint8_t readyFrameIndex = 1;

void __isr dmaHandler() {
  uint32_t status = dma_hw->ints0;

  if (status & (1u << dmaChannels[0])) {
    // Acknowledge the IRQ
    dma_hw->ints0 = 1u << dmaChannels[0];

    // Tell core 1 to read from buffer 0
    readyFrameIndex = 0;
    
    // Kickoff the next DMA
    dma_channel_set_write_addr(dmaChannels[0], dmaBuffers[0], false);
    dma_channel_set_trans_count(dmaChannels[0], BUFFER_SIZE, false);

    frameReady = true;
  }

  if (status & (1u << dmaChannels[1])) {
    // Acknowledge the IRQ
    dma_hw->ints0 = 1u << dmaChannels[1];

    // Tell core 1 to read from buffer 1
    readyFrameIndex = 1;

    // Kickoff the next DMA
    dma_channel_set_write_addr(dmaChannels[1], dmaBuffers[1], false);
    dma_channel_set_trans_count(dmaChannels[1], BUFFER_SIZE, false);

    frameReady = true;
  }
}

void initializeDMA() {
  dmaChannels[0] = dma_claim_unused_channel(true);
  dmaChannels[1] = dma_claim_unused_channel(true);

  // Configure channel 0
  dma_channel_config c0 = dma_channel_get_default_config(dmaChannels[0]);
  channel_config_set_read_increment(&c0, false);
  channel_config_set_write_increment(&c0, true);
  channel_config_set_transfer_data_size(&c0, DMA_SIZE_16);
  channel_config_set_dreq(&c0, pio_get_dreq(pio, h_sm, false));
  channel_config_set_chain_to(&c0, dmaChannels[1]);
  dma_channel_configure(
    dmaChannels[0],
    &c0,
    dmaBuffers[0],
    &pio->rxf[h_sm],
    BUFFER_SIZE,
    false
  );

  // Configure channel 1
  dma_channel_config c1 = dma_channel_get_default_config(dmaChannels[1]);
  channel_config_set_read_increment(&c1, false);
  channel_config_set_write_increment(&c1, true);
  channel_config_set_transfer_data_size(&c1, DMA_SIZE_16);
  channel_config_set_dreq(&c1, pio_get_dreq(pio, h_sm, false));
  channel_config_set_chain_to(&c1, dmaChannels[0]);
  dma_channel_configure(
    dmaChannels[1],
    &c1,
    dmaBuffers[1],
    &pio->rxf[h_sm],
    BUFFER_SIZE,
    false
  );

  // Enable DMA IRQ
  dma_channel_set_irq0_enabled(dmaChannels[0], true);
  dma_channel_set_irq0_enabled(dmaChannels[1], true);
  irq_set_exclusive_handler(DMA_IRQ_0, dmaHandler);
  irq_set_enabled(DMA_IRQ_0, true);

  // Kickoff the first DMA channel
  dma_channel_start(dmaChannels[0]);
}

void setup() {
  // Setup TE pin
  pinMode(pinTE, OUTPUT);
  
  // PIO setup
  v_sm = pio_claim_unused_sm(pio, true);
  uint8_t v_offset = pio_add_program(pio, &vertical_loop_program);
  vertical_loop_program_init(pio, v_sm, v_offset, pinRed);
  pio_sm_put(pio, v_sm, COL_SIZE - 1);

  h_sm = pio_claim_unused_sm(pio, true);
  uint8_t h_offset = pio_add_program(pio, &horizontal_loop_program);
  horizontal_loop_program_init(pio, h_sm, h_offset, pinRed);
  pio_sm_put(pio, h_sm, ROW_SIZE - 1);

  // DMA setup
  initializeDMA();

  // TFT setup
  tft.init();
  tft.initDMA();
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);

  tft.writecommand(0x35);
  tft.writedata(0);

  // Start writing image data to the LCD
  tft.startWrite();

  // Kick off the PIO
  pio_sm_set_enabled(pio, v_sm, true);
  pio_sm_set_enabled(pio, h_sm, true);
}

// ==============================
// Main Loop
// ==============================
void loop() {
}

void loop1() {
  // Only act if a frame is ready AND TFT DMA is idle
  if (frameReady && !tft.dmaBusy()) {
    frameReady = false;

    // Push front buffer to display
    tft.setAddrWindow(0, 0, BUFFER_WIDTH, 216);
    bool yFlipper = readyFrameIndex == 0 ? true : false;
    uint16_t bi = 0;
    for (uint8_t y = 0; y < COL_SIZE; y++) {
      if (yFlipper) {
        tft.pushPixelsDMA(&dmaBuffers[readyFrameIndex][bi], BUFFER_WIDTH);
        tft.pushPixelsDMA(&dmaBuffers[readyFrameIndex][bi], BUFFER_WIDTH);
        bi += BUFFER_WIDTH;
      } else {
        tft.pushPixelsDMA(&dmaBuffers[readyFrameIndex][bi], BUFFER_WIDTH);
        bi += BUFFER_WIDTH;
      }
      yFlipper = !yFlipper;
    }
  }
}
