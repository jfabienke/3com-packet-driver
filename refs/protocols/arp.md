# ARP Protocol Reference
Source: RFC 826 (https://www.ietf.org/rfc/rfc826.txt)

## Packet Structure
```
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|        Hardware Type          |         Protocol Type         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Hlen  | Plen  |           Operation           |   Sender      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Sender Hardware Address                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Sender Hardware Address (cont)|   Sender Protocol Address    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Sender Protocol Address (cont)|   Target Hardware Address    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Target Hardware Address                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Target Protocol Address        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

## Field Definitions
- **Hardware Type (16 bits)**: 1 for Ethernet
- **Protocol Type (16 bits)**: Protocol being resolved (0x0800 for IP)
- **Hardware Length (8 bits)**: 6 for Ethernet MAC addresses
- **Protocol Length (8 bits)**: 4 for IPv4 addresses
- **Operation (16 bits)**: 1 = REQUEST, 2 = REPLY

## Implementation Requirements for DOS Packet Driver
1. **Address Resolution Table**: Maintain cache of protocol->hardware mappings
2. **Request Processing**: Broadcast ARP requests when target unknown
3. **Reply Processing**: Update cache with sender information from all ARP packets
4. **Reply Generation**: Respond to ARP requests for local protocol addresses
5. **Table Management**: Age out old entries, handle table overflow

## Ethernet-Specific Values
- Hardware Type: 1
- Hardware Address Length: 6 bytes
- Typical Protocol Type: 0x0800 (IPv4)
- Protocol Address Length: 4 bytes (for IPv4)

## Processing Algorithm
1. Receive ARP packet
2. Extract sender hardware/protocol address pair
3. Update/add entry in translation table
4. If operation is REQUEST and target protocol address matches local address:
   - Set operation to REPLY
   - Swap sender/target fields
   - Fill in local hardware address as new sender
   - Transmit reply

## Memory Layout for DOS Implementation
```c
struct arp_packet {
    uint16_t hw_type;       // Network byte order
    uint16_t proto_type;    // Network byte order
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t operation;     // Network byte order
    uint8_t  sender_hw[6];  // MAC address
    uint8_t  sender_proto[4]; // IP address
    uint8_t  target_hw[6];  // MAC address
    uint8_t  target_proto[4]; // IP address
} __attribute__((packed));
```