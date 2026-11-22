import AVFoundation
import Foundation
import Speech

/// Pip's voice → text, on the phone (on-device recognition when the language supports it: nothing leaves the iPhone).
final class Listener {
    private var recognizer: SFSpeechRecognizer?
    private var request: SFSpeechAudioBufferRecognitionRequest?
    private var task: SFSpeechRecognitionTask?
    private let format = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: 16000, channels: 1, interleaved: false)!
    private var best = ""
    private var done: ((String) -> Void)?
    private var lastSeq: UInt8?
    private var lastCount = 0
    private(set) var samples = 0

    /// What has been understood so far, as it grows (on the main queue).
    var onPartial: ((String) -> Void)?

    init(language: String) {
        setLanguage(language)
    }

    func setLanguage(_ language: String) {
        recognizer = SFSpeechRecognizer(locale: Locale(identifier: language))
    }

    static var authorized: Bool { SFSpeechRecognizer.authorizationStatus() == .authorized }

    static func authorize(_ done: @escaping (Bool) -> Void) {
        SFSpeechRecognizer.requestAuthorization { status in
            DispatchQueue.main.async { done(status == .authorized) }
        }
    }

    var onDevice: Bool { recognizer?.supportsOnDeviceRecognition ?? false }

    /// Begin a question. Returns why it cannot, or nil.
    func start() -> String? {
        cancel()
        guard Listener.authorized else { return "Нет разрешения на распознавание речи: открой Pip Bridge" }
        guard let recognizer, recognizer.isAvailable else { return "Распознавание речи сейчас недоступно" }
        let r = SFSpeechAudioBufferRecognitionRequest()
        r.shouldReportPartialResults = true
        r.taskHint = .search
        r.addsPunctuation = true
        if recognizer.supportsOnDeviceRecognition { r.requiresOnDeviceRecognition = true }
        request = r
        best = ""
        lastSeq = nil
        samples = 0
        task = recognizer.recognitionTask(with: r) { [weak self] result, error in
            DispatchQueue.main.async { self?.update(result, error) }
        }
        return nil
    }

    /// One voice packet from Pip. A lost one becomes silence of the same length, so the words keep their timing.
    func feed(_ packet: Data) {
        guard request != nil, let p = ADPCM.decode(packet) else { return }
        if let last = lastSeq {
            let gap = Int(p.seq &- last) - 1
            if gap > 0 && gap < 10 { append([Int16](repeating: 0, count: gap * lastCount)) }
        }
        lastSeq = p.seq
        lastCount = p.pcm.count
        append(p.pcm)
    }

    private func append(_ pcm: [Int16]) {
        guard !pcm.isEmpty, let buf = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(pcm.count)),
              let ch = buf.floatChannelData?[0] else { return }
        buf.frameLength = AVAudioFrameCount(pcm.count)
        for i in 0 ..< pcm.count { ch[i] = Float(pcm[i]) / 32768 }
        request?.append(buf)
        samples += pcm.count
    }

    /// The key was let go: the final text (maybe empty) comes to `done`, within a few seconds at most.
    func finish(_ done: @escaping (String) -> Void) {
        guard request != nil else {
            done(best)
            return
        }
        self.done = done
        request?.endAudio()
        DispatchQueue.main.asyncAfter(deadline: .now() + 4) { [weak self] in self?.complete() }
    }

    func cancel() {
        task?.cancel()
        task = nil
        request = nil
        done = nil
    }

    private func update(_ result: SFSpeechRecognitionResult?, _ error: Error?) {
        if let result {
            best = result.bestTranscription.formattedString
            onPartial?(best)
            if result.isFinal { complete() }
        } else if error != nil {
            complete()
        }
    }

    private func complete() {
        guard let d = done else { return }
        done = nil
        task = nil
        request = nil
        d(best)
    }
}
