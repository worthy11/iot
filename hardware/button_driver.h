#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#define CFG_BUTTON_GPIO 4
#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_LONG_PRESS_MS 3000 // 3 seconds for long press to clear WiFi credentials

void button_driver_init(void);

#endif /* BUTTON_DRIVER_H */
