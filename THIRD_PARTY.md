# Third-party notices

Original project additions are not assigned a blanket license in this initial publication; the repository owner may choose one later. Public visibility alone does not grant an open-source license.

Files derived from WCH CH32X035 SDK retain their headers. WCH permits use of the software (modified or not) and binaries for microcontrollers manufactured by Nanjing Qinheng Microelectronics. This hardware uses a WCH microcontroller. Do not relicense these files under a blanket project license.

`src/uECC-mini.c` derives from micro-ecc (Kenneth MacKay), BSD-2-Clause, with `crypto_yield()` calls added. The complete notice is included in `licenses/micro-ecc-LICENSE.txt`.

Fetched upstream dependencies are not bundled in Git. Source URLs and exact revisions are in `dependencies.json`; retain the upstream license when redistributing them: micro-ecc BSD-2-Clause, tiny-AES-c Unlicense, crypto-algorithms upstream public-domain declaration, WCH SDK hardware-use restriction above. Review upstream notices rather than assuming all dependencies share the same license.
