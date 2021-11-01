
# 03: Exploring Firmware Bugs 

For this section we will first introduce the firmware a little bit.

The firmware listens over UART for a connection, which it treats as a 
serial console. You can `login` to the device and if successful, the 
firmware will accept a base64-encoded `DER` certificate to sign 
further firmware, and it will echo back certain information it has 
found from the certificate.

As we will explain on the slides, certificates are encoded as 
ASN.1 using Distinguished Encoding Rules (DER). 

Here we show you how to generate a certificate and view it in DER:

<DER example>

We have taken a perfectly working ASN.1 library and added some 
bugs for inclusion in your demo firmware today! 
What we want to do is this:

 1. Automatically log in to the firmware.
 2. Generate a certificate.
 3. Base64-encode it, and send it to the firmware to see what 
    happens.
 4. See if the firmware crashes.

You should be able to tackle #1 from the previous section.

In `gencert.py` we have provided some code for you to generate 
random certificates. What happens if you tweak the function parameters? 
We ask you to explore this briefly to see the different outputs that 
you recover.

Next, we would like you to modify `extdev.py` to automatically 
send your generated certificates to the firmware. Can you make the 
firmware crash? What does that look like? 
