# Hands-on 03: Exploring Firmware Bugs 

After setting up HALucinator and exploring it, we're going to speed things up a
bit and will develop a naive fuzzer for our target device. For this section we
will first introduce the firmware functionality a little bit more. The analyst
would usually do this through static binary analysis (IDA) or by probing the
functionality as we did in the previous step.

The firmware listens over UART for a connection, which it treats as a 
serial console. You can `login` to the device and, if successful, the 
firmware will accept a base64-encoded `DER` certificate to sign 
further firmware, and it will echo back certain information it has 
found from the certificate.

Certificates are encoded as [ASN.1](https://en.wikipedia.org/wiki/ASN.1) encoded
as using [Distinguished Encoding Rules (DER)](https://en.wikipedia.org/wiki/X.690#DER_encoding).

Here we show you how to generate a certificate and view it in DER:

```
<DER example>
```

Our tutorial firmware runs a perfectly working ASN.1 library that unfortunately
has some  bugs. It is up to us to find these bugs now. What we want to do is
this:

 1. Automatically log in to the firmware.
 2. Generate a certificate.
 3. Base64-encode it, and send it to the firmware to see what 
    happens.
 4. See if the firmware crashes.

You can reuse #1 from the previous section (leveraging your automatic login
functionality).

In `gencert.py` we have provided some code for you to generate 
random certificates. What happens if you tweak the function parameters? 
We ask you to explore this briefly to see the different outputs that 
you recover.

Next, we would like you to modify `extdev.py` to automatically 
send your generated certificates to the firmware. Can you make the 
firmware crash? What does that look like?

We suggest that you implement a testing loop that walks through the four steps
above and, with a timeout checks if the firmware still reacts accordingly. If
you have discovered a crash, you can report the current input to the analyst and
report any discovered bugs. 
