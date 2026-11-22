import Foundation
import Security

/// What the app keeps: the Claude key in the Keychain, the rest in UserDefaults.
final class Settings: ObservableObject {
    @Published var apiKey: String {
        didSet { Keychain.set(apiKey, for: "claude.apiKey") }
    }
    @Published var model: String {
        didSet { UserDefaults.standard.set(model, forKey: "claude.model") }
    }
    @Published var webSearch: Bool {
        didSet { UserDefaults.standard.set(webSearch, forKey: "claude.webSearch") }
    }
    @Published var language: String {
        didSet { UserDefaults.standard.set(language, forKey: "speech.language") }
    }
    @Published var memoryMinutes: Int {
        didSet { UserDefaults.standard.set(memoryMinutes, forKey: "claude.memoryMinutes") }
    }

    struct Language: Identifiable {
        let id: String
        let name: String
    }

    static let languages: [Language] = [Language(id: "ru-RU", name: "Русский"), Language(id: "en-US", name: "English")]

    init() {
        let d = UserDefaults.standard
        apiKey = Keychain.get("claude.apiKey") ?? ""
        model = d.string(forKey: "claude.model") ?? "claude-sonnet-5-5"
        webSearch = d.object(forKey: "claude.webSearch") as? Bool ?? true
        language = d.string(forKey: "speech.language") ?? "ru-RU"
        memoryMinutes = d.object(forKey: "claude.memoryMinutes") as? Int ?? 5
    }

    var claude: Claude { Claude(apiKey: apiKey.trimmingCharacters(in: .whitespacesAndNewlines), model: model, webSearch: webSearch) }
}

enum Keychain {
    static func set(_ value: String, for key: String) {
        let base: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrAccount as String: key]
        SecItemDelete(base as CFDictionary)
        guard !value.isEmpty else { return }
        var add = base
        add[kSecValueData as String] = Data(value.utf8)
        add[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlock // the app works with the phone locked
        SecItemAdd(add as CFDictionary, nil)
    }

    static func get(_ key: String) -> String? {
        let q: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrAccount as String: key,
                                kSecReturnData as String: true, kSecMatchLimit as String: kSecMatchLimitOne]
        var out: AnyObject?
        guard SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess, let data = out as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }
}
