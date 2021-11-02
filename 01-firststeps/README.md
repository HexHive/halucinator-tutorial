# 01: First Steps with HALucinator

In this part of the tutorial we will run, for the first time, a firmware under
HALucinator.  In this folder, you will find the following files:

 - config.yaml
 - mem.yaml
 - run.sh
 - stm32-f411re-uart.bin
 - stm32-f411re-uart.elf

Our first step is to inspect the `mem.yaml` file. This file is responsible for
the overall memory mapping and configures the address space of our emulator.
Each region defined in this file is mapped into the emulator address space.
It should look like this:

```
memories:
  alias: {base_addr: 0x0, file: stm32-firmware-here,
    permissions: r-x, size: 0x800000}
  flash: {base_addr: 0x8000000, file: stm32-firmware-here,
    permissions: r-x, size: 0x200000}
  ram: {base_addr: 0x20000000, size: 0x51000}
peripherals:
  logger: {base_addr: 0x40000000, emulate: GenericPeripheral, permissions: rw-, size: 0x20000000}
```

We need to tell HALucinator what firmware to use and map it to the correct
region. For stm32, the target address is `0x8000000`. To do this, replace
`stm32-firmware-here` with the name of the firmware file.

This file MUST be the flat binary format that would be written to flash. In this
case, it is the BIN file and not the ELF file, so replace this with
`stm32-f411re-uart.bin`.

Next, open `config.yaml`. This file contains the main configuration of
HALucinator including the different interception points. An interception point
allows the analyst to intercept execution from QEMU and redirect it to a custom
implementation. You will see that some default interception points have already
been added for you. However, not all are present. Especially the UART peripheral
has not yet been configured. To do so, we need to define a set of UART-specific
interception handlers and interception point in the firmware and connect those
with HALucinator. Copy and paste the following UART-specific interception points
into the file and save it:

```
- class: halucinator.bp_handlers.stm32f4.stm32f4_uart.STM32F4UART 
  function: HAL_UART_Init
- class: halucinator.bp_handlers.stm32f4.stm32f4_uart.STM32F4UART 
  function: HAL_UART_GetState
- class: halucinator.bp_handlers.stm32f4.stm32f4_uart.STM32F4UART
  function: HAL_UART_Transmit
- class: halucinator.bp_handlers.stm32f4.stm32f4_uart.STM32F4UART
  function: HAL_UART_Receive
```

Last of all: we need to tell HALucinator about possible firmware addresses. 
The file `addrs.yaml` contains a mapping between memory addresses and symbols,
allowing the other files to refer to symbols instead of the (hardly human
readable) raw addresses. Luckily, we can often auto-generate this file, e.g., by
extracting the information from the symbol table in an ELF file or by recovering
this through a binary analysis.

Since we have an ELF file, which contains these locations, we can extract 
this out for you. We have provided a handy tool with the HALucinator 
source code to do this. Copy and paste the following into your terminal: 

```
halucinator-symtool -b stm32-f411re-uart.elf -o addrs.yaml
```

This will create a file `addrs.yaml`. Open it in an editor to see all the 
possible firmware functions we have identified.

Finally, we are in a position to run the firmware! Let's do that using the 
run script as follows:

```
./run.sh
```

Type, or copy-paste, this into your terminal. The firmware will run. Let it 
run for a little bit and then holding control, press the C key to stop it.

The run.sh file contains the following command:

```
halucinator-rehost \
  -c=./config.yaml \
  -a=./addrs.yaml \
  -m=./mem.yaml \
  --log_blocks -n stm32f411re_uart
```

So all the files you have modified, you should now see being used.

Next, we will go a little bit further and communicate with the firmware! We 
will send events to the device! We therefore connect a virtual device at the
other end of the UART. This virtual device is implemented in python.

To do this, have a look at `extdev.py` script in this folder. You will see a 
main function and handler function like this:

```py
def message_received(message):
    # TODO: do something when we receive a message.
    pass

def main():
    logging.basicConfig()

    halzmq = HALucinatorZMQConn(5556, 5555) 
    uart = UartPeripheral(halzmq, received_callback=message_received)

    halzmq.start()

    try:
        # TODO: send some commands.
        pass
    except KeyboardInterrupt:
        pass
    log.info("Shutting Down")
    halzmq.shutdown()
```

We want to modify this to send some data to HALucinator. The UartPeripheral object 
has methods that can help us here. Here are the lines you might consider:


```py
uart.send_line(huart2, 'MY_COMMAND\r\n')
```

You can also print the output received from HALucinator in the terminal by modifying 
`message_received` with a simple:

```py
print(message)
```

Add some lines to your python file.

To see this in action, you first want to launch the firmware using `run.sh`. 
Then, open a separate terminal and type 

```
python3 extdev.py
```


To run the external device. What output do you see?
