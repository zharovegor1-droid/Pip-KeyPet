import CoreBluetooth
import Foundation

/// Pip's bridge service (firmware/main/ble.c). The app rides the link iOS already keeps to Pip for notifications
/// and music: Pip has to be paired in Settings → Bluetooth first (it shows up there as a keyboard named "Pip").
enum PipUUID {
    static let service = CBUUID(string: "50495042-5249-4447-4500-000000000001")
    static let rx = CBUUID(string: "50495042-5249-4447-4500-000000000002")    // app → Pip: lines
    static let tx = CBUUID(string: "50495042-5249-4447-4500-000000000003")    // Pip → app: lines
    static let audio = CBUUID(string: "50495042-5249-4447-4500-000000000004") // Pip → app: the voice, ADPCM
    static let speak = CBUUID(string: "50495042-5249-4447-4500-000000000005") // app → Pip: his voice to play, ADPCM
    static let battery = CBUUID(string: "180F")
    static let hid = CBUUID(string: "1812")
}

struct PipStatus: Decodable, Equatable {
    var bat: Int
    var chg: Bool
    var fw: String
    var up: Int
    var ram: Int
    var psram: Int
    var rssi: Int
    var mood: String
    var theme: Int
    var wifi: Int
    var mtu: Int
}

protocol BridgeDelegate: AnyObject {
    func bridge(_ bridge: Bridge, line: String)
    func bridge(_ bridge: Bridge, audio: Data)
    func bridge(_ bridge: Bridge, linked: Bool)
}

final class Bridge: NSObject, ObservableObject {
    enum State: Equatable {
        case bluetoothOff, unauthorized, searching, connecting, noBridge, ready

        var title: String {
            switch self {
            case .bluetoothOff: return "Bluetooth выключен"
            case .unauthorized: return "Нет доступа к Bluetooth"
            case .searching: return "Ищу Pip…"
            case .connecting: return "Подключаюсь…"
            case .noBridge: return "На Pip прошивка без моста"
            case .ready: return "На связи"
            }
        }
    }

    static let protocolVersion = 2

    @Published private(set) var state: State = .searching
    @Published private(set) var status: PipStatus?
    @Published private(set) var firmware = ""
    @Published private(set) var wifi: [String] = []
    @Published private(set) var wifiLoaded = false
    @Published var lastError: String?
    @Published private(set) var log: [String] = []

    weak var delegate: BridgeDelegate?

    private var central: CBCentralManager!
    private var pip: CBPeripheral?
    private var rx: CBCharacteristic?
    private var sp: CBCharacteristic?
    private var voice: [Data] = []    // packets for Pip's speaker, sent as fast as the link takes them
    private var voiceEnd = false      // "say off" once they are all out
    private var inbox = Data()
    private var wifiIncoming: [String] = []
    private var timer: Timer?
    private var helloAt = Date.distantPast
    private var connectingSince = Date.distantPast
    private let savedKey = "pip.peripheral"

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil, options: [
            CBCentralManagerOptionRestoreIdentifierKey: "pip.bridge.central",
            CBCentralManagerOptionShowPowerAlertKey: true,
        ])
        timer = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { [weak self] _ in self?.tick() }
    }

    // MARK: sending

    /// One line to Pip. Long lines go in several writes; Pip joins them up to the '\n'.
    func send(_ line: String) {
        guard let pip, let rx, state == .ready else { return }
        var data = Data((line + "\n").utf8)
        let chunk = max(20, min(180, pip.maximumWriteValueLength(for: .withResponse)))
        while !data.isEmpty {
            let part = Data(data.prefix(chunk))
            pip.writeValue(part, for: rx, type: .withResponse)
            data.removeFirst(part.count)
        }
    }

    // MARK: Pip's voice

    /// ADPCM samples per packet for this link (Pip takes up to 480).
    var voicePacketSamples: Int {
        guard let pip else { return 352 }
        return max(32, min(480, (pip.maximumWriteValueLength(for: .withoutResponse) - 4) * 2))
    }

    func voiceBegin() {
        voice.removeAll()
        voiceEnd = false
        send("say on")
    }

    func voiceSend(_ packet: Data) {
        voice.append(packet)
        pump()
    }

    func voiceFinish() {
        voiceEnd = true
        pump()
    }

    func voiceStop() {
        voice.removeAll()
        voiceEnd = false
    }

    private func pump() {
        guard let pip, let sp, state == .ready else { voice.removeAll(); return }
        while !voice.isEmpty && pip.canSendWriteWithoutResponse {
            pip.writeValue(voice.removeFirst(), for: sp, type: .withoutResponse)
        }
        if voice.isEmpty && voiceEnd {
            voiceEnd = false
            send("say off")
        }
    }

    func refresh() {
        send("stat?")
        send("wifi?")
    }

    func addWifi(ssid: String, password: String) {
        send("wifi+ \(ssid)\t\(password)")
    }

    func forgetWifi(ssid: String) {
        send("wifi- \(ssid)")
    }

    /// Look for Pip again now (the app came to the front).
    func wake() {
        if state == .ready { send("stat?") } else { find() }
    }

    // MARK: finding Pip

    private func tick() {
        if state == .ready {
            if firmware.isEmpty && Date().timeIntervalSince(helloAt) > 8 { hello() } // Pip did not answer: say it again
        } else if state != .connecting || Date().timeIntervalSince(connectingSince) > 20 {
            find() // also when a connection hangs halfway
        }
    }

    private func find() {
        guard central.state == .poweredOn else { return }
        if let pip, pip.state == .connected {
            // ours already (restored in the background, or the services are being looked at): just open the bridge
            pip.delegate = self
            setConnecting()
            pip.discoverServices([PipUUID.service])
            return
        }
        let saved = UserDefaults.standard.string(forKey: savedKey)
        // Pip is usually already connected to the phone (notifications, music): take that link
        let withBridge = central.retrieveConnectedPeripherals(withServices: [PipUUID.service])
        let withBattery = central.retrieveConnectedPeripherals(withServices: [PipUUID.battery])
            .filter { $0.name == "Pip" || $0.identifier.uuidString == saved }
        if let p = withBridge.first ?? withBattery.first {
            connect(p)
            return
        }
        if let saved, let id = UUID(uuidString: saved), let p = central.retrievePeripherals(withIdentifiers: [id]).first {
            connect(p) // a pending connection: iOS makes it whenever Pip comes into range, even with the app asleep
            return
        }
        state = .searching
        if !central.isScanning { central.scanForPeripherals(withServices: [PipUUID.hid], options: nil) }
    }

    private func connect(_ p: CBPeripheral) {
        if central.isScanning { central.stopScan() }
        pip = p
        p.delegate = self
        setConnecting()
        central.connect(p, options: nil) // connected by the system already: didConnect comes at once and the link is ours too
    }

    private func setConnecting() {
        if state != .connecting { connectingSince = Date() }
        state = .connecting
    }

    private func hello() {
        helloAt = Date()
        send("hello")
    }

    private func linkDown() {
        let was = state == .ready
        rx = nil
        sp = nil
        voice.removeAll()
        inbox.removeAll()
        firmware = ""
        if was { delegate?.bridge(self, linked: false) }
    }

    // MARK: lines from Pip

    private func received(_ data: Data) {
        inbox.append(data)
        while let nl = inbox.firstIndex(of: 0x0A) {
            let lineData = inbox[inbox.startIndex ..< nl]
            inbox.removeSubrange(inbox.startIndex ... nl)
            if let line = String(data: Data(lineData), encoding: .utf8) { handle(line) }
        }
        if inbox.count > 4096 { inbox.removeAll() } // garbage with no end
    }

    private func handle(_ line: String) {
        if line.hasPrefix("hi ") {
            let parts = line.split(separator: " ", maxSplits: 2).map(String.init)
            firmware = parts.count > 2 ? parts[2] : "?"
            if let v = Int(parts.count > 1 ? parts[1] : ""), v != Bridge.protocolVersion {
                lastError = "Протокол Pip v\(v), приложение v\(Bridge.protocolVersion): обнови то, что старее"
            }
            send("wifi?")
        } else if line.hasPrefix("stat ") {
            status = try? JSONDecoder().decode(PipStatus.self, from: Data(line.dropFirst(5).utf8))
        } else if line.hasPrefix("wifi+ ") {
            wifiIncoming.append(String(line.dropFirst(6)))
        } else if line == "wifi." {
            wifi = wifiIncoming
            wifiIncoming = []
            wifiLoaded = true
        } else if line.hasPrefix("err ") {
            lastError = String(line.dropFirst(4))
        }
        // the journal is for people: only what Pip did, not the protocol's chatter
        switch line {
        case _ where line.hasPrefix("ask start"): note("Пип слушает")
        case "ask stop": note("Пип отпустил кнопку — распознаю")
        case "ask cancel": note("вопрос отменён")
        case _ where line.hasPrefix("say "): note("Пип читает ответ вслух")
        case "ask hush": note("Пип попросил замолчать")
        case _ where line.hasPrefix("err "): note("ошибка Пипа: " + String(line.dropFirst(4)))
        case _ where line.hasPrefix("hi "): note("Пип на связи, прошивка \(firmware)")
        default: break
        }
        delegate?.bridge(self, line: line)
    }

    func note(_ s: String) {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        log.append("\(f.string(from: Date())) \(s)")
        if log.count > 200 { log.removeFirst(log.count - 200) }
    }
}

// MARK: - CBCentralManagerDelegate

extension Bridge: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn: find()
        case .unauthorized: state = .unauthorized
        default:
            linkDown()
            state = .bluetoothOff
        }
    }

    func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        // iOS relaunched us in the background for Pip: pick up where we were
        if let p = (dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral])?.first {
            pip = p
            p.delegate = self
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? peripheral.name
        if name == "Pip" { connect(peripheral) }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        UserDefaults.standard.set(peripheral.identifier.uuidString, forKey: savedKey)
        peripheral.discoverServices([PipUUID.service])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        state = .searching
        note("не подключился: \(error?.localizedDescription ?? "?")")
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        linkDown()
        setConnecting()
        note("связь пропала")
        central.connect(peripheral, options: nil) // pending: back as soon as Pip is
    }
}

// MARK: - CBPeripheralDelegate

extension Bridge: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let service = peripheral.services?.first(where: { $0.uuid == PipUUID.service }) else {
            state = .noBridge
            lastError = "Pip не показывает сервис моста. Прошей свежую прошивку; если уже прошита — выключи и включи Bluetooth на iPhone."
            return
        }
        peripheral.discoverCharacteristics([PipUUID.rx, PipUUID.tx, PipUUID.audio, PipUUID.speak], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        linkDown()
        setConnecting()
        peripheral.discoverServices([PipUUID.service]) // Pip got new firmware: look again
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        for c in service.characteristics ?? [] {
            switch c.uuid {
            case PipUUID.rx: rx = c
            case PipUUID.speak: sp = c
            case PipUUID.tx, PipUUID.audio: peripheral.setNotifyValue(true, for: c)
            default: break
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if let error {
            lastError = "Подписка не удалась: \(error.localizedDescription)"
            return
        }
        guard characteristic.uuid == PipUUID.tx, characteristic.isNotifying, rx != nil else { return }
        state = .ready
        lastError = nil
        note("мост открыт")
        delegate?.bridge(self, linked: true)
        hello()
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard let value = characteristic.value else { return }
        if characteristic.uuid == PipUUID.audio {
            delegate?.bridge(self, audio: value)
        } else if characteristic.uuid == PipUUID.tx {
            received(value)
        }
    }

    func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        pump()
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error { note("запись не прошла: \(error.localizedDescription)") }
    }
}
