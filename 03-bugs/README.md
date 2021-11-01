
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


