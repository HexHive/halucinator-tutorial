# Hands-on 03: Exploring Firmware Bugs 

[Back](../README.html)

After setting up HALucinator and exploring it, we're going to speed things up a
bit and will develop a naive fuzzer for our target device. For this section we
will first introduce the firmware functionality a little bit more. The analyst
would usually do this through static binary analysis (IDA) or by probing the
functionality as we did in the previous step.

For this section we will first introduce the firmware a little bit.

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

 1. Automatically log in to the firmware (part 2)
 2. Generate a certificate (gencert.py)
 3. Base64-encode it, and send it to the firmware to see what 
    happens (gencert.py)
 4. See if the firmware crashes.

The first part is the work we have done in the previous part.
In `gencert.py` we have provided some code for you to generate 
random certificates in the right format. Here is the code:

```
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.serialization import Encoding, PrivateFormat, NoEncryption
from OpenSSL import crypto, SSL
import base64
import string

"""
This function will provide you a random string that is 'length' characters long.
You can use it to help you explore the firmware for bugs.
"""
def random_string(length):
    return ''.join(random.SystemRandom().choice(string.ascii_uppercase + string.digits) for _ in range(length))


"""
This function takes some of the usual properties you would expect 
of a certificate. 
It returns a tuple: DER-encoded certificate, DER-encoded private key.

Note: probably this should be done with cryptography and not 
pyopenssl.
"""
def selfsigned_certificate(emailAddress="emailAddress",
             commonName="commonName",
             countryName="NT",
             localityName="localityName",
             stateOrProvinceName="stateOrProvinceName",
             organizationName="organizationName",
             organizationUnitName="organizationUnitName",
             serialNumber=0,
             validityStartInSeconds=0,
             validityEndInSeconds=10*365*24*60*60):

    # For small keys and fast generation, lets do this 
    # using P-256:
    q = ec.generate_private_key(ec.SECP256R1)
    q_der = q.private_bytes(encoding=Encoding.DER, format=PrivateFormat.TraditionalOpenSSL, encryption_algorithm=NoEncryption())
    k = crypto.load_privatekey(crypto.FILETYPE_ASN1, q_der)

    cert = crypto.X509()
    cert.get_subject().C = countryName
    cert.get_subject().ST = stateOrProvinceName
    cert.get_subject().L = localityName
    cert.get_subject().O = organizationName
    cert.get_subject().OU = organizationUnitName
    cert.get_subject().CN = commonName
    cert.get_subject().emailAddress = emailAddress
    cert.set_serial_number(serialNumber)
    cert.gmtime_adj_notBefore(0)
    cert.gmtime_adj_notAfter(validityEndInSeconds)
    cert.set_issuer(cert.get_subject())
    cert.set_pubkey(k)
    cert.sign(k, 'sha512')

    return (crypto.dump_certificate(crypto.FILETYPE_ASN1, cert), q_der)
```

Add this to your `extdev.py` in part 3. Now, you can use this code like this:


```
certificate, privkey = selfsigned_certificate()
b64_cert = base64.b64encode(certificate).decode('utf-8')
```

Then `b64_cert` contains a base64-encoded version of the certificate.

What does this look like when we decompile it? The library we have used would 
print the raw encoding like this:

```
* ASN1_TAG_SEQUENCE
  * ASN1_TAG_SEQUENCE
    * ASN1_CLASS_CONTEXT:0
      - ASN1_TAG_INT 2
    - ASN1_TAG_INT <INVALID>
    * ASN1_TAG_SEQUENCE
      - ASN1_TAG_OID 1.2.840.113549.1.1.11
      - ASN1_TAG_NULL
    * ASN1_TAG_SEQUENCE
      * ASN1_TAG_SET
        * ASN1_TAG_SEQUENCE
          - ASN1_TAG_OID 2.5.4.6
          - ASN1_TAG_PRINTABLESTRING 'US'
      * ASN1_TAG_SET
        * ASN1_TAG_SEQUENCE
          - ASN1_TAG_OID 2.5.4.10
          - ASN1_TAG_PRINTABLESTRING 'Entrust, Inc.'
      * ASN1_TAG_SET
        * ASN1_TAG_SEQUENCE
          - ASN1_TAG_OID 2.5.4.11
          - ASN1_TAG_PRINTABLESTRING 'See www.entrust.net/legal-terms'
      * ASN1_TAG_SET
        * ASN1_TAG_SEQUENCE
          - ASN1_TAG_OID 2.5.4.11
          - ASN1_TAG_PRINTABLESTRING '(c) 2012 Entrust, Inc. - for authorized use only'
      * ASN1_TAG_SET
        * ASN1_TAG_SEQUENCE
          - ASN1_TAG_OID 2.5.4.3
          - ASN1_TAG_PRINTABLESTRING 'Entrust Certification Authority - L1K'
    * ASN1_TAG_SEQUENCE
      - ASN1_TAG_UTCTIME 2021-10-13 13:49:14 UTC
      - ASN1_TAG_UTCTIME 2022-11-05 13:49:14 UTC
    * ASN1_TAG_SEQUENCE
      * ASN1_TAG_SET
        * ASN1_TAG_SEQUENCE
          - ASN1_TAG_OID 2.5.4.6
          - ASN1_TAG_PRINTABLESTRING 'US'
      * ASN1_TAG_SET
        * ASN1_TAG_SEQUENCE
          - ASN1_TAG_OID 2.5.4.8
          - ASN1_TAG_PRINTABLESTRING 'Virginia'
      * ASN1_TAG_SET
        * ASN1_TAG_SEQUENCE
          - ASN1_TAG_OID 2.5.4.7
          - ASN1_TAG_PRINTABLESTRING 'Arlington'
      * ASN1_TAG_SET
        * ASN1_TAG_SEQUENCE
          - ASN1_TAG_OID 2.5.4.10
          - ASN1_TAG_PRINTABLESTRING 'Office of Naval Research'
      * ASN1_TAG_SET
        * ASN1_TAG_SEQUENCE
          - ASN1_TAG_OID 2.5.4.3
          - ASN1_TAG_PRINTABLESTRING 'www.onr.navy.mil'
```

This binary format can be simplified by user agents such as browsers. Something more human would look 
like this:

```
---
Version: 2, Algo: 4
Valid from: hT, to: hT
Issuer:
  Country:             US
  Organization:        Entrust, Inc.
  Organizational Unit: See www.entrust.net/legal-terms
  Organizational Unit: (c) 2012 Entrust, Inc. - for authorized use only
  Common Name:         Entrust Certification Authority - L1K
Subject:
  Country:             US
  State or Province:   Virginia
  Locality:            Arlington
  Organization:        Office of Naval Research
  Common Name:         www.onr.navy.mil
Public key: 1
```

In our demo firmware, we want to upload a certificate like this that will sign future firmware 
updates!

However, we have added bugs to the firmware parser for you to find.

So, you will need to modify `extdev.py`. Now you are logged into the device, you can 
also run the `cert` command. This responds with a prompt for the base64 encoded `DER`, 
which you can get from the functions we have provided.

The function to generate certificates takes parameters as well. What happens if you vary 
these? For example you might try changing the common name and extend it with
random data:

```
    commonName=random_string(12)
```

Each time the firmware crashes you may need to restart HALucinator (the terminal
running `./run.sh` --- press Ctrl-C and restart) and try again. These bugs are
common programming bugs like missing length checks and format string issues, so
give those a try!

What does the output look like in HALucinator?
What about if you send C-style format strings?

We suggest that you implement a testing loop that walks through the four steps
above and, with a timeout checks if the firmware still reacts accordingly. If
you have discovered a crash, you can report the current input to the analyst and
report any discovered bugs. 
