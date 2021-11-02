set architecture armv7e-m
set can-use-hw-watchpoints 0
set remotetimeout 2000
file build/stm32-f411re-app.elf
target extended-remote localhost:3333
monitor reset
