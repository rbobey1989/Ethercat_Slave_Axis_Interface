# Current Pin Usage

This file summarizes the pin usage currently hardcoded in the
`esc_ax58100_soes_emu_eeprom_app` firmware.

## Pin Status Summary

| Pins | Status | Function |
| --- | --- | --- |
| `PA4..PA7` | Used | AX58100 SPI PDI |
| `PC0`, `PC2`, `PC3` | Used | AX58100 SINT, reset, SYNC0 |
| `PB9`, `PB10`, `PB11`, `PC5` | Used | Boot EEPROM emulation on `I2C2`, `PDI_EMU`, `EEP_DONE` |
| `PE8..PE15` | Used | PWM direction and PWM outputs |
| `PA0`, `PA1`, `PA15`, `PB3`, `PD12`, `PD13`, `PC6`, `PC7` | Used | Quadrature encoder inputs |
| `PB4`, `PB5`, `PC8`, `PC9` | Used | Encoder capture inputs |
| `PD0..PD11`, `PE0..PE7`, `PA8..PA12`, `PB0..PB2`, `PB6..PB8`, `PC4` | Used | Temporary PDO GPIO pool |
| `PA13`, `PA14` | Reserved | SWD debug |
| `PH0`, `PH1` | Reserved | `HSE_OSC_IN` / `HSE_OSC_OUT` |
| `PB12..PB15`, `PC1`, `PC10..PC15`, `PD14`, `PD15` | Free | Available for new functions |

## ESC SPI / Reset / Interrupts

- `PA4`  : AX58100 SPI chip select
- `PA5`  : AX58100 SPI1 SCK
- `PA6`  : AX58100 SPI1 MISO
- `PA7`  : AX58100 SPI1 MOSI
- `PC2`  : AX58100 reset
- `PC0`  : AX58100 SINT / EXTI0
- `PC3`  : AX58100 SYNC0 / EXTI3

Sources:

- `lib/soes/hal/ax58100/spi_utils.c`
- `lib/soes/hal/ax58100/rst.c`
- `src/irq.c`

## PWM Outputs

- Axis 0: `PE9`  PWM, `PE8`  DIR
- Axis 1: `PE11` PWM, `PE10` DIR
- Axis 2: `PE13` PWM, `PE12` DIR
- Axis 3: `PE14` PWM, `PE15` DIR

Source:

- `src/pwm_dma.c`

## Encoder Inputs

Quadrature inputs:

- Axis 0: `PA0`, `PA1`
- Axis 1: `PA15`, `PB3`
- Axis 2: `PD12`, `PD13`
- Axis 3: `PC6`, `PC7`

Velocity capture inputs:

- Axis 0: `PB4`
- Axis 1: `PB5`
- Axis 2: `PC8`
- Axis 3: `PC9`

Source:

- `src/enc_dma.c`

## Temporary PDO GPIO Pool

Outputs:

- `PD0..PD11`
- `PE0..PE3`

Inputs:

- `PE4..PE7`
- `PA8..PA12`
- `PB0..PB2`
- `PB6..PB8`
- `PC4`

Source:

- `src/io.c`

## Pins That Are Free For New Functions

This list excludes:

- pins already claimed by the current firmware,
- SWD debug pins (`PA13`, `PA14`), and
- reserved clock pins (`PH0`, `PH1`) kept for a future HSE option.

- `PB12..PB15`
- `PC1`
- `PC10..PC15`
- `PD14`
- `PD15`

## Notes For Boot EEPROM Emulation

The current boot EEPROM emulation proposal uses:

- `PB10` as `I2C2_SCL` and is now used by the boot EEPROM scaffold
- `PB11` as `I2C2_SDA` and is now used by the boot EEPROM scaffold
- `PB9` drives `PDI_EMU`
- `PC5` is read as `EEP_DONE`

The AX58100 datasheet supports two boot EEPROM access families selected by the
`EEP_SIZE` bootstrap pin:

- `1 Kbit .. 16 Kbit`: control byte `1010 A10 A9 A8 R/W` plus one address byte
  (`24C16`-like access)
- `32 Kbit .. 4 Mbit`: control byte `1010 A18 A17 A16 R/W` plus two address
  bytes (`24C32+`-like access)

The current `eeprom.bin` is `2048` bytes (`16 Kbit`), but the firmware keeps a
`24C32+`-style boot model with two address bytes and base slave address `0x50`.
That means the hardware bootstrap for `EEP_SIZE` must select the `32 Kbit ..
4 Mbit` class so the STM32 can emulate the device with a single I2C slave
address.

Recommended hardware choice for a new PCB:

- Strap `LED_RUN / EEP_SIZE = 1` during reset.
- Treat the boot EEPROM interface as `24C32`-like, even if the current image is
  smaller than `32 Kbit`.
- Leave any unused upper EEPROM space returning `0xFF`.

## Reserved Non-GPIO Functions

- `PA13` : SWDIO
- `PA14` : SWCLK
- `PH0`  : `HSE_OSC_IN`
- `PH1`  : `HSE_OSC_OUT`

## Pins Intentionally Not Reused

- `PA13` and `PA14` are left alone for SWD debug.
- `PH0` and `PH1` are reserved for `HSE_OSC_IN` / `HSE_OSC_OUT` even though the
  current firmware clock tree starts from HSI.
- `PB6..PB8` are already part of the temporary input pool, so `I2C1` is not a
  clean fit unless `src/io.c` is remapped.
- `PE0..PE15` are effectively full once PWM, direction, and the temporary IO
  pool are considered.