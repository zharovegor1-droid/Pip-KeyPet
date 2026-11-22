import Foundation

/// IMA ADPCM, the decoder for Pip's voice packets. Keep in step with ui/adpcm.c in the firmware.
/// Packet: [seq u8][predictor i16 LE][step index u8][nibbles: the first sample in the low half of a byte].
enum ADPCM {
    private static let steps: [Int] = [
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
        41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
        230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
        1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
        7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
    ]
    private static let moves: [Int] = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]

    struct Packet {
        let seq: UInt8
        let pcm: [Int16]
    }

    static func decode(_ data: Data) -> Packet? {
        let b = [UInt8](data)
        guard b.count >= 4 else { return nil }
        var pred = Int(Int16(bitPattern: UInt16(b[1]) | (UInt16(b[2]) << 8)))
        var index = min(max(Int(b[3]), 0), 88)
        var out: [Int16] = []
        out.reserveCapacity((b.count - 4) * 2)
        func apply(_ code: Int) {
            let step = steps[index]
            var diff = step >> 3
            if code & 4 != 0 { diff += step }
            if code & 2 != 0 { diff += step >> 1 }
            if code & 1 != 0 { diff += step >> 2 }
            pred = min(max(pred + (code & 8 != 0 ? -diff : diff), -32768), 32767)
            index = min(max(index + moves[code], 0), 88)
            out.append(Int16(pred))
        }
        for byte in b[4...] {
            apply(Int(byte & 15))
            apply(Int(byte >> 4))
        }
        return Packet(seq: b[0], pcm: out)
    }

    /// The vector sim/adpcm_test prints: if this fails, the two decoders drifted apart.
    static func selfTest() -> Bool {
        let bytes: [UInt8] = [0x05, 0x00, 0x00, 0x00, 0xFF, 0x5F, 0x77, 0x77, 0x22, 0x21, 0x22, 0x33]
        let want: [Int16] = [-11, -41, -104, -4, 195, 625, 1550, 3537, 4957, 6248, 6951, 8017, 8987, 9868, 10989, 12008]
        guard let p = decode(Data(bytes)) else { return false }
        return p.seq == 5 && p.pcm == want
    }
}
