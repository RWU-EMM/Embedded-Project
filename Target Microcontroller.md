### 2.X Target Microcontroller

The Arduino Nano 33 IoT was selected as the device under test (DUT) for evaluating the real-time digital filtering implementation. The firmware was developed using the Arduino IDE with a modular C-style architecture. Hardware-specific functions were separated into GPIO, UART, ADC, DAC, and timer modules, while the application layer implements the acquisition, filtering, and output sequence.

The Nano 33 IoT provides the required interfaces for the test setup: one digital input for the command signal (`CMD`), one digital output for the ready signal (`RDY`), the hardware UART (`Serial1`), one ADC input, one DAC output, and a hardware timer. The implemented configuration uses D2 for `CMD`, D3 for `RDY`, A1 for the ADC input, A0/DAC0 for the DAC output, and D0/D1 for the hardware UART. The UART operates at 115200 baud with an 8-bit data format, one stop bit, and no parity.

The filtering system operates at a sampling frequency of 5 kHz, corresponding to a sampling period of 200 µs. A hardware timer based on the SAMD21 TC3 peripheral generates the periodic timing event. Rather than executing the complete signal-processing chain inside the interrupt service routine (ISR), the ISR only generates a timer event. The main application loop detects this event through `timer_take_event()` and executes one complete acquisition-processing-output cycle. This minimizes ISR execution time while retaining a deterministic 200 µs sampling trigger.

For each sampling event, the ADC is first sampled with 12-bit resolution. The resulting unsigned ADC value in the range 0–4095 is converted into the signed representation used by the digital-filter implementation by subtracting the midpoint of 2048:

\[
x[n] = ADC[n] - 2048
\]

This produces a nominal signed input range of approximately −2048 to +2047. The centred sample is then passed to the digital-filter processing function `df_process()`. The filter structure and coefficients are initialized using `df_init()`. The implementation supports IIR, FIR, second-order-section (SOS), and lattice-type filters in both floating-point and integer representations. The default configuration uses the floating-point IIR implementation with direct-form I and rounded quantization settings. 
The default coefficient set corresponds to a ninth-order Bessel low-pass filter with a 555 Hz cutoff frequency and a 5 kHz sampling frequency. The coefficient bank is stored locally in non-volatile program memory as constant data and is selected according to the active filter type. The integer coefficient sets retain the scaling used by the original filter implementation.

After filtering, the signed filter output is converted back to the DAC domain. Since the DAC uses a 10-bit output range, the signed filter result is scaled from the 12-bit signed signal domain to 0–1023 while preserving the signal midpoint. The DAC output is therefore generated according to

\[
y_{\mathrm{DAC}} =
\frac{y[n]\cdot1023}{4095}+512
\]

with the resulting value constrained to the valid 10-bit DAC range.

The command interface is controlled by the `CMD` GPIO. The signal is active-low. During normal operation, `CMD` remains high, `RDY` is high, and the timer-driven acquisition/filtering/DAC process runs continuously. When `CMD` becomes low, the firmware disables the timer and drives `RDY` low, preventing further sampling events from being processed while the UART command sequence is handled.

UART commands are processed in the main loop rather than in the timer ISR. Received bytes are accumulated into a line buffer and commands are executed after a newline delimiter. The implementation retains the filter-selection protocol of the original ESP32 implementation, including commands such as `fmode:iirf`, `fmode:iiri`, `fmode:firf`, `fmode:sosf`, and the corresponding filter types.

The firmware also supports runtime coefficient transfer using the `scoeff:<filter>` command. When coefficient loading is requested, the timer is stopped and subsequent UART lines are interpreted as coefficient records. The received coefficients are parsed into dedicated coefficient structures, validated, and then applied by reinitializing the digital-filter state. This permits the same filtering implementation to be evaluated with different coefficient sets without recompiling the firmware. 
Once UART processing is complete and the command input returns high, the firmware exits UART mode, drives `RDY` high, and restarts the sampling timer. Consequently, the DUT has two mutually exclusive operating states:

\[
\boxed{\text{Sampling mode: CMD=HIGH,\ RDY=HIGH}}
\]

\[
\boxed{\text{UART/configuration mode: CMD=LOW,\ RDY=LOW}}
\]

The resulting real-time signal path implemented on the Nano 33 IoT is therefore:

\[
\boxed{
ADC
\rightarrow
\text{12-bit centering}
\rightarrow
\text{Digital Filter}
\rightarrow
\text{12-to-10-bit scaling}
\rightarrow
DAC
}
\]

with the complete processing cycle released at a nominal 5 kHz sampling rate. The timer interrupt is restricted to event generation, while ADC acquisition, digital filtering, and DAC updating are executed by the application layer.