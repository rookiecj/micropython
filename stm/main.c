#include <stdio.h>
#include <stm32f4xx.h>
#include <stm32f4xx_rcc.h>
#include <stm32f4xx_gpio.h>
#include <stm32f4xx_tim.h>
#include <stm_misc.h>
#include "std.h"

#include "misc.h"
#include "mpyconfig.h"
#include "gc.h"
#include "systick.h"
#include "led.h"
#include "lcd.h"
#include "storage.h"
#include "mma.h"
#include "usb.h"
#include "ff.h"

static FATFS fatfs0;

extern uint32_t _heap_start;

void flash_error(int n) {
    for (int i = 0; i < n; i++) {
        led_state(PYB_LED_R1, 1);
        led_state(PYB_LED_R2, 0);
        sys_tick_delay_ms(250);
        led_state(PYB_LED_R1, 0);
        led_state(PYB_LED_R2, 1);
        sys_tick_delay_ms(250);
    }
    led_state(PYB_LED_R2, 0);
}

static void impl02_c_version(void) {
    int x = 0;
    while (x < 400) {
        int y = 0;
        while (y < 400) {
            volatile int z = 0;
            while (z < 400) {
                z = z + 1;
            }
            y = y + 1;
        }
        x = x + 1;
    }
}

#define PYB_USRSW_PORT (GPIOA)
#define PYB_USRSW_PIN (GPIO_Pin_13)

void sw_init(void) {
    // make it an input with pull-up
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = PYB_USRSW_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(PYB_USRSW_PORT, &GPIO_InitStructure);
}

int sw_get(void) {
    if (PYB_USRSW_PORT->IDR & PYB_USRSW_PIN) {
        // pulled high, so switch is not pressed
        return 0;
    } else {
        // pulled low, so switch is pressed
        return 1;
    }
}

void __fatal_error(const char *msg) {
    lcd_print_strn("\nFATAL ERROR:\n", 14);
    lcd_print_strn(msg, strlen(msg));
    for (;;) {
        flash_error(1);
    }
}

#include "nlr.h"
#include "misc.h"
#include "lexer.h"
#include "lexerstm.h"
#include "mpyconfig.h"
#include "parse.h"
#include "compile.h"
#include "runtime.h"
#include "repl.h"

static qstr pyb_config_source_dir = 0;
static qstr pyb_config_main = 0;

py_obj_t pyb_source_dir(py_obj_t source_dir) {
    pyb_config_source_dir = py_get_qstr(source_dir);
    return py_const_none;
}

py_obj_t pyb_main(py_obj_t main) {
    pyb_config_main = py_get_qstr(main);
    return py_const_none;
}

// sync all file systems
py_obj_t pyb_sync(void) {
    storage_flush();
    return py_const_none;
}

py_obj_t pyb_delay(py_obj_t count) {
    sys_tick_delay_ms(py_get_int(count));
    return py_const_none;
}

py_obj_t pyb_led(py_obj_t state) {
    led_state(PYB_LED_G1, rt_is_true(state));
    return state;
}

py_obj_t pyb_sw(void) {
    if (sw_get()) {
        return py_const_true;
    } else {
        return py_const_false;
    }
}

/*
void g(uint i) {
    printf("g:%d\n", i);
    if (i & 1) {
        nlr_jump((void*)(42 + i));
    }
}
void f(void) {
    nlr_buf_t nlr;
    int i;
    for (i = 0; i < 4; i++) {
        printf("f:loop:%d:%p\n", i, &nlr);
        if (nlr_push(&nlr) == 0) {
            // normal
            //printf("a:%p:%p %p %p %u\n", &nlr, nlr.ip, nlr.sp, nlr.prev, nlr.ret_val);
            g(i);
            printf("f:lp:%d:nrm\n", i);
            nlr_pop();
        } else {
            // nlr
            //printf("b:%p:%p %p %p %u\n", &nlr, nlr.ip, nlr.sp, nlr.prev, nlr.ret_val);
            printf("f:lp:%d:nlr:%d\n", i, (int)nlr.ret_val);
        }
    }
}
void nlr_test(void) {
    f(1);
}
*/

void fatality(void) {
    led_state(PYB_LED_R1, 1);
    led_state(PYB_LED_G1, 1);
    led_state(PYB_LED_R2, 1);
    led_state(PYB_LED_G2, 1);
}

static const char fresh_boot_py[] =
"# boot.py -- run on boot-up\n"
"# can run arbitrary Python, but best to keep it minimal\n"
"\n"
"pyb.source_dir('/src')\n"
"pyb.main('main.py')\n"
"#pyb.usb_usr('VCP')\n"
"#pyb.usb_msd(True, 'dual partition')\n"
"#pyb.flush_cache(False)\n"
"#pyb.error_log('error.txt')\n"
;

// get lots of info about the board
static py_obj_t pyb_info(void) {
    // get and print clock speeds
    // SYSCLK=168MHz, HCLK=168MHz, PCLK1=42MHz, PCLK2=84MHz
    {
        RCC_ClocksTypeDef rcc_clocks;
        RCC_GetClocksFreq(&rcc_clocks);
        printf("S=%lu\nH=%lu\nP1=%lu\nP2=%lu\n", rcc_clocks.SYSCLK_Frequency, rcc_clocks.HCLK_Frequency, rcc_clocks.PCLK1_Frequency, rcc_clocks.PCLK2_Frequency);
    }

    // to print info about memory
    {
        extern void *_sidata;
        extern void *_sdata;
        extern void *_edata;
        extern void *_sbss;
        extern void *_ebss;
        extern void *_estack;
        extern void *_etext;
        printf("_sidata=%p\n", &_sidata);
        printf("_sdata=%p\n", &_sdata);
        printf("_edata=%p\n", &_edata);
        printf("_sbss=%p\n", &_sbss);
        printf("_ebss=%p\n", &_ebss);
        printf("_estack=%p\n", &_estack);
        printf("_etext=%p\n", &_etext);
        printf("_heap_start=%p\n", &_heap_start);
    }

    // GC info
    {
        gc_info_t info;
        gc_info(&info);
        printf("GC:\n");
        printf("  %lu total\n", info.total);
        printf("  %lu : %lu\n", info.used, info.free);
        printf("  1=%lu 2=%lu m=%lu\n", info.num_1block, info.num_2block, info.max_block);
    }

    // free space on flash
    {
        DWORD nclst;
        FATFS *fatfs;
        f_getfree("0:", &nclst, &fatfs);
        printf("LFS free: %u bytes\n", (uint)(nclst * fatfs->csize * 512));
    }

    return py_const_none;
}

int readline(vstr_t *line, const char *prompt) {
    usb_vcp_send_str(prompt);
    int len = vstr_len(line);
    for (;;) {
        while (usb_vcp_rx_any() == 0) {
            sys_tick_delay_ms(10);
        }
        char c = usb_vcp_rx_get();
        if (c == 4 && vstr_len(line) == len) {
            return 0;
        } else if (c == '\r') {
            usb_vcp_send_str("\r\n");
            return 1;
        } else if (c == 127) {
            if (vstr_len(line) > len) {
                vstr_cut_tail(line, 1);
                usb_vcp_send_str("\b \b");
            }
        } else if (32 <= c && c <= 126) {
            vstr_add_char(line, c);
            usb_vcp_send_strn(&c, 1);
        }
        sys_tick_delay_ms(100);
    }
}

void do_repl(void) {
    usb_vcp_send_str("Micro Python 0.5; STM32F405RG; PYBv2\r\n");
    usb_vcp_send_str("Type \"help\" for more information.\r\n");

    vstr_t line;
    vstr_init(&line);

    for (;;) {
        vstr_reset(&line);
        int ret = readline(&line, ">>> ");
        if (ret == 0) {
            // EOF
            break;
        }

        if (vstr_len(&line) == 0) {
            continue;
        }

        if (py_repl_is_compound_stmt(vstr_str(&line))) {
            for (;;) {
                vstr_add_char(&line, '\n');
                int len = vstr_len(&line);
                int ret = readline(&line, "... ");
                if (ret == 0 || vstr_len(&line) == len) {
                    // done entering compound statement
                    break;
                }
            }
        }

        py_lexer_str_buf_t sb;
        py_lexer_t *lex = py_lexer_new_from_str_len("<stdin>", vstr_str(&line), vstr_len(&line), false, &sb);
        py_parse_node_t pn = py_parse(lex, PY_PARSE_SINGLE_INPUT);
        py_lexer_free(lex);

        if (pn != PY_PARSE_NODE_NULL) {
            bool comp_ok = py_compile(pn, true);
            if (comp_ok) {
                py_obj_t module_fun = rt_make_function_from_id(1);
                if (module_fun != py_const_none) {
                    nlr_buf_t nlr;
                    if (nlr_push(&nlr) == 0) {
                        rt_call_function_0(module_fun);
                        nlr_pop();
                    } else {
                        // uncaught exception
                        py_obj_print((py_obj_t)nlr.ret_val);
                        printf("\n");
                    }
                }
            }
        }
    }

    usb_vcp_send_str("\r\n");
}

bool do_file(const char *filename) {
    py_lexer_file_buf_t fb;
    py_lexer_t *lex = py_lexer_new_from_file(filename, &fb);

    if (lex == NULL) {
        printf("could not open file '%s' for reading\n", filename);
        return false;
    }

    py_parse_node_t pn = py_parse(lex, PY_PARSE_FILE_INPUT);
    py_lexer_free(lex);

    if (pn == PY_PARSE_NODE_NULL) {
        return false;
    }

    bool comp_ok = py_compile(pn, false);
    if (!comp_ok) {
        return false;
    }

    py_obj_t module_fun = rt_make_function_from_id(1);
    if (module_fun == py_const_none) {
        return false;
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        rt_call_function_0(module_fun);
        nlr_pop();
        return true;
    } else {
        // uncaught exception
        py_obj_print((py_obj_t)nlr.ret_val);
        printf("\n");
        return false;
    }
}

#define RAM_START (0x20000000) // fixed for chip
#define HEAP_END  (0x2001c000) // tunable
#define RAM_END   (0x20020000) // fixed for chip

void gc_helper_get_regs_and_clean_stack(machine_uint_t *regs, machine_uint_t heap_end);

void gc_collect(void) {
    uint32_t start = sys_tick_counter;
    gc_collect_start();
    gc_collect_root((void**)RAM_START, (((uint32_t)&_heap_start) - RAM_START) / 4);
    machine_uint_t regs[10];
    gc_helper_get_regs_and_clean_stack(regs, HEAP_END);
    gc_collect_root((void**)HEAP_END, (RAM_END - HEAP_END) / 4); // will trace regs since they now live in this function on the stack
    gc_collect_end();
    uint32_t ticks = sys_tick_counter - start; // TODO implement a function that does this properly
    gc_info_t info;
    gc_info(&info);
    printf("GC@%lu %lums\n", start, ticks);
    printf(" %lu total\n", info.total);
    printf(" %lu : %lu\n", info.used, info.free);
    printf(" 1=%lu 2=%lu m=%lu\n", info.num_1block, info.num_2block, info.max_block);
}

py_obj_t pyb_gc(void) {
    gc_collect();
    return py_const_none;
}

// PWM
// TIM2 and TIM5 have CH1, CH2, CH3, CH4 on PA0-PA3 respectively
// they are both 32-bit counters
// 16-bit prescaler
// TIM2_CH3 also on PB10 (used below)
void servo_init(void) {
    // TIM2 clock enable
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    // GPIOC Configuration: TIM2_CH3 (PB10)
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // Connect TIM2 pins to AF1
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_TIM2);

    // Compute the prescaler value so TIM2 runs at 100kHz
    uint16_t PrescalerValue = (uint16_t) ((SystemCoreClock / 2) / 100000) - 1;

    // Time base configuration
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_TimeBaseStructure.TIM_Period = 2000; // timer cycles at 50Hz
    TIM_TimeBaseStructure.TIM_Prescaler = PrescalerValue;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    // PWM1 Mode configuration: Channel1
    TIM_OCInitTypeDef TIM_OCInitStructure;
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 150; // units of 10us
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC3Init(TIM2, &TIM_OCInitStructure);

    // ?
    TIM_OC3PreloadConfig(TIM2, TIM_OCPreload_Enable);

    // ?
    TIM_ARRPreloadConfig(TIM2, ENABLE);

    // TIM2 enable counter
    TIM_Cmd(TIM2, ENABLE);
}

py_obj_t pyb_servo_set(py_obj_t value) {
    int v = py_get_int(value);
    if (v < 100) { v = 100; }
    if (v > 200) { v = 200; }
    TIM2->CCR3 = v;
    return py_const_none;
}

int main(void) {
    // TODO disable JTAG

    // set interrupt priority config to use all 4 bits for pre-empting
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    // enable the CCM RAM and the GPIO's
    RCC->AHB1ENR |= RCC_AHB1ENR_CCMDATARAMEN | RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

    // basic sub-system init
    sys_tick_init();
    led_init();

    // turn on LED to indicate bootup
    led_state(PYB_LED_G1, 1);

    // more sub-system init
    sw_init();
    storage_init();

soft_reset:

    // LCD init
    lcd_init();

    // GC init
    gc_init(&_heap_start, (void*)HEAP_END);

    // Micro Python init
    qstr_init();
    rt_init();

    // servo
    servo_init();

    // add some functions to the python namespace
    {
        py_obj_t m = py_module_new();
        rt_store_attr(m, qstr_from_str_static("info"), rt_make_function_0(pyb_info));
        rt_store_attr(m, qstr_from_str_static("source_dir"), rt_make_function_1(pyb_source_dir));
        rt_store_attr(m, qstr_from_str_static("main"), rt_make_function_1(pyb_main));
        rt_store_attr(m, qstr_from_str_static("sync"), rt_make_function_0(pyb_sync));
        rt_store_attr(m, qstr_from_str_static("gc"), rt_make_function_0(pyb_gc));
        rt_store_attr(m, qstr_from_str_static("delay"), rt_make_function_1(pyb_delay));
        rt_store_attr(m, qstr_from_str_static("led"), rt_make_function_1(pyb_led));
        rt_store_attr(m, qstr_from_str_static("sw"), rt_make_function_0(pyb_sw));
        rt_store_attr(m, qstr_from_str_static("servo"), rt_make_function_1(pyb_servo_set));
        rt_store_name(qstr_from_str_static("pyb"), m);
    }

    // print a message to the LCD
    lcd_print_str(" micro py board\n");

    // local filesystem init
    {
        // try to mount the flash
        FRESULT res = f_mount(&fatfs0, "0:", 1);
        if (res == FR_OK) {
            // mount sucessful
        } else if (res == FR_NO_FILESYSTEM) {
            // no filesystem, so create a fresh one

            // LED on to indicate creation of LFS
            led_state(PYB_LED_R2, 1);
            uint32_t stc = sys_tick_counter;

            res = f_mkfs("0:", 0, 0);
            if (res == FR_OK) {
                // success creating fresh LFS
            } else {
                __fatal_error("could not create LFS");
            }

            // keep LED on for at least 200ms
            sys_tick_wait_at_least(stc, 200);
            led_state(PYB_LED_R2, 0);
        } else {
            __fatal_error("could not access LFS");
        }
    }

    // make sure we have a /boot.py
    {
        FILINFO fno;
        FRESULT res = f_stat("0:/boot.py", &fno);
        if (res == FR_OK) {
            if (fno.fattrib & AM_DIR) {
                // exists as a directory
                // TODO handle this case
                // see http://elm-chan.org/fsw/ff/img/app2.c for a "rm -rf" implementation
            } else {
                // exists as a file, good!
            }
        } else {
            // doesn't exist, create fresh file

            // LED on to indicate creation of boot.py
            led_state(PYB_LED_R2, 1);
            uint32_t stc = sys_tick_counter;

            FIL fp;
            f_open(&fp, "0:/boot.py", FA_WRITE | FA_CREATE_ALWAYS);
            UINT n;
            f_write(&fp, fresh_boot_py, sizeof(fresh_boot_py) - 1 /* don't count null terminator */, &n);
            // TODO check we could write n bytes
            f_close(&fp);

            // keep LED on for at least 200ms
            sys_tick_wait_at_least(stc, 200);
            led_state(PYB_LED_R2, 0);
        }
    }

    // run /boot.py
    if (!do_file("0:/boot.py")) {
        flash_error(4);
    }

    // USB
    usb_init();

    // turn boot-up LED off
    led_state(PYB_LED_G1, 0);

    // run main script
    {
        vstr_t *vstr = vstr_new();
        vstr_add_str(vstr, "0:/");
        if (pyb_config_source_dir == 0) {
            vstr_add_str(vstr, "src");
        } else {
            vstr_add_str(vstr, qstr_str(pyb_config_source_dir));
        }
        vstr_add_char(vstr, '/');
        if (pyb_config_main == 0) {
            vstr_add_str(vstr, "main.py");
        } else {
            vstr_add_str(vstr, qstr_str(pyb_config_main));
        }
        if (!do_file(vstr_str(vstr))) {
            flash_error(3);
        }
        vstr_free(vstr);
    }

    //printf("init;al=%u\n", m_get_total_bytes_allocated()); // 1600, due to qstr_init
    //sys_tick_delay_ms(1000);

    // Python!
    if (0) {
        //const char *pysrc = "def f():\n  x=x+1\nprint(42)\n";
        const char *pysrc =
            // impl01.py
            /*
            "x = 0\n"
            "while x < 400:\n"
            "    y = 0\n"
            "    while y < 400:\n"
            "        z = 0\n"
            "        while z < 400:\n"
            "            z = z + 1\n"
            "        y = y + 1\n"
            "    x = x + 1\n";
            */
            // impl02.py
            /*
            "#@micropython.native\n"
            "def f():\n"
            "    x = 0\n"
            "    while x < 400:\n"
            "        y = 0\n"
            "        while y < 400:\n"
            "            z = 0\n"
            "            while z < 400:\n"
            "                z = z + 1\n"
            "            y = y + 1\n"
            "        x = x + 1\n"
            "f()\n";
            */
            /*
            "print('in python!')\n"
            "x = 0\n"
            "while x < 4:\n"
            "    pyb_led(True)\n"
            "    pyb_delay(201)\n"
            "    pyb_led(False)\n"
            "    pyb_delay(201)\n"
            "    x += 1\n"
            "print('press me!')\n"
            "while True:\n"
            "    pyb_led(pyb_sw())\n";
            */
            /*
            // impl16.py
            "@micropython.asm_thumb\n"
            "def delay(r0):\n"
            "    b(loop_entry)\n"
            "    label(loop1)\n"
            "    movw(r1, 55999)\n"
            "    label(loop2)\n"
            "    subs(r1, r1, 1)\n"
            "    cmp(r1, 0)\n"
            "    bgt(loop2)\n"
            "    subs(r0, r0, 1)\n"
            "    label(loop_entry)\n"
            "    cmp(r0, 0)\n"
            "    bgt(loop1)\n"
            "print('in python!')\n"
            "@micropython.native\n"
            "def flash(n):\n"
            "    x = 0\n"
            "    while x < n:\n"
            "        pyb_led(True)\n"
            "        delay(249)\n"
            "        pyb_led(False)\n"
            "        delay(249)\n"
            "        x = x + 1\n"
            "flash(20)\n";
            */
            // impl18.py
            /*
            "# basic exceptions\n"
            "x = 1\n"
            "try:\n"
            "    x.a()\n"
            "except:\n"
            "    print(x)\n";
            */
            // impl19.py
            "# for loop\n"
            "def f():\n"
            "    for x in range(400):\n"
            "        for y in range(400):\n"
            "            for z in range(400):\n"
            "                pass\n"
            "f()\n";

        py_lexer_str_buf_t py_lexer_str_buf;
        py_lexer_t *lex = py_lexer_new_from_str_len("<stdin>", pysrc, strlen(pysrc), false, &py_lexer_str_buf);

        // nalloc=1740;6340;6836 -> 140;4600;496 bytes for lexer, parser, compiler
        printf("lex; al=%u\n", m_get_total_bytes_allocated());
        sys_tick_delay_ms(1000);
        py_parse_node_t pn = py_parse(lex, PY_PARSE_FILE_INPUT);
        py_lexer_free(lex);
        if (pn != PY_PARSE_NODE_NULL) {
            printf("pars;al=%u\n", m_get_total_bytes_allocated());
            sys_tick_delay_ms(1000);
            //parse_node_show(pn, 0);
            bool comp_ok = py_compile(pn, false);
            printf("comp;al=%u\n", m_get_total_bytes_allocated());
            sys_tick_delay_ms(1000);

            if (!comp_ok) {
                printf("compile error\n");
            } else {
                // execute it!

                py_obj_t module_fun = rt_make_function_from_id(1);

                // flash once
                led_state(PYB_LED_G1, 1);
                sys_tick_delay_ms(100);
                led_state(PYB_LED_G1, 0);

                nlr_buf_t nlr;
                if (nlr_push(&nlr) == 0) {
                    py_obj_t ret = rt_call_function_0(module_fun);
                    printf("done! got: ");
                    py_obj_print(ret);
                    printf("\n");
                    nlr_pop();
                } else {
                    // uncaught exception
                    printf("exception: ");
                    py_obj_print((py_obj_t)nlr.ret_val);
                    printf("\n");
                }

                // flash once
                led_state(PYB_LED_G1, 1);
                sys_tick_delay_ms(100);
                led_state(PYB_LED_G1, 0);

                sys_tick_delay_ms(1000);
                printf("nalloc=%u\n", m_get_total_bytes_allocated());
                sys_tick_delay_ms(1000);
            }
        }
    }

    do_repl();

    // benchmark C version of impl02.py
    if (0) {
        led_state(PYB_LED_G1, 1);
        sys_tick_delay_ms(100);
        led_state(PYB_LED_G1, 0);
        impl02_c_version();
        led_state(PYB_LED_G1, 1);
        sys_tick_delay_ms(100);
        led_state(PYB_LED_G1, 0);
    }

    // MMA testing
    if (0) {
        printf("1");
        mma_init();
        printf("2");
        mma_start(0x4c, 1);
        printf("3");
        mma_send_byte(0);
        printf("4");
        mma_stop();
        printf("5");
        mma_start(0x4c, 1);
        printf("6");
        mma_send_byte(0);
        printf("7");
        mma_restart(0x4c, 0);
        for (int i = 0; i <= 0xa; i++) {
            int data;
            if (i == 0xa) {
                data = mma_read_nack();
            } else {
                data = mma_read_ack();
            }
            printf(" %02x", data);
        }
        printf("\n");

        mma_start(0x4c, 1);
        mma_send_byte(7); // mode
        mma_send_byte(1); // active mode
        mma_stop();

        for (;;) {
            sys_tick_delay_ms(500);

            mma_start(0x4c, 1);
            mma_send_byte(0);
            mma_restart(0x4c, 0);
            for (int i = 0; i <= 3; i++) {
                int data;
                if (i == 3) {
                    data = mma_read_nack();
                    printf(" %02x\n", data);
                } else {
                    data = mma_read_ack() & 0x3f;
                    if (data & 0x20) {
                        data |= 0xc0;
                    }
                    printf(" % 2d", data);
                }
            }
        }
    }

    // SD card testing
    if (0) {
        //sdio_init();
    }

    printf("PYB: sync filesystems\n");
    pyb_sync();

    printf("PYB: soft reboot\n");
    goto soft_reset;
}