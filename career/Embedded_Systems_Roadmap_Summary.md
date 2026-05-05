# Complete Embedded Systems Roadmap - Summary

> Source: "COMPLETE EMBEDDED SYSTEMS Roadmap - What Arduino Won't Teach You"

## Core Thesis

Arduino is a hobbyist platform and **not representative of real embedded systems engineering** in industry. This roadmap covers the six key categories of knowledge required to work professionally in embedded systems.

---

## 1. General Skills & Mindset

- **Big Picture Thinking** - An embedded device is part of a larger ecosystem (servers, phones, other devices via Wi-Fi, cellular, BLE, etc.). You must consider the whole system from cloud integration down to individual ICs on the PCB.
- **Debugging** - Problems are cryptic and low-level. Learn to visualize how code travels through the CPU, how data flows, and how electronic signaling works. Debug tools exist but are less advanced than desktop OS tooling.

---

## 2. Platform

### Programming Language

- **C is still king** in embedded. Rust efforts exist but lack proper manufacturer support.
- **Embedded C ≠ Systems C** - No OS abstractions; your code is the only thing running. You have full control over the CPU including hardware interrupts.
- Key fundamentals: pointers, structs, `uint` types, **bitmasks** (bit-level manipulation is critical).

### Microcontrollers

| Manufacturer | Series | Notes |
|---|---|---|
| Atmel/Microchip | ATmega, SAM, PIC | Arduino Uno uses ATmega |
| Nordic Semiconductor | nRF52 | Focused on BLE |
| STMicroelectronics | STM32 | Extremely popular, great tooling (CubeMX) |
| Texas Instruments | MSP | Long-standing series |

Choose your MCU based on the hardware peripherals your application needs.

### Bare Metal vs. RTOS

| | Bare Metal | RTOS |
|---|---|---|
| **Structure** | Cyclic executive (`while(1)` loop) | Kernel + scheduler + tasks |
| **Driven by** | Interrupts & polling | Kernel scheduler |
| **Concurrency** | None (sequential) | Task-based concurrency (not parallelism) |
| **Best for** | Simple applications | Complex, multi-concern applications |
| **Primitives** | HAL, MMIO, peripherals | Queues, events, locks, software timers, blocking/non-blocking |

**Key insight:** Using an RTOS for a simple app adds unnecessary complexity; using bare metal for a complex app becomes unmanageable. Choose wisely.

### Popular RTOS Options

- **FreeRTOS** - Simple, flexible, easy to get running. Provides kernel, heap, scheduler. No drivers included.
- **Zephyr** - Complex ecosystem backed by Linux Foundation. Provides drivers, networking stack, device tree configuration. Amazing if your hardware is supported; nightmarish if not.

### Hardware Abstraction Library (HAL)

Adds a layer between application code and hardware registers, making code more portable across different MCUs. Quality varies greatly among implementations.

---

## 3. Hardware Peripherals

Peripherals are the building blocks for your application. They can be **internal** (on-chip) or **external** (separate ICs on the PCB).

- **GPIO** - Digital input/output, logic levels, used for buttons/LEDs, can trigger interrupts or be polled.
- **Timers** - Count up/down, trigger interrupts/events, drive time-based routines (ms or ns precision).
- **Watchdogs** - Fault tolerance; must be periodically "fed" or the system resets.
- **ADC/DAC** - Bridge between analog real world (temperature, light, sound, magnetism) and digital domain.
- **IMUs** - Accelerometers and magnetometers for motion/orientation sensing.

---

## 4. Communication Protocols

### Wired

| Protocol | Topology | Notes |
|---|---|---|
| **I2C** | One-to-many (master/slave) | Shared bus |
| **SPI** | One-to-many (master/slave) | Faster than I2C |
| **UART** | One-to-one | MCU-to-MCU communication |
| **CAN Bus** | Multi-node | Automotive/industrial/aviation; uses differential signaling for noise immunity |

**Differential signaling** (CAN, USB): Uses two complementary wires; taking the delta cancels noise.

### Wireless / IoT

- **BLE** - Local proximity data transfer, persistent connections, pairing security, audio streaming.
- **MQTT** - Event-driven pub/sub over TCP/IP; requires Wi-Fi/cellular/Ethernet. Great for IoT architectures.
- **Zigbee** - Mesh networking for home automation (e.g., Philips Hue).

---

## 5. Memory

### Memory Management Philosophy

- **Minimize dynamic allocation** - Heap usage is rare in bare metal; prefer stack and globals.
- Full RAM and flash are available at all times; dynamic allocation scenarios are less common in embedded.

### Memory-Mapped IO (MMIO) vs Port-Mapped IO (PMIO)

- **MMIO** - Peripheral registers are mapped into the CPU's main address space. Direct addressing; costs memory addresses.
- **PMIO** - Peripheral address space is separate; requires special CPU instructions. Doesn't waste main memory (e.g., ATmega GPIO).

### Direct Memory Access (DMA)

- DMA controller transfers data from peripherals directly to user-specified buffers **without CPU intervention**.
- Essential for performance on large data transfers (e.g., UART/I2C receive buffers).
- CPU is notified only when transfer is complete.

### Memory Types

| Type | Speed | Volatile? | Used For |
|---|---|---|---|
| **SRAM** | Fast | Yes | Stack, globals, heap (RTOS) |
| **Flash/EEPROM** | Slower | No | Program code, constants, user config |

Memory can be internal or external (e.g., SPI flash chip on PCB).

---

## 6. Electronics Fundamentals

You don't need to design PCBs as a pure embedded software engineer, but you **must understand the basics**.

### Essentials

- **Ohm's Law** (V = IR) and **resistor dividers** - Critical for wiring resistive sensors to ADCs.
- **Capacitors & Inductors** - Decoupling, filtering (low-pass, high-pass, band-pass), button debouncing.
- **Voltage regulation & levels** - Know your chip's voltage limits or it will be destroyed.
- **Data sheets** - Massive documents containing everything about a component. Reading them efficiently is a critical skill.

### Transistors & ICs

- **MOSFETs** (voltage-activated) and **BJTs** (current-activated) are electronic switches and the building blocks of CPUs, RAM, and flash.
- **Integrated Circuits (ICs)** - Complex circuits miniaturized into a single package (MCUs, flash chips, ADCs, etc.).

### Signals & Filtering

- **Quantization** - Converting continuous analog signals to discrete digital steps.
- **Software filters** - Often cheaper than hardware filters; common embedded task.
- **Filter types**: High-pass, low-pass, band-pass for extracting frequencies.

### Schematics

- Primary communication method between hardware and software engineers.
- Must know how to read them to understand pin assignments and peripheral connections.

---

## Recommended Starting Path

1. **Buy an STM32 Nucleo board** (often cheaper than Arduino).
2. Learn **register-level programming** using the data sheet — don't rely on CubeMX code generation initially.
3. Implement basics (LED blink, GPIO) via direct register manipulation.
4. Once comfortable, use HAL/Cube ecosystem to save time.
5. Branch out: connect two boards via UART, add BLE/Wi-Fi peripherals, explore RTOS.
