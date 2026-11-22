import SwiftUI

/// One of each, for the app's whole life (also when iOS wakes it in the background for Pip).
final class AppModel {
    static let shared = AppModel()
    let settings = Settings()
    let bridge = Bridge()
    lazy var assistant = Assistant(bridge: bridge, settings: settings)

    private init() {
        assert(ADPCM.selfTest(), "ADPCM.swift no longer decodes what ui/adpcm.c encodes")
    }
}

@main
struct PipBridgeApp: App {
    private let model = AppModel.shared
    @Environment(\.scenePhase) private var phase

    init() {
        _ = AppModel.shared.assistant // the bridge's delegate from the first moment, before any view
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(model.settings)
                .environmentObject(model.bridge)
                .environmentObject(model.assistant)
        }
        .onChange(of: phase) { _, now in
            if now == .active { model.bridge.wake() }
        }
    }
}
