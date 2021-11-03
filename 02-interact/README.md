# Hands-on 02: Interacting with HALucinator

This section picks up where the last section ended. Instead of just interacting
with the firmware through basic commands, we will create a simple shell,
allowing the analyst to play with the firmware and explore it further.

After setting up a firmware, such basic interaction is the first step an analyst
would do to get an overview of what features the firmware provides. The analyst
will hook several devices to probe different I/O capabilities and then play with
the discovered functionalities.

Here, luckily, we're only interacting through the UART so only need to expand
that interaction.

## Setup / Initialization

We don't need to go through the initialization of HALucinator and the device
again. Simply run the local `copy.sh` script to copy over files we worked on
during the first part of the tutorial:

```
./copy.sh
```

To copy your setup from the first part into this part. Note that you can run
`./copy-cheat.sh` if you want to bootstrap with the provided solution of the
first part.


## Building a custom shell to interact with the firmware

Our goal for this part of the tutorial is to build I/O capabilities and enable
the analyst to play with the emulated firmware.

For this, we need to build a small shell that can fire off events to trigger the
main functions of the firmware. We need some parsing for input from the command
line that is then packaged up and sent to the emulated firmware.

We already learned about the `UartPeripheral` object and how to define callback
functions that fire when the device returns data. This allows us to communicate
bidirectionally: `send_line` to send data to the device and adjusting the
callback function to handle received data.

In Python, we can read input lines (and print them to stdout) as follows:

```py
for line in sys.stdin:
    print(line)
```

As a first step, modify `extdev.py` to simply send the commands read from stdin
to the firmware and display anything the firmware returns.

Does the `message_received` function need any improvement? What 
about the `UartPeripheral` class itself?

Your friendly co-worker told you that the firmware accepts the following
commands:
 
 * version
 * status
 * login
 * cert

Try them. How do they work with your tool?

Next, this is a highly secure IoT device. The username and password 
are `admin` and `123456`. Can you automatically send these to 
the device?
