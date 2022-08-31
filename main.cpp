#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "vga.pio.h"


uint active_buffer = 0;
uint8_t line_buffers[2][VGA_BUFFER_SIZE];
int line_nr = 0;

uint dma_channel;
void dma_interrupt()
{
    dma_channel_acknowledge_irq0(dma_channel);

    line_nr++;
    if (line_nr == VGA_TOTAL_LINE_COUNT)
        line_nr = 0;
    active_buffer ^= 1;

    //Prepare a new dma transfer
    if (line_nr < VGA_VIDEO_LINE_COUNT)
    {
        line_buffers[active_buffer][0] = 0xFF;
        line_buffers[active_buffer][VGA_BUFFER_SIZE - 1] = 0;
        dma_channel_set_trans_count(dma_channel, VGA_BUFFER_SIZE, false);
    } else {
        //No active line, just skip out, not sending anything except for indication that we have nothing to send.
        line_buffers[active_buffer][0] = 0;
        dma_channel_set_trans_count(dma_channel, 1, false);
    }
    dma_channel_set_read_addr(dma_channel, line_buffers[active_buffer], true);

    if (line_nr < VGA_VIDEO_LINE_COUNT + VGA_FRONT_LINE_COUNT)
        pio0->txf[0] = 1; //not VBLANK
    else if (line_nr < VGA_VIDEO_LINE_COUNT + VGA_FRONT_LINE_COUNT + VGA_SYNC_LINE_COUNT)
        pio0->txf[0] = 0; //VBLANK
    else
        pio0->txf[0] = 1; //not VBLANK
}

int main() {
    stdio_init_all();
    while(!stdio_usb_connected()) tight_loop_contents();
    sleep_ms(500);
    
    uint vga_vsync_offset = pio_add_program(pio0, &vga_sync_program);
    uint vga_pixel_offset = pio_add_program(pio0, &vga_pixel_program);

    vga_sync_program_init(pio0, 0, vga_vsync_offset, 0, 1);
    vga_pixel_program_init(pio0, 1, vga_pixel_offset, 2);

    dma_channel = dma_claim_unused_channel(true);
    auto dma = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&dma, DMA_SIZE_8);
    channel_config_set_read_increment(&dma, true);
    channel_config_set_write_increment(&dma, false);
    channel_config_set_dreq(&dma, DREQ_PIO0_TX1);
    dma_channel_set_irq0_enabled(dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_interrupt);
    dma_channel_configure(dma_channel, &dma, &pio0->txf[1], line_buffers[0], VGA_BUFFER_SIZE, true);
    pio0->txf[0] = 1; //not VBLANK
    irq_set_priority(DMA_IRQ_0, 0);
    irq_set_enabled(DMA_IRQ_0, true);

    pio_set_sm_mask_enabled(pio0, (1 << 0) | (1 << 1), true);

    for(int n=1; n<=VGA_PIXELS; n++)
        line_buffers[0][n] = line_buffers[1][n] = n;

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
