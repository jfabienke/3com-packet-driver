# Hardware Programming Reference

## 3Com 3C509 Network Interface Card

# 1\. Introduction

This document provides an extensive reference for programming the **3Com EtherLink III (3C509) ISA Network Interface Card (NIC)**. It covers detailed descriptions of the hardware, register operations, EEPROM structure, initialization, and practical operational guidelines. Understanding the windowed register architecture and the proper use of each window is critical for efficient driver implementation and troubleshooting.

The 3C509 NIC is **widely used in legacy computing** environments due to its robust design and compatibility with ISA-based systems. This reference manual is intended for developers, system integrators, and maintainers working with the NIC at a low level, such as writing drivers, troubleshooting network configurations, and optimizing performance.

Although the NIC can be considered simple by today's standards, the **Programmed I/O (PIO) implementation** used by the 3C509 remains an interesting example of efficient low-level hardware interaction. PIO, which requires the CPU to actively transfer data to and from the NIC, can introduce performance constraints when compared to later **bus mastering** or **DMA-based** architectures. However, the **windowed register model** of the 3C509 demonstrates a practical way to optimize resource-limited ISA hardware by multiplexing multiple register banks within a constrained I/O address space. This technique allows the NIC to handle **transmit/receive buffering, media control, and configuration tasks** efficiently while operating on a legacy bus architecture. Understanding this approach is crucial for developers maintaining older networking hardware or designing optimized low-level drivers.

### **Key Features**

* **16-bit ISA Bus Interface**: The NIC operates on standard ISA slots, allowing integration into legacy PC systems.
* **Multi-Mode Media Support**: The card supports **10Base-T (Twisted Pair), 10Base2 (BNC), and AUI**connections, making it adaptable to different network environments.
* **EEPROM-Based Configuration**: Critical parameters such as MAC address, I/O base, and IRQ settings are stored in EEPROM and can be read and modified by the driver.
* **Windowed Register Architecture**: The NIC uses a windowing technique to efficiently manage a large number of registers within a limited I/O address space.
* **Hardware-Accelerated Packet Handling**: Integrated FIFO buffers and hardware-assisted packet transmission and reception improve performance in constrained systems.
* **Plug and Play (PnP) Support**: The **3C509B** introduced ISA Plug and Play capabilities, allowing for automatic resource allocation of IRQs and I/O bases in compatible systems.

This document is structured to provide a logical flow of information, from **hardware specifications** to **register details**, **window management**, and **advanced diagnostics** along with **code snippets** for the most significant use cases.
The goal is to serve as both an educational and practical reference for working with the 3C509 NIC at a deep technical level.

#

# 2\. Hardware Specifications

The EtherLink III 3C509 is a **16-bit ISA** network interface card (NIC) designed for legacy PC architectures. It supports multiple media types, configurable I/O base addresses, and software-defined IRQ settings. The following specifications provide an overview of the card's physical and operational characteristics.

## 2.1 Bus Interface

The **3C509** operates on the **Industry Standard Architecture (ISA)** bus, a widely used expansion interface in older PCs. The card features a **16-bit data bus**, which allows it to transfer data at higher speeds compared to older **8-bit** ISA NICs. It operates synchronously with the ISA bus, running at typical bus speeds of **8 MHz or 16 MHz**, depending on system configuration. The card occupies a **fixed I/O address range**, which can be set manually through software, typically defaulting to `0x300`.

## 2.2 Network Media Support

One of the key advantages of the **3C509** is its **multi-mode media support**, allowing it to adapt to different physical network environments. This flexibility makes it ideal for mixed networking infrastructures, supporting both older **coaxial** networks and modern **twisted-pair Ethernet** connections.

| Media Type | Connector | Description |
| :---- | :---- | :---- |
| **10Base-T** | RJ-45 | Standard **twisted-pair Ethernet** interface. This is the most common connection type, used in modern Ethernet networks. |
| **10Base2** | BNC | Also known as **ThinNet**, this coaxial connection was widely used in older networks, typically arranged in a **bus topology**. |
| **AUI** | 15-pin D-Sub | The **Attachment Unit Interface (AUI)** allows external transceivers to be connected, supporting additional media types like **fiber optic** or **thick coaxial** networks. |

The NIC can **automatically detect and configure the appropriate media type** based on the connection, or it can be manually set via the **Media Control Register** (in **Window 4**). This capability ensures compatibility with a broad range of network infrastructures.

## 2.3 Interrupt and I/O Configuration

The **3C509 NIC** allows flexible **IRQ (interrupt request) assignment** and **I/O base address configuration**, both of which are critical for system compatibility. Since **ISA devices do not support Plug and Play (PnP) IRQ sharing**, proper configuration of these values is required to avoid conflicts with other devices. Unlike PCI devices, which allow multiple devices to share the same interrupt line, **ISA devices require each IRQ to be uniquely assigned**, meaning two devices cannot use the same IRQ without causing conflicts.

In older ISA-based systems, expansion cards often had **fixed or manually configurable IRQs**, requiring users to manually adjust settings using **jumpers or software utilities**. The **3C509 B-variant** introduced **ISA Plug and Play (PnP)** capabilities, allowing the BIOS or operating system to automatically assign an IRQ and I/O base without requiring manual intervention. However, many legacy systems do not fully support ISA PnP, requiring a combination of **BIOS settings, operating system utilities, and EEPROM modifications** to properly configure the card.

For **EISA-based systems**, configuration is handled via **EISA Configuration Files (ECF)**, which define valid IRQ and I/O base assignments for installed expansion cards. These configuration files were required by early **EISA setup utilities** to prevent resource conflicts and ensure optimal hardware compatibility.

### Interrupt (IRQ) Assignment

ISA devices use **hardware interrupts** to notify the CPU of events such as **received packets** or **transmission completion**. When the NIC detects incoming data or successfully transmits a packet, it signals the CPU by triggering an IRQ. Because ISA lacks an IRQ-sharing mechanism, each device must have a unique IRQ assigned.

The **3C509** supports configurable IRQ assignments, which are **stored in EEPROM** and can be modified through software. The available IRQs are:

* **Supported IRQs**: 3, 5, 7, 9, 10, 11, 12, and 15
* **PnP-enabled systems (3C509B)**: The IRQ is dynamically assigned by the **BIOS or OS**
* **Non-PnP (manual mode)**: The IRQ must be set in the **EEPROM Configuration Register**
* **EISA systems**: The IRQ is assigned using **EISA Configuration Files (ECF)**

To check or change the assigned IRQ, the driver must read or write to **Window 0’s Resource Configuration Register (0x08)**.

**Example: Reading the Assigned IRQ**

`mov dx, base_address + 0x08  ; Resource Configuration Register (Window 0)`
`in ax, dx                    ; Read current IRQ setting`

**Example: Modifying the IRQ (Non-PnP Mode)**

`mov dx, base_address + 0x0A  ; EEPROM Command Register`
`mov ax, 0x80 | new_irq       ; Set new IRQ in EEPROM`
`out dx, ax`

**Example: EISA ECF Snippet for IRQ Configuration**

`FUNCTION = "Interrupt Request Level"`
    `HELP =`
        `"Determines the interrupt request level that is used by the adapter."`
    `CHOICE = "3"`
        `LINK`
            `IRQ = 3`
            `INIT = IOPORT(2) LOC(15 14 13 12) 0011`
    `CHOICE = "5"`
        `LINK`
            `IRQ = 5`
            `INIT = IOPORT(2) LOC(15 14 13 12) 0101`
    `CHOICE = "7"`
        `LINK`
            `IRQ = 7`
            `INIT = IOPORT(2) LOC(15 14 13 12) 0111`
    `CHOICE = "9"`
        `LINK`
            `IRQ = 9`
            `INIT = IOPORT(2) LOC(15 14 13 12) 1001`
    `CHOICE = "10"`
        `LINK`
            `IRQ = 10`
            `INIT = IOPORT(2) LOC(15 14 13 12) 1010`
    `CHOICE = "11"`
        `LINK`
            `IRQ = 11`
            `INIT = IOPORT(2) LOC(15 14 13 12) 1011`
    `CHOICE = "12"`
        `LINK`
            `IRQ = 12`
            `INIT = IOPORT(2) LOC(15 14 13 12) 1100`
    `CHOICE = "15"`
        `LINK`
            `IRQ = 15`
            `INIT = IOPORT(2) LOC(15 14 13 12) 1111`

### I/O Base Address

Every ISA device requires a dedicated **I/O base address** to communicate with the system. The **3C509’s default I/O base is `0x300`**, but it can be changed to another valid address if necessary. The I/O base determines the range of **I/O ports** the NIC will use for register access.

* **Default I/O base**: **`0x300`**
* **Valid I/O range**: **`0x200-0x3F0`** (varies depending on system configuration)
* **PnP Mode (3C509B)**: The BIOS and/or OS assigns the I/O base dynamically
* **EISA Mode**: The I/O base is set via **EISA configuration utilities**
* **Legacy Mode**: The base address is read from **EEPROM** and must be manually set

The EEPROM retains the I/O base setting, ensuring that the NIC remains at the same address across reboots unless reconfigured.

**Example: Manually Checking and Modifying the I/O Base Address**

`mov dx, base_address + 0x06  ; Address Configuration Register (Window 0)`
`in ax, dx                    ; Read current I/O base setting`
`mov dx, base_address + 0x0A  ; EEPROM Command Register`
`mov ax, 0x80 | new_io_base   ; Set new I/O base in EEPROM`
`out dx, ax`

**Example: Using PnP for Querying and Modifying the I/O Base**

`mov dx, 0x279               ; Read PnP status port`
`out dx, al                  ; Wake up PnP mode`
`mov dx, 0x203               ; PnP Read Data Port`
`in al, dx                   ; Read current I/O base`
`mov dx, 0x207               ; PnP Write Data Port`
`mov al, new_io_base         ; Set new I/O base`
`out dx, al`

**Example: EISA ECF Snippet for I/O Base Address**

`FUNCTION = "I/O Address Range"`
    `SHOW = NO`
    `HELP =`
        `"This function is a placeholder for the I/O ports resource."`
    `CHOICE = ""`
        `LINK`
            `PORT = 0z0h-0zfh`
            `INIT = IOPORT(1) LOC(4 3 2 1 0) 11111`

## 2.4 Memory and Buffering

The **3C509** features onboard **FIFO (First-In, First-Out) buffers** to optimize data transfer efficiency. These buffers help to smooth out variations in bus access times, reducing the likelihood of dropped packets or transmission delays.

### FIFO Buffers

* The NIC has separate **RX (Receive) and TX (Transmit) FIFOs**.
* **FIFO depth**: 2 KB shared between receive and transmit queues.
* The FIFO prevents **bus contention issues** and enhances **performance under high traffic loads**.
* The driver must monitor **FIFO status registers** in **Window 1** to ensure packets are processed efficiently.

**Example: Checking TX FIFO space before sending a packet**

`mov dx, base_address + 0x0C  ; TX Free Space Register (Window 1)`
`in ax, dx                    ; Read available buffer size`
`cmp ax, packet_size          ; Check if space is sufficient`
`jb wait_for_space            ; Wait if buffer is full`

### EEPROM Storage

The **EEPROM** (Electrically Erasable Programmable Read-Only Memory) stores **persistent configuration data** for the NIC, including:

* **MAC Address**
* **IRQ Settings**
* **I/O Base Address**
* **Preferred Media Type**
* **Performance Tuning Parameters**

This data is retained even after power is removed and is critical for ensuring consistent hardware operation across system reboots.

## 2.5 Power and Performance Features

The 3C509 NIC includes several power management and performance optimization features designed to enhance efficiency, reduce power consumption, and improve network responsiveness. These features allow the card to dynamically adjust its operation based on network conditions and system requirements, ensuring reliability in both active and idle states.

### Low-Power Mode

To reduce overall system power consumption, the NIC supports a **low-power mode**, which can be activated when the card is idle. This mode helps conserve energy in legacy systems where power efficiency was not always a primary concern. When enabled, the NIC minimizes activity and reduces power draw, only waking up when network activity is detected or when an explicit command is issued by the driver.

The low-power mode can be controlled manually by writing to **Window 0’s Configuration Control Register (`0x04`)**. Alternatively, some driver implementations may configure the NIC to enter low-power mode automatically after a period of inactivity. This is particularly useful for embedded systems or battery-powered devices that rely on ISA networking.

### Automatic Media Selection

One of the NIC’s key features is its ability to **automatically detect and configure the appropriate media type**. Depending on the network infrastructure, the card can switch between **10Base-T (twisted pair Ethernet), 10Base2 (coaxial ThinNet), and AUI (Attachment Unit Interface)** without requiring manual intervention.

When auto-selection is enabled, the NIC continuously monitors the **Media Status Register (`0x08`, Window 4\)** to determine which media type is active. If multiple media types are connected, the card prioritizes them based on preset logic, typically preferring **10Base-T** over **BNC/AUI**. However, if a specific media type needs to be forced, the driver can override auto-selection by writing to the **Media Control Register (`0x0A`, Window 4\)**.

**Example: Forcing the NIC to use BNC mode**

`mov dx, base_address + 0x0A  ; Media Control Register (Window 4)`
`mov ax, 0x2000               ; Enable 10Base2 (BNC)`
`out dx, ax`

This feature simplifies network configuration and makes the 3C509 adaptable to diverse networking environments.

### Packet Filtering

To improve network efficiency and reduce unnecessary CPU processing, the NIC includes **hardware-assisted packet filtering**. This allows the NIC to accept or discard packets based on their destination address, significantly reducing the overhead on the system processor.

Packet filtering is managed through **Window 1’s Receive Control Register**, which can be configured to accept only specific types of packets:

* **Unicast filtering**: Accepts packets addressed specifically to the NIC’s MAC address.
* **Multicast filtering**: Allows the NIC to accept packets sent to multicast groups.
* **Broadcast filtering**: Determines whether broadcast packets should be processed.

By offloading packet filtering to the hardware, the NIC reduces the number of interrupts generated by unwanted packets, improving overall system performance.

## 2.6 Plug and Play (PnP) Support

One of the major advancements introduced in the **3C509B** variant over its predecessor is **ISA Plug and Play (PnP) support**. PnP allows the system BIOS or operating system to automatically detect and configure the card’s **I/O base, IRQ, and DMA settings**, reducing the need for manual jumper configuration.

PnP functionality is controlled through **Window 0’s Resource Configuration Register (`0x08`)** and a series of specialized PnP registers. The NIC supports both **software-driven PnP configuration** and legacy **manual configuration mode**, ensuring compatibility with older systems.

#### **PnP Features:**

* **Automatic Resource Allocation**: Eliminates IRQ and I/O conflicts by dynamically selecting available settings.
* **EEPROM-Stored Configuration**: Ensures consistent settings across system reboots.
* **Fallback to Manual Mode**: Allows users to disable PnP and manually assign IRQ and I/O base if necessary.

**Example: Enabling PnP mode via software configuration**

`mov dx, base_address + 0x08  ; Resource Configuration Register (Window 0)`
`mov ax, 0x4000               ; Enable PnP`
`out dx, ax`

# 3\. Windowed Register Architecture

The 3Com 3C509 NIC employs a **windowed register system** to expand the number of available registers while maintaining a compact I/O footprint. This system divides registers into distinct groups called **windows**, each focusing on a particular aspect of NIC functionality. Software must explicitly select a window before interacting with its registers, ensuring that different functions do not interfere with each other.

## 3.1 Concept Overview

Due to the limited address space, PIO-type ISA expansion cards use a **windowed register model** to multiplex multiple register sets within the same I/O address range. This technique **allows a larger number of registers to be accessed** without tying up additional physical I/O ports.

At any given time, only the registers in the active window are accessible. To access registers from another window, **the driver must switch the active window** by issuing a **window select command**. This architecture enables efficient memory usage but requires careful software management to avoid register conflicts.

### Windowed Register Mechanism

* **Registers are grouped into logical sets (windows).**
* **Each window contains a different set of registers**, such as configuration settings, transmit/receive operations, or diagnostics.
* **A single Command/Status register is always accessible**, used to issue commands including window selection.
* **Only one window is active at a time**, and all register operations apply only to the currently selected window.

## 3.2 Window Selection Procedure

To switch between register windows safely, the driver must follow a structured procedure:

### Step-by-Step Window Selection

1. **Check Command Register Status**

* Read from the **Command/Status register (`0x0E`)**.
* Verify that the ***3C509B\_STATUS\_CMD\_BUSY*** **(0x1000)** bit is cleared, ensuring the NIC is ready for a command.

2. **Select the Desired Window**

* Write the ***3C509B\_CMD\_SELECT\_WINDOW | window\_number*** command to the Command/Status register.
  **Example: Selecting Window 1 (Transmit/Receive Operations)**
  `mov dx, base_address + 0x0E   ; Command/Status register`
  `mov ax, _3C509B_CMD_SELECT_WINDOW | 1  ; Select Window 1`
  `out dx, ax`

3. **Verify Window Selection**

* Read from a known register within the new window to confirm the selection.
  **Example: Verifying Window Selection**
  `mov dx, base_address + 0x08  ; RX Status Register (Window 1)`
  `in ax, dx                    ; Read RX status`
  `cmp ax, 0                    ; Check if valid response`
  `jz error                     ; Jump if incorrect window selected`

## 3.3 Window Summary

The following table provides a high-level overview of the available windows and their functions:

| Window | Functionality | Typical Use Cases |
| :---: | :---- | :---- |
| **0** | Configuration and EEPROM Access | Setting IRQ, I/O base, EEPROM read/write operations |
| **1** | Transmit and Receive Operations | Sending and receiving packets, managing FIFO |
| **2** | MAC Address Setup and Filtering | Programming the MAC address, setting filters |
| **4** | Media Control and Diagnostics | Configuring media type (10Base-T, AUI, BNC), LED control |
| **6** | Network Statistics | Tracking performance, collecting error statistics |

Each window must be explicitly selected before interacting with its registers. Failing to do so may result in unintended behavior or incorrect register access.

# 4\. Windowed Register Details

Each window in the 3Com 3C509 NIC provides access to a specific set of registers for controlling various aspects of the card's operation. This chapter describes each window in detail, including its **purpose, register layout, command set, and example use cases**.

## 4.1 Window 0: Configuration and EEPROM Access

### Purpose

Window 0 is primarily used during initialization. It provides access to the EEPROM, I/O base configuration, and IRQ settings. The EEPROM stores critical information such as the MAC address and resource allocations. Software must interact with the EEPROM using a specific read/write procedure.

### Register Map for Window 0

| Offset | Register Name | Description |
| :---: | :---- | :---- |
| **`0x00`** | Manufacturer ID | 3Com vendor ID (read-only) |
| **`0x02`** | Product ID | Product identification (read-only) |
| **`0x04`** | Configuration Control | Reset and configuration control |
| **`0x06`** | Address Configuration | I/O base address settings |
| **`0x08`** | Resource Configuration | IRQ, DMA, and shared memory settings |
| **`0x0A`** | EEPROM Command | EEPROM operation command |
| **`0x0C`** | EEPROM Data | EEPROM data read/write |
| **`0x0E`** | Command/Status | Universal command and status register |

###

### Bitfield Breakdown

**Configuration Control Register (0x04)**

`Options →`
`+------------------------------------+`
`|  15-8   | 7 | 6 | 5 | 4 | 3 | 2-0  |`
`+------------------------------------+`
`|  Res    | 0 | 0 | 1 | 1 | 0 | 000  |`
`+------------------------------------+`
            `|   |   |   |   |   |`
            `|   |   |   |   |   +--> Reserved`
            `|   |   |   |   +------> Enable Full Duplex`
            `|   |   |   +----------> Force media selection`
            `|   |   +--------------> Enable EEPROM programming`
            `|   +------------------> Reset NIC`
            `+----------------------> Reserved`

**Address Configuration Register (0x06)**

`Options →`
`+---------------------------------+`
`| 15-10 |         9-4      | 3-0  |`
`+---------------------------------+`
`| Res   | I/O Base Address | Res  |`
`+---------------------------------+`
                 `|            |`
                 `|            +-> Reserved`
                 `+--------------> I/O Base Address`
                                    `(default: 0x300, mask 0x3F)`

**Resource Configuration Register (0x08)**

`Options →`
`+-----------------------------+`
`|  15-12  | 11-8 | 7-4 | 3-0  |`
`+-----------------------------+`
`|  Res    | IRQ  | Res | DMA  |`
`+-----------------------------+`
              `|     |     |`
              `|     |     +-> DMA Channel (non-3C509)`
              `|     +-------> Reserved`
              `+-------------> IRQ Number`
                                `(values: 3, 5, 7, 9, 10, 11, 12, 15)`

### Typical EEPROM Read Procedure

`mov dx, base_address + 0x0A  ; EEPROM Command Register`
`mov ax, 0x80 | address        ; Issue read command`
`out dx, ax`
`mov dx, base_address + 0x0C  ; EEPROM Data Register`
`in ax, dx                    ; Read data from EEPROM`

## 4.2 Window 1: Transmit and Receive Operations

### Purpose

Window 1 is the **primary operational window** for sending and receiving packets. It contains **FIFO buffers**, **status registers**, and **interrupt control settings** for efficient packet handling.

### Register Map for Window 1

| Offset | Register Name | Description |
| :---: | :---- | :---- |
| **`0x00`** | TX/RX FIFO | Transmit or receive data port |
| **`0x02`** | TX/RX FIFO (32-bit) | 32-bit access to FIFO for faster transfers |
| **`0x06`** | TX/RX Status | Status of last transmit/receive |
| **`0x08`** | RX Status | Detailed status of received packet |
| **`0x0A`** | Timer | Internal timer value |
| **`0x0B`** | TX Status | Detailed status of transmitted packet |
| **`0x0C`** | TX Free | Free bytes in transmit FIFO |
| **`0x0E`** | Command/Status | Command register and status |

### Bitfield Breakdown

**TX/RX Status Register (0x06)**

`Options →`
`+---------------------------+`
`|  15-8  |  7  |  6  | 5-0  |`
`+---------------------------+`
`|  Res   |  0  |  1  | Data |`
`+---------------------------+`
            `|     |     |`
            `|     |     +-> FIFO Packet Status Bits (varies by mode)`
            `|     +-------> Receive FIFO Full`
            `+-------------> Transmit FIFO Empty`

**RX Status Register (0x08)**

`Options →`
`+----------------------------------------+`
`|  15-13  | 12 | 11 | 10 |  9 |  8 | 7-0 |`
`+----------------------------------------+`
`|   Res   |  0 |  1 |  0 |  1 |  0 | Len |`
`+----------------------------------------+`
             `|    |    |    |    |    |`
             `|    |    |    |    |    +--> RX FIFO Empty`
             `|    |    |    |    +-------> RX FIFO Overflow`
             `|    |    |    +------------> Oversized Packet Error`
             `|    |    +-----------------> Frame Alignment Error`
             `|    +----------------------> CRC Error`
             `+---------------------------> Reserved`
