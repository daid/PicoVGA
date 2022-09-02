#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "vga.pio.h"


volatile uint active_buffer = 0;
uint8_t line_buffers[2][VGA_BUFFER_SIZE];
uint8_t vblank_buffer[VGA_FRONT_LINE_COUNT + VGA_SYNC_LINE_COUNT + VGA_BACK_LINE_COUNT];
volatile int line_nr = 0;
uint vblank_timing = 0;
int repeat_line;

uint dma_channel;
void dma_interrupt()
{
    dma_channel_acknowledge_irq0(dma_channel);

    if (repeat_line) {
        repeat_line -= 1;
    } else {
        if (line_nr == VGA_VIDEO_LINE_COUNT/2 - 1)
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
        line_nr++;
        repeat_line = 1;
    }
    //Prepare a new dma transfer
    dma_channel_set_trans_count(dma_channel, VGA_BUFFER_SIZE, false);
    dma_channel_set_read_addr(dma_channel, line_buffers[active_buffer], true);
}

uint8_t rng()
{
	static uint8_t s = 0xAA, a = 0;
	s^=s<<3;
        s^=s>>5;
        s^=a++>>2;
        return s;
}

int main() {
    stdio_init_all();
    //while(!stdio_usb_connected()) tight_loop_contents();
    sleep_ms(500);

    uint vga_program_offset = pio_add_program(pio0, &vga_program);

    vga_program_init(pio0, 0, vga_program_offset, 1, 0, 2);

    line_buffers[0][0] = line_buffers[1][0] = 0; // pixel data line
    line_buffers[0][1] = line_buffers[1][1] = 0x15; // first 40 pixels
    line_buffers[0][VGA_PIXELS+2] = line_buffers[1][VGA_PIXELS+2] = 0x15; // last 40 pixels
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
int done_line = 0;
    while(true)
    {
	if (done_line != line_nr)
	{
	    done_line = line_nr;
            auto buf = line_buffers[active_buffer ^ 1];
	    if (done_line < 20 || done_line >= 220)
                for(int n=0; n<240; n++)
                    buf[n+2] = 0x15;
            else
                for(int n=0; n<240; n++)
                    buf[n+2] = rng();
	}
/*
        sleep_ms(500);
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        sleep_ms(500);
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        printf("%d %d\n", line_nr, vblank_timing);
        printf("%p %p\n", pio0->flevel, pio0->fdebug);
        pio0->fdebug = pio0->fdebug;
        printf("%p %p\n", pio0->sm[0].instr, pio0->sm[0].addr - vga_program_offset);
*/
    }
    return 0;
}
