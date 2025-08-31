# Ethernet Frame Format Reference  
Source: RFC 894 - Standard for the Transmission of IP Datagrams over Ethernet Networks

## Frame Structure
```
0                   6                   12      14
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Destination Address (6 bytes)       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Source Address (6 bytes)           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    Type/Length    |        Data              |
+-+-+-+-+-+-+-+-+-+-+                         ~
~                                              ~
~              Data (46-1500 bytes)            ~
~                                              ~
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 FCS (4 bytes)                |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

## Field Definitions
- **Destination Address (6 bytes)**: Target MAC address
- **Source Address (6 bytes)**: Sender MAC address  
- **Type/Length (2 bytes)**: Protocol type or frame length
- **Data (46-1500 bytes)**: Payload data
- **FCS (4 bytes)**: Frame Check Sequence (CRC-32)

## Critical Constants for DOS Implementation
- **Minimum Frame Size**: 64 bytes (including 4-byte FCS)
- **Maximum Frame Size**: 1518 bytes (including 4-byte FCS)
- **Minimum Data Size**: 46 bytes (pad with zeros if needed)
- **Maximum Data Size**: 1500 bytes
- **Header Size**: 14 bytes (6+6+2)

## EtherType Values
- **0x0800**: Internet Protocol (IP)
- **0x0806**: Address Resolution Protocol (ARP)
- **0x8137**: Novell IPX
- **Values < 0x0600**: IEEE 802.3 length field

## Special Addresses
- **Broadcast**: FF-FF-FF-FF-FF-FF (all ones)
- **Multicast**: First bit of first byte set to 1
- **Unicast**: First bit of first byte set to 0

## Implementation Notes for DOS Packet Driver
1. **Frame Validation**: Check minimum/maximum size limits
2. **Padding**: Add zero padding if data < 46 bytes
3. **CRC Calculation**: Hardware typically handles FCS generation/checking
4. **Type Field**: Use 0x0800 for IP, 0x0806 for ARP
5. **Address Filtering**: Support unicast, broadcast, and multicast filtering

## Memory Layout for DOS
```c
struct ethernet_header {
    uint8_t  dest_addr[6];    // Destination MAC
    uint8_t  src_addr[6];     // Source MAC  
    uint16_t type_length;     // Network byte order
} __attribute__((packed));

#define ETH_TYPE_IP      0x0800
#define ETH_TYPE_ARP     0x0806
#define ETH_ADDR_LEN     6
#define ETH_HEADER_LEN   14
#define ETH_MIN_DATA     46
#define ETH_MAX_DATA     1500
#define ETH_MIN_FRAME    64
#define ETH_MAX_FRAME    1518
```

## Packet Driver Considerations
- Handle both Type (Ethernet II) and Length (IEEE 802.3) formats
- Support promiscuous mode for network debugging
- Implement proper address filtering for performance
- Buffer management for minimum frame size requirements