# esc_ax58100_soes_phy_eeprom_app

Inventario de pines usados por la variante con EEPROM fisica para el AX58100.

Alcance de este documento:

- STM32 objetivo: `STM32F407VGT6`
- Aplicacion: `esc_ax58100_soes_phy_eeprom_app`
- EEPROM de arranque del AX58100: fisica, externa al STM32
- Reloj actual del firmware: `HSI -> PLL -> 168 MHz`

## Resumen

Esta variante no emula la EEPROM de arranque desde el STM32. El AX58100 debe
cargar su EEPROM fisica directamente en el lado ESC. Por tanto, en esta app el
STM32 no reclama pines de `SCL`, `SDA` ni `EEP_DONE` para boot EEPROM.
`PDI_EMU` no se trata aqui como señal de estado del arranque, sino como pin
bootstrap del AX58100 para `0x0141.0` (`Device emulation`).

Recuento consolidado sobre los puertos visibles en el firmware:

| Grupo | Cantidad |
| --- | ---: |
| GPIO usados por el firmware | 63 |
| GPIO reservados | 4 |
| GPIO libres | 15 |

Reservas consideradas:

- `PA13`, `PA14`: SWD
- `PH0`, `PH1`: HSE futuro de PCB, aunque hoy el firmware usa HSI

## Resumen por puerto

| Puerto | Usados por firmware | Reservados | Libres |
| --- | --- | --- | --- |
| `PA` | `PA0`, `PA1`, `PA4`, `PA5`, `PA6`, `PA7`, `PA8`, `PA9`, `PA10`, `PA11`, `PA12`, `PA15` | `PA13`, `PA14` | `PA2`, `PA3` |
| `PB` | `PB0`, `PB1`, `PB2`, `PB3`, `PB4`, `PB5`, `PB6`, `PB7`, `PB8`, `PB9`, `PB10`, `PB11`, `PB12` | none | `PB13`, `PB14`, `PB15` |
| `PC` | `PC0`, `PC2`, `PC3`, `PC4`, `PC6`, `PC7`, `PC8`, `PC9` | none | `PC1`, `PC5`, `PC10`, `PC11`, `PC12`, `PC13`, `PC14`, `PC15` |
| `PD` | `PD0`, `PD1`, `PD2`, `PD3`, `PD4`, `PD5`, `PD6`, `PD7`, `PD8`, `PD9`, `PD10`, `PD11`, `PD12`, `PD13` | none | `PD14`, `PD15` |
| `PE` | `PE0`, `PE1`, `PE2`, `PE3`, `PE4`, `PE5`, `PE6`, `PE7`, `PE8`, `PE9`, `PE10`, `PE11`, `PE12`, `PE13`, `PE14`, `PE15` | none | none |
| `PH` | none | `PH0`, `PH1` | none |

## EtherCAT AX58100

Pines STM32 dedicados a la interfaz PDI SPI y control del ESC:

| Funcion | Pin STM32 | Detalle |
| --- | --- | --- |
| `ESC_CS` | `PA4` | Chip-select SPI del AX58100 |
| `SPI1_SCK` | `PA5` | Reloj SPI |
| `SPI1_MISO` | `PA6` | Datos ESC -> STM32 |
| `SPI1_MOSI` | `PA7` | Datos STM32 -> ESC |
| `ESC_RST` | `PC2` | Reset del AX58100 |
| `ESC_SINT` | `PC0` | Interrupcion principal del ESC, `EXTI0` |
| `ESC_SYNC0` | `PC3` | Sync0/DC, `EXTI3` cuando se habilita DC |

## PWM por eje

Salidas PWM y direccion:

| Eje | PWM | Timer | Direccion |
| --- | --- | --- | --- |
| 0 | `PE9` | `TIM1_CH1` | `PE8` |
| 1 | `PE11` | `TIM1_CH2` | `PE10` |
| 2 | `PE13` | `TIM1_CH3` | `PE12` |
| 3 | `PE14` | `TIM1_CH4` | `PE15` |

Perifericos asociados:

- `TIM1` para las cuatro PWM
- `DMA2 Stream5 Channel6` para actualizar `CCR1..CCR4`

## Encoders por eje

Entradas de posicion en cuadratura:

| Eje | Canal A | Canal B | Timer |
| --- | --- | --- | --- |
| 0 | `PA0` | `PA1` | `TIM5_CH1/CH2` |
| 1 | `PA15` | `PB3` | `TIM2_CH1/CH2` |
| 2 | `PD12` | `PD13` | `TIM4_CH1/CH2` |
| 3 | `PC6` | `PC7` | `TIM8_CH1/CH2` |

Captura lenta de periodo, normalmente derivada de la fase A:

| Eje | Pin | Timer | DMA |
| --- | --- | --- | --- |
| 0 | `PB4` | `TIM3_CH1` | `DMA1 Stream4 Channel5` |
| 1 | `PB5` | `TIM3_CH2` | `DMA1 Stream5 Channel5` |
| 2 | `PC8` | `TIM3_CH3` | `DMA1 Stream7 Channel5` |
| 3 | `PC9` | `TIM3_CH4` | `DMA1 Stream2 Channel5` |

Captura de `Z` / index implementada por interrupcion:

| Eje | Z / Index | Uso previsto |
| --- | --- | --- |
| 0 | `PB9` | `EXTI9`, latched por firmware |
| 1 | `PB10` | `EXTI10`, latched por firmware |
| 2 | `PB11` | `EXTI11`, latched por firmware |
| 3 | `PB12` | `EXTI12`, latched por firmware |

Nota importante:

- `PA15`, `PB3` y `PB4` comparten funciones con JTAG. En esta app se usan para encoder/captura, asi que en practica debes quedarte con `SWD` sobre `PA13/PA14` y no contar con JTAG completo.
- `Z` se expone ya a traves de `Enc_Status` en el OD/PDO existente: bit 5 = nivel actual fisico de la entrada Z; bit 6 = pulso de un ciclo servo cuando hubo un flanco ascendente nuevo desde el ultimo latch. En otras palabras, bit 5 describe el estado instantaneo del pin y bit 6 describe un evento nuevo.

## IO temporales expuestos por PDO

Salidas digitales temporales:

| Bit | Pin |
| --- | --- |
| `Outputs[0]` | `PD0` |
| `Outputs[1]` | `PD1` |
| `Outputs[2]` | `PD2` |
| `Outputs[3]` | `PD3` |
| `Outputs[4]` | `PD4` |
| `Outputs[5]` | `PD5` |
| `Outputs[6]` | `PD6` |
| `Outputs[7]` | `PD7` |
| `Outputs[8]` | `PD8` |
| `Outputs[9]` | `PD9` |
| `Outputs[10]` | `PD10` |
| `Outputs[11]` | `PD11` |
| `Outputs[12]` | `PE0` |
| `Outputs[13]` | `PE1` |
| `Outputs[14]` | `PE2` |
| `Outputs[15]` | `PE3` |

Entradas digitales temporales:

| Bit | Pin |
| --- | --- |
| `Inputs[0]` | `PE4` |
| `Inputs[1]` | `PE5` |
| `Inputs[2]` | `PE6` |
| `Inputs[3]` | `PE7` |
| `Inputs[4]` | `PA8` |
| `Inputs[5]` | `PA9` |
| `Inputs[6]` | `PA10` |
| `Inputs[7]` | `PA11` |
| `Inputs[8]` | `PA12` |
| `Inputs[9]` | `PB0` |
| `Inputs[10]` | `PB1` |
| `Inputs[11]` | `PB2` |
| `Inputs[12]` | `PB6` |
| `Inputs[13]` | `PB7` |
| `Inputs[14]` | `PB8` |
| `Inputs[15]` | `PC4` |

## Pines libres

Los siguientes pines no aparecen reclamados por esta app:

- `PA2`, `PA3`
- `PB13`, `PB14`, `PB15`
- `PC1`, `PC5`, `PC10`, `PC11`, `PC12`, `PC13`, `PC14`, `PC15`
- `PD14`, `PD15`

## Reservas y notas de diseno

- `PA13` y `PA14` deben mantenerse para `SWDIO` y `SWCLK`.
- `PH0` y `PH1` conviene reservarlos para `HSE_IN` y `HSE_OUT` en la PCB, aunque el firmware actual arranque con HSI.
- `PE0..PE15` quedan completamente ocupados por IO temporal y PWM/direccion.
- `PD0..PD13` quedan casi completos por IO temporal y encoder del eje 2.
- `PDI_EMU` debe leerse como bootstrap del AX58100 (`Device emulation`, bit `0x0141.0`), no como salida de estado de EEPROM cargada.
- `EEP_DONE` sigue siendo una salida de estado util para diagnostico, pero no es obligatoria para el firmware actual con EEPROM fisica.
- Esta variante con EEPROM fisica no asigna pines STM32 a la carga de EEPROM del AX58100. Si en el futuro migras a una solucion asistida por MCU, habra que reservar al menos `SCL`, `SDA` y, si quieres diagnostico de arranque, `EEP_DONE`.