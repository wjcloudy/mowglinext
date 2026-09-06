#!/usr/bin/env python3
"""Run the production ADC GPIO initialization with clock-aware HAL stubs.

The acquisition harness removes ADC_Charging_Init(), so it cannot catch a
missing port clock or configuring a different pin from the NTC input.
This executes the GPIO portion of that function on F103/F401/LFP profiles.
It tests initialization, not sensor accuracy or electrical motor interference.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

FW = Path(__file__).resolve().parents[1] / 'stm32/ros_usbnode'

SHIM = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef int ADC_TypeDef;
static ADC_TypeDef adc1, adc2;
#define ADC1 (&adc1)
#define ADC2 (&adc2)
typedef struct { unsigned clock, mode[16], pull[16]; } GPIO_TypeDef;
static GPIO_TypeDef port_a, port_c;
#define GPIOA (&port_a)
#define GPIOC (&port_c)
#define __HAL_RCC_ADC1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_ADC2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOA_CLK_ENABLE() (port_a.clock = 1)
#define __HAL_RCC_GPIOC_CLK_ENABLE() (port_c.clock = 1)
#define GPIO_PIN_1 (1u << 1)
#define GPIO_PIN_2 (1u << 2)
#define GPIO_PIN_3 (1u << 3)
#define GPIO_PIN_7 (1u << 7)
#define GPIO_MODE_ANALOG 3u
#define GPIO_NOPULL 0u
typedef struct { unsigned Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
static void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init) {
    assert(port->clock && "GPIO writes require the port clock first");
    for (unsigned pin = 0; pin < 16; ++pin) {
        if (init->Pin & (1u << pin)) {
            port->mode[pin] = init->Mode;
            port->pull[pin] = init->Pull;
        }
    }
}
'''

TEST = r'''
int main(void) {
    // Start PC3 in digital input with a pull to catch missing mode/pull setup.
    port_c.pull[3] = 1;
    port_c.mode[2] = 2; // unrelated PC2 must not be reconfigured
    ADC_Charging_Init();
    assert(port_a.clock && port_c.clock);
    assert(port_c.mode[3] == GPIO_MODE_ANALOG);
    assert(port_c.pull[3] == GPIO_NOPULL);
    assert(port_c.mode[2] == 2);
    const unsigned pins[] = {1, 2, 3, 7};
    for (unsigned i = 0; i < 4; ++i)
        assert(port_a.mode[pins[i]] == GPIO_MODE_ANALOG);
    puts("PASS: ADC GPIO clocks, PC3 analogue/no-pull, PA inputs, PC2 preserved");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--source', type=Path, default=FW / 'src/adc.c')
    args = parser.parse_args()
    source = args.source.read_text(encoding='utf-8')
    start = source.index('void ADC_Charging_Init(void)')
    end = source.index('    ADC_Charging_Handle.Instance', start)
    gpio_init = source[start:end] + '\n    (void)Charging_ADC;\n}\n'
    with tempfile.TemporaryDirectory(prefix='adc-gpio-') as directory:
        out = Path(directory)
        (out / 'test.c').write_text(SHIM + gpio_init + TEST, encoding='utf-8')
        binary = out / ('test.exe' if os.name == 'nt' else 'test')
        for defs in [['BOARD_YARDFORCE500_VARIANT_ORIG=1'],
                     ['BOARD_YARDFORCE500_VARIANT_B=1'],
                     ['BOARD_YARDFORCE500_VARIANT_B=1', 'BOARD_YARDFORCE500B_LFP=1']]:
            if Path(args.cc).stem.lower() == 'cl':
                cmd = [args.cc, '/nologo', '/std:c11', '/utf-8', '/W3',
                       *('/D' + d for d in defs), 'test.c', '/Fe:' + str(binary)]
            else:
                cmd = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-variable', *('-D' + d for d in defs),
                       'test.c', '-o', str(binary)]
            subprocess.run(cmd, cwd=out, check=True)
            subprocess.run([str(binary)], cwd=out, check=True)


if __name__ == '__main__':
    main()
