# Framework Issues

This document tracks known framework and application-integration bugs that need investigation and resolution.

## 1. Desk Clock time is off-center in 12-hour mode

**Area:** Desk Clock / UI layout  
**Status:** Open

### Problem
When the Desk Clock is configured for 12-hour time, the rendered time is visibly off-center.

### Expected behavior
The complete 12-hour time display, including any AM/PM indicator where applicable, should be measured and centered within the available clock display area.

### Actual behavior
The time is rendered with an incorrect horizontal center in 12-hour mode.

---

## 2. App navigation-bar Back button does not go back

**Area:** Framework navigation / ELF apps  
**Status:** Open

### Problem
The framework-provided **Back** button shown in the navigation bar while running apps does not perform back navigation.

### Expected behavior
Pressing **Back** should return to the previous view/activity according to the framework navigation stack, and at the root of an app should return to the appropriate launcher/previous framework screen.

### Actual behavior
Pressing the navigation-bar **Back** control does not go back.

---

## 3. Home-screen apps all use the same icon

**Area:** Home screen / app manifests / icons  
**Status:** Open

### Problem
Apps added to the home screen all render with the same icon rather than the icon assigned to the individual app.

### Expected behavior
A home-screen shortcut should resolve and render the icon defined for that app, consistent with the app launcher and its manifest metadata.

### Actual behavior
Different apps added to the home screen display an identical icon.

---

## 4. App Store bypasses framework navigation controls

**Area:** App Store / framework UI integration  
**Status:** Open

### Problem
The App Store does not use the navigation bar and navigation controls provided by the application framework. The same problem is present in multiple other apps.

### Expected behavior
Apps should use the framework-provided navigation UI/API so navigation behavior, Back handling, layout, and controls remain consistent across applications.

### Actual behavior
The App Store implements its screen without the standard framework navigation bar/controls. Other apps appear to have the same integration problem.

### Scope
Audit framework applications for custom or legacy navigation implementations and migrate them to the common framework navigation controls where appropriate.

---

## 5. App Store cannot scroll past initial app list

**Area:** App Store / scrolling  
**Status:** Open

### Problem
The App Store displays its initial visible list of applications but cannot scroll far enough to access applications beyond that initial list.

### Expected behavior
The application list should be vertically scrollable through the complete set of available applications, with the scrollable viewport correctly accounting for headers/navigation controls.

### Actual behavior
Apps beyond the initially displayed list cannot be reached by scrolling.

---

## 6. USB Serial icon renders as a blank square

**Area:** USB Serial / Font Awesome / app manifest  
**Status:** Open

### Problem
The USB Serial application uses an icon glyph that renders as a blank square instead of a USB, terminal, serial, or console symbol.

### Expected behavior
USB Serial should use a Font Awesome glyph that exists in the font set shipped/supported by the device and renders reliably in the app launcher and other framework surfaces.

### Actual behavior
The selected icon resolves to the missing-glyph square.

### Suggested direction
Use a supported USB, terminal, console, plug, or serial-related Font Awesome icon and verify the manifest/icon lookup against the actual SD-card font set.

---

## 7. Device does not sleep after "Time to sleep" while an app is open

**Area:** Power management / application lifecycle  
**Status:** Open

### Problem
When an application is open, the device does not enter sleep after the configured **Time to sleep** interval expires.

### Expected behavior
The framework-level inactivity/sleep timer should continue to operate while an app is running unless the app has explicitly acquired a valid power/sleep lock. When the configured timeout expires with no qualifying activity or lock, the device should sleep.

### Actual behavior
Keeping an app open prevents the configured sleep timeout from putting the device to sleep.

### Investigation
Check whether app execution, activity lifecycle handling, periodic redraws, input polling, or leaked power locks continuously reset or suppress the framework sleep timer.

---

## 8. USB Serial baud rate is hard-coded

**Area:** USB Serial / serial configuration  
**Status:** Open

### Problem
The USB Serial app uses a hard-coded baud rate. Devices configured for a different serial speed therefore cannot be connected correctly.

### Expected behavior
The user should be able to select the serial baud rate. The selected value should be applied when the connection is opened and should preferably persist for subsequent sessions.

Common baud rates should include at least 9600, 19200, 38400, 57600, 115200, 230400, 460800, and 921600 where supported by the hardware/driver.

### Actual behavior
The baud rate is fixed in the application and cannot be changed from the UI.
