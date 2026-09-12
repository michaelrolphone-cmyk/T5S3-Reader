
v1.0.1
- Add screen backlight settings
- Add font download management function
- Resolve the issue of file transmission errors via WIFI

v1.0.2

- Added support for Chinese books. #5
- Optimize the screen refresh method to reduce screen flickering
- Solve the problem of slow page loading when reading Chinese books
- Correct selecting tabs on Settings. #7
- Correct overriding already rendered images when Text Anti-Aliasing is enabled. #8

v1.0.3

- Add the bookmark function to the EPUB file. You can access the bookmarks by using the `Toggle Bookmark` option from the reader's menu. 

- Add the "whether the current page has been bookmarked" status to the status bar of the reading interface.

- Add the `Recent Book` option. Long press to remove a single book. You can also choose to automatically remove it after finishing reading. 

- Add the main page clock, read the page status bar clock, and set it through `Settings -> Reader -> Customise Status Bar`
- Time Zone is now a region → city picker with the full IANA city list (legacy numeric settings migrate automatically).
    - Go to `Settings > System > Time Zone`, choose a region, then a city.
    - Go to `WiFi Networks` and connect to the network once. Let it automatically perform NTP time synchronization and write the RTC data.

If the time is reset after being reinitialized, an operation that does not 
rely on the public network NTP can be performed:
- Connect the device to WiFi, and open the device's web page once using a mobile phone or computer. `http://deviceIP/`, `/files/`, `/settings/`, and `/fonts` are all available.

- Quick Menu Feature #15
- System Chapter Font Footer Fix #16
- System-wide Hide Clock Option #17
- Flip up/down buttons and screen option #18
- Resume Book on Wake Fix + Making Optional #19

v1.0.4

- Open `.md` / `.markdown` files in the TXT reader.
- Render Markdown: strip inline markup, bold ATX headings, treat `#` H1 titles as chapter breaks.
- Markdown chapter picker (Confirm hold), fence-aware page-index cache, sleep-cover path.
- Board image: `firmware/corsspoint_lilygo_t5s3_1.0.4.bin` (T5-4.7-S3 E-Paper Pro, merged flash image).

v1.0.5

- Same Markdown reader as 1.0.4, cut through `.github/workflows/release.yml`.
- Board image: `firmware/corsspoint_lilygo_t5s3_1.0.5.bin` (T5-4.7-S3 E-Paper Pro, merged flash image).

v1.0.6

- OTA latest-release URL points at `michaelrolphone-cmyk/T5S3-Reader`.
- Release assets include `firmware-t5s3-pro.bin` for on-device OTA and SD update.
- Version compare accepts a leading `v` on GitHub tags.
- Board image: `firmware/corsspoint_lilygo_t5s3_1.0.6.bin` (merged USB image). Use `firmware-t5s3-pro.bin` or the `-app` file for SD/OTA.

v1.0.7

- Product name on boot/power-off splash is Manifold; default logo is Eye of Horus.
- GitHub OTA uses pinned USERTrust ECC + ISRG Root X1/X2 instead of the missing Arduino cert bundle.
- Feed the task WDT while validating large SD firmware images.
- Settings / boot splash show `1.0.7` on `gh_release` builds.
- USB: `firmware/corsspoint_lilygo_t5s3_1.0.7.bin`. SD/OTA: `firmware-t5s3-pro.bin`.

v1.0.8

- Home menu Ask screen: type a question, call LLM7.io (`fast`, anonymous `Bearer unused`).
- LLM TLS pinned to GTS Root R4 for `api.llm7.io`.
- USB: `firmware/corsspoint_lilygo_t5s3_1.0.8.bin`. SD/OTA: `firmware-t5s3-pro.bin`.
