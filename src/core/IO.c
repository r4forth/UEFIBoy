#include "cpu.h"
#include "graphics.h"
#include "IO.h"
#include <stdint.h>
#include "memory_layout.h"

typedef struct {
    uint8_t isr_addr; /* Interrupt Service Routine Address */
    uint8_t flag; /* bit set to compare with IF_FLAG to 
                    check if interrupt has been raised */
} Interrupt;


static uint8_t io_mem[(0x10000 - 0xFF00)];

static const Interrupt interrupts[] = { 
    {.flag = BIT_0, .isr_addr = VBLANK_ISR_ADDR},
    {.flag = BIT_1, .isr_addr = LCDC_ISR_ADDR},
    {.flag = BIT_2, .isr_addr = TIMER_ISR_ADDR},
    {.flag = BIT_3, .isr_addr = IO_ISR_ADDR},
    {.flag = BIT_3, .isr_addr = HIGH_LOW_ISR_ADDR}
   };

#define INTERRUPTS_LEN sizeof (interrupts) / sizeof (Interrupt)


/*  Check if any interrupts need to be handled, and handle them */
void check_interrupts() {

    #define IO_IF_FLAG GLOBAL_TO_IO_ADDR(IF_FLAG)
    #define IO_IE_REG  GLOBAL_TO_IO_ADDR(IE_REG)
    // Get all interrupts which flag has been set and are enabled
    uint8_t if_flag = io_mem[IO_IF_FLAG];
    uint8_t possible_interrupts = if_flag & io_mem[IO_IE_REG] & 0xF;
 
    if (possible_interrupts != 0) {
        /* Go through each interrupt and check if it has been raised */
        for (unsigned long i = 0; i < INTERRUPTS_LEN; i++) {

            uint8_t flag = interrupts[i].flag;
            if ((flag & possible_interrupts) != 0) {
                /*  Still need to check if master override is not in place */
                if (master_interrupts_enabled()) {
                    /* Unset interrupt flag for interrupt being serviced
                     * unset master interrupts so interrupt handler routine
                     * isn't unecessarily interrupted and then call
                     * the interrupt handler */
                    io_mem[IO_IF_FLAG] = if_flag & ~flag;
                    master_interrupts_disable(); 
                    restart(interrupts[i].isr_addr);
                }
                /* If CPU is halted, even if MIE if off, 
                 * interrupt unhalts the cpu*/
                unhalt_cpu();
            }
        }
    }
}

void io_set_mem(uint8_t io_addr, uint8_t val) {

    uint16_t global_addr = IO_TO_GLOBAL_ADDR(io_addr);
    io_mem[io_addr] = val;
    switch (global_addr) {
        /*  Timers */
        case TIMA_REG : break;
        case TMA_REG  : break;
        case TAC_REG  : break;
        /*  Attempting to set DIV reg resets it to 0 */
        case DIV_REG  : io_mem[io_addr] = 0; break;
    }
}

uint8_t io_get_mem(uint8_t io_addr) {
    return io_mem[io_addr];
}

/*  Increments the TIMA register
 *  if causes overflow, timer interrupt is raised*/
void increment_tima() {

    #define IO_TIMA_REG GLOBAL_TO_IO_ADDR(TIMA_REG)
    io_mem[IO_TIMA_REG] += 1;
    /*  Overflow */
    if (io_mem[IO_TIMA_REG] == 0) {
        io_mem[GLOBAL_TO_IO_ADDR(IF_FLAG)] |= BIT_2;
    } 
}

/*  Increcements LY registers
 *  if LY becomes greater than 143 then
 *  VBlank interrupt is raised */
void increment_ly() {

    #define IO_LY_REG GLOBAL_TO_IO_ADDR(LY_REG)
    uint8_t ly = (io_mem[IO_LY_REG] +1) % 154;
    io_mem[IO_LY_REG] = ly;

    if (ly < 144) {
        //draw_row(ly);
    } else { /*  V-Blank interrupt */
       io_mem[GLOBAL_TO_IO_ADDR(IF_FLAG)] |= BIT_0;
    }
}

/*  Increment DIV register */
void increment_div() {

}

typedef enum {RIGHT = 0x1, LEFT = 0x2, UP = 0x4, DOWN = 0x8, 
    A = 0x10, B = 0x20, SELECT = 0x40, START = 0x80} button_t;


int joypad;
uint8_t joypad_reg_value = 0x0;

void joypad_pressed(button_t button) {
    

}


/*  Reset current keys pressed */
void joypad_reset() {
    joypad  =0x0;
}


/*  Write the current joypad state
 *  to the joypad register in memory */
void joypad_write_to_mem() {
    //set_mem(P1_REG, joypad);
}



/*  Managing FF40 LCDC control reg */   
lcd_ctrl_t get_lcd_control() {
    uint8_t lcd_ctrl_val = io_mem[GLOBAL_TO_IO_ADDR(LCDC_REG)];
    lcd_ctrl_t lcd_ctrl;
    
    lcd_ctrl.operation = lcd_ctrl_val & BIT_7; /* 0 no display, 1 otherwise */
    lcd_ctrl.win_tile_map = lcd_ctrl_val & BIT_6; /* 0: 0x9800-0x9BFF, 1: 0x9C00-0x9FFF */
    lcd_ctrl.win_display = lcd_ctrl_val & BIT_5; /* 0: off, 1: on */
    lcd_ctrl.bg_win_tile = lcd_ctrl_val & BIT_4; /* 0: 0x8800-0x97FF, 1: 0x8000-0x8FFF */
    lcd_ctrl.bg_tile_map = lcd_ctrl_val & BIT_3; /* 0: 0x9800-0x97FF, 1: 0x9C00-0x9FFF */
    lcd_ctrl.sprite_size = lcd_ctrl_val & BIT_2; /* 0: 8x8. 1: 8*x16  (width * height) */
    lcd_ctrl.sprite_display = lcd_ctrl_val & BIT_1; /* 0: off, 1: on */
    lcd_ctrl.bg_window_display = lcd_ctrl_val & BIT_0; /* 0 off, 1: on  */

    return lcd_ctrl;
}

void lcd_status() {
    uint8_t lcd_stat = io_mem[GLOBAL_TO_IO_ADDR(STAT_REG)];

    int lyc_equals_ly_coincidence = lcd_stat & BIT_6;
    int oam_search = lcd_stat * BIT_5;
    int v_blank = lcd_stat & BIT_4;
    int h_blank = lcd_stat & BIT_3;
    int coincidence_flag = lcd_stat & BIT_2; /* 0: LYC != LCDC LY, 1: equals */
    int mode_flag = lcd_stat & (BIT_1 | BIT_0);
    /*  0 : During H_Blank. Entire Display RAM (0x8000-0x9FFF) can be accessed */
    /*  1 : During V_BLANK. Entire Display RAM (0x8000-0x9FFF) can be accessed 
     *  2 : During Searching OAM_RAM (0xFE00-0xFE9F), OAM cannot be accessed */
    /*  3 : During Transferring Data to LCD Driver. CPU cannot access OAM
     *      and display RAM during this period */
}



/*  Get amount of scrolling in X/Y directions */
uint8_t get_scroll_y() {
    return io_mem[GLOBAL_TO_IO_ADDR(SCROLL_Y_REG)];
}

uint8_t get_scroll_x() {
    return io_mem[GLOBAL_TO_IO_ADDR(SCROLL_X_REG)];
}


/*  Vertical line which present data transferred
 *  to the LCD Driver- LY can take any value 0-153, 
 *  144-153 indicate V_Blank period. Writing will
 *  reset the counter */
//uint8_t get_ly() {
//    return get_mem(LY_REG);
//}



/*  Compares with LY, if values are the same, causes STAT
 *  to set the coincidence flag */
//uint8_t get_lyc() {
//    return get_mem(LYC_REG);
//}


/*  DMA Transfer  */
/*  (40*28 bit) from internal ROM or RAM (0x0000-0xF19F)
 *  to the OAM (0xFE00-0xFE9F) */
//void dma() {
    
//}


//uint8_t bg_palette() {
    /*  7-6 Data for Darkest colour */
    /*  5-4 Data for Dark colour */
    /*  3-2 Data for Light Colour */
    /*  1-0 Data for Lightest Colour */
//    return 0;
//}


//uint8_t OBP0() {
    /*  Selects colours for sprite palette 0. Works exactly as BGP
     *  (0xFF47) except 0 is transparent */
//    return 0;
//}

//uint8_t OBP1() {
   /* Selects the colours for sprite palette 1. Works exactly as OBPO
    * (0xFF48) */
//   return 0;
//}

uint8_t get_win_y_pos() {
    /*  0<=WY<=143 */
    return io_mem[GLOBAL_TO_IO_ADDR(WY_REG)];
}

uint8_t get_win_x_pos() {
    /*  0<=WX<=166 */
    return io_mem[GLOBAL_TO_IO_ADDR(WX_REG)];
}


//void interrupt_enable() {
//    uint8_t interrupt = get_mem(IE_REG);

// }
























