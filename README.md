# Smart door

A card-access door controller on an STM32 Nucleo-F303RE, built as a team project for UNSW's DESN2000 on the coaST education board.

- **Entry:** an LDR on each side of the door detects someone approaching. From the outside, the PN532 NFC reader asks for a MIFARE card. Access needs both an enrolled UID and a matching per-card secret on the card.
- **Door:** a stepper motor swings the door 90°. It waits until its path is clear, pauses mid-swing if someone steps in, and re-opens if a sensor trips while closing.
- **Security:** tailgating detection, an alarm after five rejected cards, and an admin fob for clearing alarms and enrolling cards.
- **Management:** a keypad and LCD menu for scheduled unlock windows, the clock, themes and card management.

Write-up: [bastianmarschner.au/work/smart-door](https://www.bastianmarschner.au/work/smart-door)

## Layout

```
smart_door_cubeide/
  Src/, Inc/      firmware (passage, auth, idstore, ldr, motor, nfc, mgmt, feedback, ...)
  *.ioc           STM32CubeMX pin and peripheral configuration
```

The firmware is a bare-metal cooperative superloop with no RTOS. Each module has an `init()` and a non-blocking `task()`, and only `hal_wrapper.c` touches hardware.

## Building

1. Open `smart_door_cubeide` in STM32CubeIDE.
2. Build the Debug configuration.
3. Flash it to the Nucleo over ST-LINK.

## Note

The card secret and MIFARE key in `auth.c` are demo values for a coursework project, not real security.
