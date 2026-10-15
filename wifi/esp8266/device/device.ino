// UART0 is exclusively the MCU binary link. Never print diagnostics here.
void cameraSetup();
void cameraLoop();
void setup() { cameraSetup(); }
void loop() { cameraLoop(); }
