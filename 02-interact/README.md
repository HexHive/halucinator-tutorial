
# 02: Interacting with HALucinator

In this section we will ask you to fill out your demo to be able to 
interact with the firmware in an arbitrary way, as a shell.

First, in this folder you will find a script `copy.sh`. Run it with 

```
./copy.sh
```

To copy your setup from the first part into this part.

Now, we have seen that we have a `UartPeripheral` object that can be 
configured to have a callback function, and also has a `send_line` 
method. We want to build a shell out of this, so let's consider 
how we might do it.

Firstly, we can read all lines being sent to us in python using 

```py
for line in sys.stdin:
    print(line)
```

This example will simply echo back what is input. Can you modify 
`extdev.py` in order to send the input to the external device.

Does the `message_received` function need any improvement? What 
about the UartPeripheral class itself?

Next, we'll remind you of the commands the device accepts:
 
 * version
 * status
 * login
 * cert

Try them. How do they work with your tool?

Next, this is a highly secure IoT device. The username and password 
are `admin` and `123456`. Can you automatically send these to 
the device?
