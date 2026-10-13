`sample.rar` is decoded from libarchive's
`libarchive/test/test_read_format_rar.rar.uu`, previously embedded in the arc
verification script. It contains two files, directories and a symbolic link.
It is kept locally so tests do not download fixtures.

Other archives are generated per test with Python's standard library. Corrupt
ZIP headers, CRC damage, sparse TAR records and ODC CPIO records are deliberately
constructed in their corresponding tests.

`password-zipcrypto.zip` was generated with Info-ZIP `zip -P secret`.
It contains `value` with the bytes `zipcrypto payload\n` and exercises actual
traditional ZIP decryption independently of arc's writer. AES test inputs and
independent output checks use pyzipper, a development-only dependency.
