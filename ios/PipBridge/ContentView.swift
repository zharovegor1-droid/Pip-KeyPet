import SwiftUI

struct ContentView: View {
    var body: some View {
        TabView {
            StatusView().tabItem { Label("Pip", systemImage: "circle.circle") }
            AssistantView().tabItem { Label("Ассистент", systemImage: "waveform") }
            WifiView().tabItem { Label("Wi-Fi", systemImage: "wifi") }
            SettingsView().tabItem { Label("Настройки", systemImage: "gearshape") }
        }
    }
}

// MARK: - Status

struct StatusView: View {
    @EnvironmentObject var bridge: Bridge

    var body: some View {
        NavigationStack {
            List {
                Section {
                    HStack {
                        Circle().fill(bridge.state == .ready ? Color.green : Color.orange).frame(width: 10, height: 10)
                        Text(bridge.state.title)
                        Spacer()
                        if !bridge.firmware.isEmpty { Text(bridge.firmware).foregroundStyle(.secondary).font(.footnote) }
                    }
                    if bridge.state != .ready {
                        Text("Pip должен быть подключён к iPhone: Настройки → Bluetooth → Pip (он там как клавиатура). Приложение пользуется той же связью.")
                            .font(.footnote).foregroundStyle(.secondary)
                    }
                    if let e = bridge.lastError {
                        Text(e).font(.footnote).foregroundStyle(.red)
                    }
                }
                if let s = bridge.status {
                    Section("Состояние") {
                        row("Батарея", s.bat < 0 ? "—" : "\(s.bat)%" + (s.chg ? " · заряжается" : ""))
                        row("Настроение", s.mood)
                        row("Сигнал", "\(s.rssi) dBm")
                        row("Работает", uptime(s.up))
                        row("Память", "\(s.ram / 1024) КБ · PSRAM \(String(format: "%.1f", Double(s.psram) / 1_048_576)) МБ")
                        row("Сетей Wi-Fi", "\(s.wifi)")
                        row("MTU", "\(s.mtu)")
                    }
                }
                Section("Журнал связи") {
                    ForEach(bridge.log.suffix(30).reversed(), id: \.self) { line in
                        Text(line).font(.caption.monospaced()).foregroundStyle(.secondary)
                    }
                }
            }
            .navigationTitle("Pip")
            .refreshable { bridge.refresh() }
            .toolbar {
                Button { bridge.refresh() } label: { Image(systemName: "arrow.clockwise") }
                    .disabled(bridge.state != .ready)
            }
        }
    }

    private func row(_ k: String, _ v: String) -> some View {
        HStack { Text(k); Spacer(); Text(v).foregroundStyle(.secondary) }
    }

    private func uptime(_ s: Int) -> String {
        s >= 3600 ? "\(s / 3600) ч \(s % 3600 / 60) мин" : "\(s / 60) мин"
    }
}

// MARK: - Assistant

struct AssistantView: View {
    @EnvironmentObject var assistant: Assistant
    @EnvironmentObject var settings: Settings
    @State private var typed = ""

    var body: some View {
        NavigationStack {
            List {
                if !assistant.speechAllowed {
                    Section {
                        Button("Разрешить распознавание речи") { assistant.requestSpeech() }
                        Text("Без него голос с Пипа не превратится в текст.").font(.footnote).foregroundStyle(.secondary)
                    }
                }
                if settings.apiKey.isEmpty {
                    Section {
                        Text("Добавь ключ Claude API во вкладке «Настройки».").foregroundStyle(.orange)
                    }
                }
                Section {
                    Text("Зажми нижнюю кнопку на лице Пипа, говори, отпусти. Ответ появится на экране; верхняя кнопка — прочитать его вслух с телефона.")
                        .font(.footnote).foregroundStyle(.secondary)
                    if !assistant.phase.isEmpty {
                        HStack {
                            ProgressView()
                            Text(assistant.phase.capitalized + "…")
                            Text(assistant.live).foregroundStyle(.secondary).lineLimit(2)
                        }
                    }
                    HStack {
                        TextField("Спросить текстом", text: $typed)
                            .onSubmit(send)
                        Button(action: send) { Image(systemName: "arrow.up.circle.fill") }
                            .disabled(typed.trimmingCharacters(in: .whitespaces).isEmpty)
                    }
                }
                Section("История") {
                    if assistant.history.isEmpty {
                        Text("Пока пусто").foregroundStyle(.secondary)
                    }
                    ForEach(assistant.history) { e in
                        VStack(alignment: .leading, spacing: 4) {
                            HStack {
                                Image(systemName: e.fromPip ? "circle.circle" : "keyboard").foregroundStyle(.secondary)
                                Text(e.question).bold()
                            }
                            Text(e.answer).foregroundStyle(e.failed ? .red : .primary)
                            Text(e.date, style: .time).font(.caption).foregroundStyle(.secondary)
                        }
                    }
                }
            }
            .navigationTitle("Ассистент")
            .onAppear { if !assistant.speechAllowed { assistant.requestSpeech() } }
        }
    }

    private func send() {
        assistant.askTyped(typed)
        typed = ""
    }
}

// MARK: - Wi-Fi

struct WifiView: View {
    @EnvironmentObject var bridge: Bridge
    @State private var ssid = ""
    @State private var password = ""

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Text("Сети, к которым Пип будет подключаться (до 8). Пока он только хранит их — пригодятся для домашнего сервера. iOS не отдаёт приложениям сохранённые пароли, поэтому пароль вводится здесь.")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                Section("На Пипе") {
                    if bridge.state != .ready {
                        Text("Нет связи с Пипом").foregroundStyle(.secondary)
                    } else if !bridge.wifiLoaded {
                        ProgressView()
                    } else if bridge.wifi.isEmpty {
                        Text("Пока ни одной").foregroundStyle(.secondary)
                    }
                    ForEach(bridge.wifi, id: \.self) { name in
                        Label(name, systemImage: "wifi")
                    }
                    .onDelete { idx in
                        for i in idx { bridge.forgetWifi(ssid: bridge.wifi[i]) }
                    }
                }
                Section("Добавить или поменять пароль") {
                    TextField("Имя сети (SSID)", text: $ssid)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    SecureField("Пароль (пусто — открытая сеть)", text: $password)
                    Button("Сохранить на Пипе") {
                        bridge.addWifi(ssid: ssid.trimmingCharacters(in: .whitespaces), password: password)
                        ssid = ""
                        password = ""
                    }
                    .disabled(bridge.state != .ready || ssid.trimmingCharacters(in: .whitespaces).isEmpty
                        || ssid.utf8.count > 32 || password.utf8.count > 64 || ssid.contains("\t") || password.contains("\t"))
                }
                if let e = bridge.lastError {
                    Section { Text(e).foregroundStyle(.red).font(.footnote) }
                }
            }
            .navigationTitle("Wi-Fi")
            .onAppear { if bridge.state == .ready { bridge.send("wifi?") } }
        }
    }
}

// MARK: - Settings

struct SettingsView: View {
    @EnvironmentObject var settings: Settings
    @EnvironmentObject var assistant: Assistant
    @State private var key = ""
    @State private var saved = false

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    SecureField(settings.apiKey.isEmpty ? "sk-ant-…" : "сохранён — вставь новый, чтобы заменить", text: $key)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    Button(saved ? "Сохранено" : "Сохранить ключ") {
                        settings.apiKey = key.trimmingCharacters(in: .whitespacesAndNewlines)
                        key = ""
                        saved = true
                    }
                    .disabled(key.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                } header: {
                    Text("Ключ Claude API")
                } footer: {
                    Text("console.anthropic.com → API Keys. Хранится в связке ключей iPhone.")
                }
                Section("Claude") {
                    Picker("Модель", selection: $settings.model) {
                        ForEach(Claude.models) { m in Text(m.name).tag(m.id) }
                    }
                    Toggle("Поиск в интернете", isOn: $settings.webSearch)
                    Stepper("Помнит разговор \(settings.memoryMinutes) мин", value: $settings.memoryMinutes, in: 0 ... 30)
                }
                Section {
                    Picker("Язык речи", selection: $settings.language) {
                        ForEach(Settings.languages) { l in Text(l.name).tag(l.id) }
                    }
                    .onChange(of: settings.language) { _, _ in assistant.languageChanged() }
                } header: {
                    Text("Распознавание")
                } footer: {
                    Text("Речь распознаётся на самом iPhone, если язык это поддерживает.")
                }
                Section("О приложении") {
                    LabeledContent("Протокол моста", value: "v\(Bridge.protocolVersion)")
                    Text("Без платного аккаунта разработчика приложение живёт 7 дней: потом снова запусти его из Xcode.")
                        .font(.footnote).foregroundStyle(.secondary)
                }
            }
            .navigationTitle("Настройки")
        }
    }
}
