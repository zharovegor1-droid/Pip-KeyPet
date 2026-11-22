import AVFoundation
import Foundation
import UIKit

/// Push to talk on Pip: "ask start" → the voice comes in → "ask stop" → recognize → Claude → the answer back in pieces.
final class Assistant: NSObject, ObservableObject, BridgeDelegate, @unchecked Sendable { // used on the main queue only
    struct Exchange: Identifiable {
        let id = UUID()
        let date = Date()
        var question: String
        var answer: String
        var failed: Bool
        var fromPip: Bool
    }

    @Published private(set) var history: [Exchange] = []
    @Published private(set) var live = ""     // what is being heard right now
    @Published private(set) var phase = ""     // "слушаю", "думаю", ""
    @Published private(set) var speechAllowed = Listener.authorized

    private let bridge: Bridge
    private let settings: Settings
    private let listener: Listener
    private var context: [Claude.Turn] = [] // the chat so far, as Pip sends it before each question ("ctx …")
    private var job: Task<Void, Never>?
    private var generation = 0 // a newer question or a cancel makes older answers stale
    private var background: UIBackgroundTaskIdentifier = .invalid
    private let speaker = Speaker()

    init(bridge: Bridge, settings: Settings) {
        self.bridge = bridge
        self.settings = settings
        listener = Listener(language: settings.language)
        super.init()
        bridge.delegate = self
        listener.onPartial = { [weak self] text in
            self?.live = text
        }
        speaker.onPacket = { [weak self] packet in self?.bridge.voiceSend(packet) }
        speaker.onEnd = { [weak self] in self?.bridge.voiceFinish() }
    }

    func requestSpeech() {
        Listener.authorize { [weak self] ok in self?.speechAllowed = ok }
    }

    func languageChanged() {
        listener.setLanguage(settings.language)
    }

    // MARK: BridgeDelegate

    func bridge(_ bridge: Bridge, line: String) {
        if line == "ctx new" { context = [] }
        else if line.hasPrefix("ctx Q ") { context.append(Claude.Turn(role: "user", content: Assistant.fromPip(line.dropFirst(6)))) }
        else if line.hasPrefix("ctx A ") { context.append(Claude.Turn(role: "assistant", content: Assistant.fromPip(line.dropFirst(6)))) }
        else if line.hasPrefix("ask start") { start() }
        else if line == "ask stop" { stop() }
        else if line == "ask cancel" { cancel() }
        else if line.hasPrefix("say ") { say(Assistant.fromPip(line.dropFirst(4))) }
        else if line == "ask hush" { hush() }
    }

    private static func fromPip(_ s: Substring) -> String {
        String(s).replacingOccurrences(of: "\t", with: "\n")
    }

    func bridge(_ bridge: Bridge, audio: Data) {
        listener.feed(audio)
    }

    func bridge(_ bridge: Bridge, linked: Bool) {
        if !linked { cancel() }
    }

    // MARK: the question

    private func start() {
        cancel()
        holdAwake()
        live = ""
        phase = "слушаю"
        if let why = listener.start() { fail(why, question: "") }
    }

    private func stop() {
        guard phase == "слушаю" else { return } // it never started (and Pip was told why)
        let gen = generation
        phase = "распознаю"
        listener.finish { [weak self] text in
            guard let self, gen == self.generation else { return }
            let question = text.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !question.isEmpty else {
                self.fail("Не расслышал. Попробуй ещё раз, ближе к Пипу.", question: "")
                return
            }
            self.live = question
            self.bridge.note("вопрос: " + question)
            self.bridge.send("heard " + Assistant.oneLine(question))
            self.ask(question, gen: gen, toPip: true)
        }
    }

    /// Typed in the app: the same Claude, the answer shows here (and Pip is left alone).
    func askTyped(_ question: String) {
        let q = question.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !q.isEmpty else { return }
        cancel()
        context = []
        ask(q, gen: generation, toPip: false)
    }

    private func ask(_ question: String, gen: Int, toPip: Bool) {
        phase = "думаю"
        holdAwake()
        let context = self.context + [Claude.Turn(role: "user", content: question)] // the chat Pip keeps, then this
        let claude = settings.claude
        let system = Claude.system(language: settings.language)
        job = Task { // the assistant lives as long as the app: no weak dance
            do {
                let answer = try await claude.ask(context, system: system)
                DispatchQueue.main.async {
                    guard gen == self.generation else { return }
                    self.deliver(answer, question: question, toPip: toPip)
                }
            } catch {
                let why = error.localizedDescription
                DispatchQueue.main.async {
                    guard gen == self.generation else { return }
                    if toPip { self.fail(why, question: question) }
                    else { self.record(question, why, failed: true, fromPip: false) }
                    self.done()
                }
            }
        }
    }

    private func deliver(_ answer: String, question: String, toPip: Bool) {
        record(question, answer, failed: false, fromPip: toPip)
        if toPip {
            for piece in Assistant.pieces(Assistant.forScreen(answer)) { bridge.send("A " + piece) }
            bridge.send("A.")
            bridge.note("ответ отправлен на Пипа")
        }
        done()
    }

    private func fail(_ why: String, question: String) {
        bridge.note("не вышло: " + why)
        bridge.send("fail " + Assistant.oneLine(why))
        record(question.isEmpty ? "—" : question, why, failed: true, fromPip: true)
        listener.cancel()
        done()
    }

    private func cancel() {
        generation += 1
        listener.cancel()
        job?.cancel()
        job = nil
        done()
    }

    private func done() {
        phase = ""
        if background != .invalid {
            UIApplication.shared.endBackgroundTask(background)
            background = .invalid
        }
    }

    private func record(_ q: String, _ a: String, failed: Bool, fromPip: Bool) {
        history.insert(Exchange(question: q, answer: a, failed: failed, fromPip: fromPip), at: 0)
        if history.count > 50 { history.removeLast() }
    }

    /// The phone may be locked in a pocket: ask iOS for the half minute a question takes.
    private func holdAwake() {
        guard background == .invalid else { return }
        background = UIApplication.shared.beginBackgroundTask(withName: "pip.ask") { [weak self] in
            guard let self else { return }
            UIApplication.shared.endBackgroundTask(self.background)
            self.background = .invalid
        }
    }

    // MARK: reading it out on Pip (the top key in the chat)

    private func say(_ text: String) {
        holdAwake()
        speaker.packetSamples = bridge.voicePacketSamples
        bridge.voiceBegin()
        speaker.speak(Assistant.forSpeech(text), language: settings.language)
    }

    private func hush() {
        speaker.stop()
        bridge.voiceStop()
    }

    /// Said, not read: no markdown marks, no lists' dashes.
    static func forSpeech(_ s: String) -> String {
        var t = s
        for mark in ["**", "__", "`", "#", "\n- ", "\n* "] { t = t.replacingOccurrences(of: mark, with: mark.hasPrefix("\n") ? "\n" : "") }
        return t
    }

    // MARK: text for Pip

    /// Plain lines for the screen: no markdown; new lines become '\t', which is how Pip's protocol carries them.
    static func forScreen(_ s: String) -> String {
        var t = s.replacingOccurrences(of: "\r\n", with: "\n")
        for mark in ["**", "__", "`", "### ", "## ", "# "] { t = t.replacingOccurrences(of: mark, with: "") }
        t = t.replacingOccurrences(of: "\n* ", with: "\n- ").replacingOccurrences(of: "\n• ", with: "\n- ")
        while t.contains("\n\n\n") { t = t.replacingOccurrences(of: "\n\n\n", with: "\n\n") }
        return t.trimmingCharacters(in: .whitespacesAndNewlines).replacingOccurrences(of: "\n", with: "\t")
    }

    static func oneLine(_ s: String) -> String {
        String(s.replacingOccurrences(of: "\n", with: " ").replacingOccurrences(of: "\t", with: " ").prefix(150))
    }

    /// Pieces of at most `max` bytes of UTF-8, never splitting a letter, so every line fits Pip's buffer.
    static func pieces(_ s: String, max: Int = 150) -> [String] {
        var out: [String] = []
        var cur = ""
        var bytes = 0
        for ch in s {
            let n = String(ch).utf8.count
            if bytes + n > max {
                out.append(cur)
                cur = ""
                bytes = 0
            }
            cur.append(ch)
            bytes += n
        }
        if !cur.isEmpty { out.append(cur) }
        return out
    }
}
