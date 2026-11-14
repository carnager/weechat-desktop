pragma Singleton
import QtQuick
import WeechatDesktop.Core

QtObject {
    id: theme

    readonly property bool dark: AppSettings.theme === "dark"
        || (AppSettings.theme === "system" && Qt.styleHints.colorScheme !== Qt.ColorScheme.Light)

    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    function mix(a, b, t) { return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1) }

    // Surfaces
    readonly property color window: dark ? "#121418" : "#eceef3"
    readonly property color sidebar: dark ? "#14161b" : "#eceef3"
    readonly property color chat: dark ? "#1a1c22" : "#ffffff"
    readonly property color surface: dark ? "#22252d" : "#ffffff"
    readonly property color raised: dark ? "#262a33" : "#f1f3f7"
    readonly property color hover: dark ? alpha("#ffffff", 0.045) : alpha("#0b1220", 0.045)
    readonly property color pressed: dark ? alpha("#ffffff", 0.08) : alpha("#0b1220", 0.08)
    readonly property color border: dark ? "#2a2e37" : "#dde1e8"
    readonly property color borderStrong: dark ? "#363b46" : "#c9ced8"
    readonly property color field: dark ? "#1e2128" : "#f5f6f9"
    readonly property color shadow: dark ? "#80000000" : "#30000000"

    // Text
    readonly property color text: dark ? "#e6e8ee" : "#1d2129"
    readonly property color muted: dark ? "#8f96a5" : "#626b7c"
    readonly property color faint: dark ? "#5d6472" : "#9aa2b1"

    // Accents
    readonly property color accentBase: AppSettings.accent
    readonly property color accent: dark ? accentBase : Qt.darker(accentBase, 1.3)
    readonly property color onAccent: dark ? "#14161b" : "#ffffff"
    readonly property color accentSoft: alpha(accent, dark ? 0.16 : 0.12)
    readonly property color mentionBg: alpha(accent, dark ? 0.10 : 0.08)
    readonly property color link: dark ? Qt.lighter(accentBase, 1.12) : Qt.darker(accentBase, 1.45)
    readonly property color danger: dark ? "#ef6b78" : "#d1344a"
    readonly property color ok: dark ? "#5fd4a6" : "#1f9d6b"
    readonly property color warn: dark ? "#f2b45a" : "#b7791f"
    readonly property color unreadMarker: danger

    // Type
    readonly property int fontSize: AppSettings.fontSize
    readonly property int small: Math.round(fontSize * 0.86)
    readonly property int tiny: Math.round(fontSize * 0.78)
    readonly property int large: Math.round(fontSize * 1.15)
    readonly property int title: Math.round(fontSize * 1.45)
    readonly property string monoFamily: "monospace"

    // Geometry
    readonly property int radius: 8
    readonly property int radiusLarge: 14
    readonly property int gap: 8

    readonly property var accents: ["#8ea2ff", "#b18cff", "#ff8fb1", "#ff9e64", "#f2c94c", "#5fd4a6", "#4fc3f7", "#9aa5b1"]

    readonly property int animFast: 110
    readonly property int anim: 180
}
