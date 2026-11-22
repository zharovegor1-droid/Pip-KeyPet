import AVFoundation
import Foundation

/// Pip reads an answer aloud on his own speaker: the phone's speech synthesizer renders it (not played here),
/// it is resampled to 16 kHz and goes to Pip as ADPCM packets.
final class Speaker {
    private let synth = AVSpeechSynthesizer()
    private let out = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: 16000, channels: 1, interleaved: true)!
    private var converter: AVAudioConverter?
    private var pending: [Int16] = []
    private var encoder = ADPCMEncoder()
    private var generation = 0

    /// Each packet as it is ready, on the main queue; then `onEnd` once.
    var onPacket: ((Data) -> Void)?
    var onEnd: (() -> Void)?
    var packetSamples = 352

    func speak(_ text: String, language: String) {
        stop()
        let gen = generation
        encoder = ADPCMEncoder()
        pending = []
        converter = nil
        let u = AVSpeechUtterance(string: text)
        u.voice = AVSpeechSynthesisVoice(language: language)
        synth.write(u) { [weak self] buffer in
            guard let self, gen == self.generation, let pcm = buffer as? AVAudioPCMBuffer else { return }
            let last = pcm.frameLength == 0 // the synthesizer's way of saying "that was all"
            if !last { self.convert(pcm) }
            let packets = self.take(all: last)
            DispatchQueue.main.async {
                guard gen == self.generation else { return }
                packets.forEach { self.onPacket?($0) }
                if last { self.onEnd?() }
            }
        }
    }

    func stop() {
        generation += 1
        synth.stopSpeaking(at: .immediate)
    }

    private func convert(_ input: AVAudioPCMBuffer) {
        if converter == nil || converter?.inputFormat != input.format {
            converter = AVAudioConverter(from: input.format, to: out)
        }
        guard let converter else { return }
        let capacity = AVAudioFrameCount(Double(input.frameLength) * out.sampleRate / input.format.sampleRate) + 64
        guard let dst = AVAudioPCMBuffer(pcmFormat: out, frameCapacity: capacity) else { return }
        var fed = false
        var error: NSError?
        converter.convert(to: dst, error: &error) { _, status in
            if fed {
                status.pointee = .noDataNow
                return nil
            }
            fed = true
            status.pointee = .haveData
            return input
        }
        guard let p = dst.int16ChannelData?[0] else { return }
        pending.append(contentsOf: UnsafeBufferPointer(start: p, count: Int(dst.frameLength)))
    }

    private func take(all: Bool) -> [Data] {
        var packets: [Data] = []
        let per = max(32, packetSamples & ~1)
        var at = 0
        while pending.count - at >= per {
            packets.append(encoder.packet(pending[at ..< at + per]))
            at += per
        }
        if all, pending.count - at >= 2 {
            let n = (pending.count - at) & ~1
            packets.append(encoder.packet(pending[at ..< at + n]))
            at += n
        }
        pending.removeFirst(at)
        return packets
    }
}
