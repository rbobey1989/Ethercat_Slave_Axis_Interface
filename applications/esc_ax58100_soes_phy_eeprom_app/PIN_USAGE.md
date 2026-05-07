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
| `PA` | `PA0`, `PA1`, `PA2`, `PA3`, `PA4`, `PA5`, `PA6`, `PA7`, `PA15` | `PA13`, `PA14` | `PA8`, `PA9`, `PA10`, `PA11`, `PA12` |
| `PB` | `PB3`, `PB4`, `PB5`, `PB9`, `PB10`, `PB11`, `PB12`, `PB13`, `PB14`, `PB15` | none | `PB0`, `PB1`, `PB2`, `PB6`, `PB7`, `PB8` |
| `PC` | `PC0`, `PC1`, `PC2`, `PC3`, `PC5`, `PC6`, `PC7`, `PC8`, `PC9`, `PC10`, `PC11`, `PC12` | none | `PC4`, `PC13`, `PC14`, `PC15` |
| `PD` | `PD0`, `PD1`, `PD2`, `PD3`, `PD4`, `PD5`, `PD6`, `PD7`, `PD8`, `PD9`, `PD10`, `PD11`, `PD12`, `PD13`, `PD14`, `PD15` | none | none |
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

- `PA15`, `PB3` y `PB4` comparten funciones con `SWJ/JTAG`, pero eso no impide usar depuracion/programacion normal por `SWD` mientras se reserven `PA13` (`SWDIO`) y `PA14` (`SWCLK`). La implicacion real es otra: si esos tres pines se usan para encoder/captura, renuncias a `JTAG` completo y tambien a `SWO`/trace en `PB3`.
- En otras palabras: para una placa que solo vaya a usar `SWD`, reutilizar `PA15/PB3/PB4` es tecnicamente viable; se vuelve una mala eleccion solo si quieres mantener opciones de `JTAG`, `SWO`, boundary-scan, o una zona de debug mas limpia para futuras revisiones.
- `Z` se expone ya a traves de `Enc_Status` en el OD/PDO existente: bit 5 = nivel actual fisico de la entrada Z; bit 6 = pulso de un ciclo servo cuando hubo un flanco ascendente nuevo desde el ultimo latch. En otras palabras, bit 5 describe el estado instantaneo del pin y bit 6 describe un evento nuevo.

## Estado actual del proyecto

El proyecto queda documentado segun el firmware actual, sin proponer aqui un
pinout alternativo. La distribucion vigente que debe considerarse congelada es:

- PWM de los 4 ejes en `TIM1` sobre `PE9`, `PE11`, `PE13`, `PE14`, con
  direccion en `PE8`, `PE10`, `PE12`, `PE15`.
- Posicion en cuadratura en `TIM5`, `TIM2`, `TIM4` y `TIM8` usando
  `PA0/PA1`, `PA15/PB3`, `PD12/PD13`, `PC6/PC7`.
- Captura lenta de velocidad en `TIM3_CH1..CH4` usando `PB4`, `PB5`, `PC8`,
  `PC9`.
- `Z` por `EXTI` en `PB9..PB12`.

Con la decision actual de depurar solo por `SWD` mediante `CMSIS-DAP` o
`ST-Link/V2`, esta asignacion es valida: `PA15`, `PB3` y `PB4` pueden usarse
en la aplicacion siempre que `PA13` y `PA14` sigan reservados para `SWDIO` y
`SWCLK`.

## Estado funcional expuesto por firmware

La superficie funcional visible hoy desde EtherCAT es minimalista y estable:

- `servo.c` publica por eje `Enc_Pos`, `Enc_Vel`, `Enc_Status` y `Pwm_Status`.
- Las salidas del master siguen siendo `Pwm_Cmd`, `Pwm_En`, `Enc_En` y
  `Outputs`.
- No hay objetos extra para posicion exacta de index ni contador de eventos de
  index; la semantica de `Z` queda encapsulada en firmware.

Bits actuales de `Enc_Status`:

| Bit | Mascara | Significado |
| --- | --- | --- |
| 0 | `1U << 0` | Encoder habilitado |
| 1 | `1U << 1` | Captura lenta valida |
| 2 | `1U << 2` | Captura lenta fresca en este latch |
| 3 | `1U << 3` | Velocidad fusionada basada en fuente rapida |
| 4 | `1U << 4` | Velocidad fusionada basada en fuente lenta |
| 5 | `1U << 5` | Nivel actual de `Z` |
| 6 | `1U << 6` | Pulso de un ciclo servo por nuevo evento de `Z` |

Resumen practico del estado actual:

- La unica redistribucion de GPIO temporales respecto al reparto anterior esta
  en `src/io.c`, que movio las entradas temporales a `PA2/PA3`, `PB13..PB15`,
  `PC1`, `PC5`, `PC10..PC12`, `PD14`, `PD15`.
- El bloque encoder mantiene la combinacion actual de contador en cuadratura,
  captura lenta por `TIM3` y latch de `Z` por `EXTI`.
- El OD/PDO activo sigue siendo el minimo necesario para LinuxCNC: posicion,
  velocidad, entradas, `Enc_Status`, `Pwm_Status`, comandos y enables.

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
| `Inputs[4]` | `PA2` |
| `Inputs[5]` | `PA3` |
| `Inputs[6]` | `PB13` |
| `Inputs[7]` | `PB14` |
| `Inputs[8]` | `PB15` |
| `Inputs[9]` | `PC1` |
| `Inputs[10]` | `PC5` |
| `Inputs[11]` | `PC10` |
| `Inputs[12]` | `PC11` |
| `Inputs[13]` | `PC12` |
| `Inputs[14]` | `PD14` |
| `Inputs[15]` | `PD15` |

## Pines libres

Los siguientes pines no aparecen reclamados por esta app:

- `PA8`, `PA9`, `PA10`, `PA11`, `PA12`
- `PB0`, `PB1`, `PB2`, `PB6`, `PB7`, `PB8`
- `PC4`, `PC13`, `PC14`, `PC15`

## Reservas y notas de diseno

- `PA13` y `PA14` deben mantenerse para `SWDIO` y `SWCLK`.
- `PH0` y `PH1` conviene reservarlos para `HSE_IN` y `HSE_OUT` en la PCB, aunque el firmware actual arranque con HSI.
- `PE0..PE15` quedan completamente ocupados por IO temporal y PWM/direccion.
- `PD0..PD15` quedan completamente ocupados por IO temporal, encoder del eje 2 y las entradas temporales `Inputs[14]` y `Inputs[15]`.
- `PDI_EMU` debe leerse como bootstrap del AX58100 (`Device emulation`, bit `0x0141.0`), no como salida de estado de EEPROM cargada.
- `EEP_DONE` sigue siendo una salida de estado util para diagnostico, pero no es obligatoria para el firmware actual con EEPROM fisica.
- Esta variante con EEPROM fisica no asigna pines STM32 a la carga de EEPROM del AX58100. Si en el futuro migras a una solucion asistida por MCU, habra que reservar al menos `SCL`, `SDA` y, si quieres diagnostico de arranque, `EEP_DONE`.