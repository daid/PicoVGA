#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "vga.pio.h"


uint8_t line_buffer[VGA_HPIXELS];
int hblank_count = 0;

void vblank_interrupt() {
    static uint32_t prev_vblank = 0;
    if (hblank_count != 480)
        printf("%d %d\n", time_us_32() - prev_vblank, hblank_count);
    hblank_count = 0;
    prev_vblank = time_us_32();
    pio_interrupt_clear(pio0, VGA_IRQ_VBLANK);
}

uint vga_hpixels_offset;
uint dma_channel;
//The hblank interrupt runs from the end of the DMA transfer,
//  we use this to setup the dma transfer for the next line, and restart the pixel pio, so it will wait for hsync to signal it is time.
void hblank_interrupt() {
    dma_channel_acknowledge_irq1(dma_channel);
    //Prepare a new dma transfer
    dma_channel_set_trans_count(dma_channel, sizeof(line_buffer), false);

    hblank_count++;
    //Wait for the hline pio to finish sending all pixels.
    while(pio_sm_get_tx_fifo_level(pio0, 2) > 0) tight_loop_contents();
    //Reset the hline pio, so it will wait for for the irq from the vsync handler.
    pio_sm_exec(pio0, 2, pio_encode_jmp(vga_hpixels_offset));

    //Actually initiate the transfer    
    dma_channel_set_read_addr(dma_channel, line_buffer, true);
}

int main() {
    stdio_init_all();
    //`while(!stdio_usb_connected()) tight_loop_contents();
    sleep_ms(500);
    
    PIO pio = pio0;
    uint vga_vsync_offset = pio_add_program(pio, &vga_vsync_program);
    uint vga_hsync_offset = pio_add_program(pio, &vga_hsync_program);
    vga_hpixels_offset = pio_add_program(pio, &vga_hpixels_program);

    irq_set_exclusive_handler(PIO0_IRQ_1, vblank_interrupt);
    irq_set_enabled(PIO0_IRQ_1, true);
    pio_set_irq1_source_enabled(pio, pis_interrupt0, true); //TODO: This needs to match VGA_IRQ_VBLANK

    vga_vsync_program_init(pio, 0, vga_vsync_offset, 2);
    vga_hsync_program_init(pio, 1, vga_hsync_offset, 3);
    vga_hpixels_program_init(pio, 2, vga_hpixels_offset, 4);

    dma_channel = dma_claim_unused_channel(true);
    auto dma = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&dma, DMA_SIZE_8);
    channel_config_set_read_increment(&dma, true);
    channel_config_set_write_increment(&dma, false);
    channel_config_set_dreq(&dma, DREQ_PIO0_TX2);
    dma_channel_set_irq1_enabled(dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_1, hblank_interrupt);
    dma_channel_configure(dma_channel, &dma, &pio->txf[2], line_buffer, count_of(line_buffer), true);
    irq_set_priority(DMA_IRQ_1, 0);
    irq_set_enabled(DMA_IRQ_1, true);

    pio_sm_set_enabled(pio, 0, true);
    pio_sm_set_enabled(pio, 1, true);
    pio_sm_set_enabled(pio, 2, true);

    for(int n=0; n<count_of(line_buffer); n++)
        line_buffer[n] = (n & 1) ? 0 : 0xFF;

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
    while(true)
    {
        sleep_ms(500);
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        sleep_ms(500);
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
    }
    return 0;
}
