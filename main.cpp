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
uint8_t vblank_buffer[VGA_FRONT_LINE_COUNT + VGA_SYNC_LINE_COUNT + VGA_BACK_LINE_COUNT];
int line_nr = 0;
uint vblank_timing = 0;

uint dma_channel;
void dma_interrupt()
{
    dma_channel_acknowledge_irq0(dma_channel);

    line_nr++;
    if (line_nr == VGA_VIDEO_LINE_COUNT)
    {
        static uint last_vblank_time;
        uint vblank_time = time_us_32();
        vblank_timing = vblank_time - last_vblank_time;
        last_vblank_time = vblank_time;
        //End of visual data, transfer the blanking info.
        line_nr = 0;
        dma_channel_set_trans_count(dma_channel, sizeof(vblank_buffer), false);
        dma_channel_set_read_addr(dma_channel, vblank_buffer, true);
        return;
    }
    active_buffer ^= 1;

    //Prepare a new dma transfer
    dma_channel_set_trans_count(dma_channel, VGA_BUFFER_SIZE, false);
    dma_channel_set_read_addr(dma_channel, line_buffers[active_buffer], true);
}

int main() {
    stdio_init_all();
    while(!stdio_usb_connected()) tight_loop_contents();
    sleep_ms(500);
    
    uint vga_program_offset = pio_add_program(pio0, &vga_program);

    vga_program_init(pio0, 0, vga_program_offset, 0, 1, 2);

    line_buffers[0][0] = line_buffers[1][0] = 0; // pixel data line
    line_buffers[0][1] = line_buffers[1][1] = 0x3F; // first 40 pixels
    line_buffers[0][VGA_PIXELS+2] = line_buffers[1][VGA_PIXELS+2] = 0x3F; // last 40 pixels
    for(int n=2; n<VGA_PIXELS+2; n++)
        line_buffers[0][n] = line_buffers[1][n] = n;
    for(int n=0; n<VGA_FRONT_LINE_COUNT; n++)
        vblank_buffer[n] = 1; // blank line
    for(int n=VGA_FRONT_LINE_COUNT; n<VGA_FRONT_LINE_COUNT+VGA_SYNC_LINE_COUNT; n++)
        vblank_buffer[n] = 2; // vblank line
    for(int n=VGA_FRONT_LINE_COUNT+VGA_SYNC_LINE_COUNT; n<VGA_FRONT_LINE_COUNT+VGA_SYNC_LINE_COUNT+VGA_BACK_LINE_COUNT; n++)
        vblank_buffer[n] = 1; // blank line

    dma_channel = dma_claim_unused_channel(true);
    auto dma = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&dma, DMA_SIZE_8);
    channel_config_set_read_increment(&dma, true);
    channel_config_set_write_increment(&dma, false);
    channel_config_set_dreq(&dma, DREQ_PIO0_TX0);
    dma_channel_set_irq0_enabled(dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_interrupt);
    dma_channel_configure(dma_channel, &dma, &pio0->txf[0], line_buffers[0], VGA_BUFFER_SIZE, true);
    irq_set_priority(DMA_IRQ_0, 0);
    irq_set_enabled(DMA_IRQ_0, true);

    pio_set_sm_mask_enabled(pio0, (1 << 0), true);

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
    while(true)
    {
        sleep_ms(500);
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        sleep_ms(500);
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        printf("%d %d\n", line_nr, vblank_timing);
        printf("%p %p\n", pio0->flevel, pio0->fdebug);
        pio0->fdebug = pio0->fdebug;
        printf("%p %p\n", pio0->sm[0].instr, pio0->sm[0].addr - vga_program_offset);
    }
    return 0;
}
