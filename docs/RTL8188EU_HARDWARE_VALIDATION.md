# RTL8188EU real-hardware validation

Host CI cannot prove a radio works. The existing C driver test uses mocked xHCI register and bulk-transfer functions; it is useful for malformed input and state-machine regressions, but its fake beacons are not over-the-air evidence.

## What this PR adds

- Serial milestone markers for actual observed driver progression: USB binding, firmware, radio initialization, active scan, scan result, authentication, association, WPA2 completion, DHCP start, and a DHCP lease.
- `scripts/test_rtl8188eu_hardware_log.py`, a strict parser for a captured serial log. It fails if a required milestone is absent, out of order, or a known driver failure marker appears.
- A validator self-test for CI. The self-test uses synthetic logs and **does not** test hardware or assert Wi-Fi works.

## Running the hardware acceptance check

1. Use a physical RTL8188EU adapter and a 2.4 GHz access point configured for WPA2-Personal (AES/CCMP). Do not use a guessed PHY profile; the profile must be validated for that exact adapter board and silicon cut.
2. Boot a VantaOS image containing the driver and profile. Capture the complete serial console from boot through connection attempt to a text file, e.g. `rtl8188eu-serial.log`.
3. Trigger a real scan and connection using the available driver control path, with the correct SSID/passphrase. The adapter must receive actual AP management frames; host-mocked tests are not evidence.
4. Run:

   ```sh
   python3 scripts/test_rtl8188eu_hardware_log.py rtl8188eu-serial.log
   ```

5. Inspect the full log. A pass means the expected association/WPA2/DHCP milestones were emitted in order; it is not a blanket Internet pass.

The checker intentionally reports failure if a stage has not been observed. It never treats USB enumeration, firmware upload, a parsed beacon, or a function return as proof of a later stage.

## What constitutes TRUE end-to-end Wi-Fi

A complete physical test must establish all of these with real traffic:

1. Adapter USB transport binds and firmware becomes ready.
2. Trusted board-specific PHY profile executes, RF calibration completes, and the driver explicitly enables transmission.
3. An over-the-air scan sees the intended AP and its BSSID/channel.
4. Open System authentication and association responses from that AP succeed.
5. WPA2 four-way handshake completes; keys are installed and protected unicast traffic is accepted.
6. DHCP ACK is accepted and the network interface receives an IPv4 address, netmask, gateway, and DNS server.
7. DNS resolves a known public hostname through the learned DNS server.
8. An outbound TCP connection and application-level request succeed; ideally repeat against a second endpoint and verify reply payload bytes.

The serial checker currently covers steps 1–6 only. Steps 7–8 must be verified separately with real DNS/TCP requests over `wlan0` before advertising Internet support. If the current shell/build has no user-facing way to initiate those requests over the Wi-Fi device, that is a missing test/control path, not a reason to mark the test passed.

## Current limitation

This PR adds evidence collection and validation; it does not conjure missing RF initialization or a test AP. If the board-specific PHY profile is unavailable, the driver should remain fail-closed and the hardware test should fail. No physical adapter is available to this CI runner, so actual radio, WPA2, DHCP, DNS, and Internet behavior remains unverified until someone runs the procedure on hardware.
