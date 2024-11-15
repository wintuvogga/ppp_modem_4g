Processor: STM32F407VET
Modem: BG77A (any AT modem)

pin configuration:
MODEM_UART_RXD    ---- PC6 (UART6_TXD)
MODEM_UART_TXD    ---- PC7 (UART6_RXD)
MODEM_POWER_KEY   ---- PD7 
LED               ---- PC13

Features:
  - Uses LWIP
  - PPPOS dialing
  - MQTT client
  - CMSIS RTOS

To build:
  - go to WSL
  - install gcc-arm-none compiler
    `apt install gcc-arm-none-eabi`
  - run `cmake .`
  - then build with `make -j8`

Then you should see a .elf file generated. You can flash it to your board using STLink programmer using STM32CubeProgrammer software.
