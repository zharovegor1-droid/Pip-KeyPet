import Foundation

/// The Messages API, plainly: one request, the text blocks of the answer.
struct Claude {
    struct Turn: Codable {
        let role: String // "user" or "assistant"
        let content: String
    }

    enum Failure: LocalizedError {
        case noKey
        case api(Int, String)
        case empty

        var errorDescription: String? {
            switch self {
            case .noKey: return "Нет ключа Claude API: добавь его в Pip Bridge"
            case let .api(code, message): return "Claude: \(message) (\(code))"
            case .empty: return "Claude ничего не ответил"
            }
        }
    }

    struct Model: Identifiable {
        let id: String
        let name: String
    }

    static let models: [Model] = [
        Model(id: "claude-haiku-5-5", name: "Haiku 5.5 — быстрый"),
        Model(id: "claude-sonnet-5-5", name: "Sonnet 5.5 — умнее"),
        Model(id: "claude-opus-5-5", name: "Opus 5.5 — самый умный, медленнее"),
    ]

    var apiKey: String
    var model: String
    var webSearch: Bool

    /// Short, plain, for a round 466-pixel screen.
    static func system(language: String) -> String {
        let f = DateFormatter()
        f.locale = Locale(identifier: "ru_RU")
        f.dateFormat = "d MMMM yyyy, EEEE, HH:mm"
        return """
        Ты — Пип, маленький помощник на круглом экране брелока. Вопросы приходят голосом через распознавание речи, \
        поэтому в них бывают ошибки: угадывай смысл, а если вопрос совсем непонятен — коротко переспроси.
        Отвечай на языке вопроса (обычно по-русски), коротко: 1–3 предложения, не длиннее 350 символов. \
        Только простой текст: без markdown, списков, таблиц, ссылок и эмодзи. Числа и единицы пиши как в обычной речи.
        Сейчас \(f.string(from: Date())), часовой пояс \(TimeZone.current.identifier). Распознавание: \(language).
        """
    }

    func ask(_ turns: [Turn], system: String) async throws -> String {
        guard !apiKey.isEmpty else { throw Failure.noKey }
        var req = URLRequest(url: URL(string: "https://api.anthropic.com/v1/messages")!)
        req.httpMethod = "POST"
        req.timeoutInterval = 40
        req.setValue(apiKey, forHTTPHeaderField: "x-api-key")
        req.setValue("2023-06-01", forHTTPHeaderField: "anthropic-version")
        req.setValue("application/json", forHTTPHeaderField: "content-type")
        var body: [String: Any] = [
            "model": model,
            "max_tokens": 600,
            "system": system,
            "messages": turns.map { ["role": $0.role, "content": $0.content] },
        ]
        if webSearch {
            body["tools"] = [["type": "web_search_20250305", "name": "web_search", "max_uses": 2]]
        }
        req.httpBody = try JSONSerialization.data(withJSONObject: body)
        let (data, response) = try await URLSession.shared.data(for: req)
        let code = (response as? HTTPURLResponse)?.statusCode ?? 0
        let json = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
        guard code == 200 else {
            let message = ((json?["error"] as? [String: Any])?["message"] as? String) ?? "HTTP \(code)"
            throw Failure.api(code, message)
        }
        let blocks = json?["content"] as? [[String: Any]] ?? []
        let text = blocks.filter { ($0["type"] as? String) == "text" }.compactMap { $0["text"] as? String }.joined()
        let answer = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if answer.isEmpty { throw Failure.empty }
        return answer
    }
}
