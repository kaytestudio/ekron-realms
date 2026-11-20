//
//  TouchControlsView.swift
//  realms iOS
//

import UIKit
import CoreGraphics
import QuartzCore

enum SandboxHandTool: Int, CaseIterable {
    case handEmpty = 0
    case spawnGun = 1
    case gravityGun = 2
    case deleteTool = 3
    case droneRemote = 4
}

final class SandboxToolState {
    private(set) var tool: SandboxHandTool = .handEmpty
    private(set) var holding: Bool = false

    func select(_ next: SandboxHandTool) {
        tool = next
        holding = false
    }

    func confirmHolding(_ value: Bool) {
        holding = (tool == .gravityGun) && value
    }

    var viewmodelVisible: Bool {
        tool == .spawnGun || tool == .gravityGun
    }
    var spawnVisible: Bool { tool == .spawnGun }
    var gravityVisible: Bool { tool == .gravityGun }
    var rotateHeldVisible: Bool { gravityVisible && holding }
    var deleteVisible: Bool { tool == .spawnGun }
    var grabCaption: String { holding ? "Бросить" : "Захват" }
}

enum SandboxToolWheelMath {
    static let tools: [SandboxHandTool] = [.gravityGun, .droneRemote, .handEmpty]
    static let names: [String] = ["Грави-пушка", "Пульт FPV", "Пустые руки"]

    static func sector(dx: CGFloat, dy: CGFloat, deadZone: CGFloat) -> Int? {
        guard dx.isFinite, dy.isFinite, hypot(dx, dy) > deadZone else { return nil }
        let degrees = atan2(Double(dy), Double(dx)) * 180.0 / .pi
        let shifted = (degrees + 150.0 + 360.0).truncatingRemainder(dividingBy: 360.0)
        return min(2, max(0, Int(shifted / 120.0)))
    }
}

/// Container view that passes touches through empty areas to sibling controls underneath.
class PassthroughContainerView: UIView {
    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        let hit = super.hitTest(point, with: event)
        return hit === self ? nil : hit
    }
}

/// Independent single-touch virtual joystick matching Android's StickView.
final class StickControlView: UIView {
    var onMove: ((Float, Float) -> Void)?

    private weak var activeTouch: UITouch?
    private var normX: CGFloat = 0
    private var normY: CGFloat = 0

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
        isOpaque = false
        isMultipleTouchEnabled = true
        accessibilityLabel = "Джойстик движения"
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func reset() {
        activeTouch = nil
        normX = 0
        normY = 0
        onMove?(0, 0)
        setNeedsDisplay()
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard activeTouch == nil, let touch = touches.first else { return }
        activeTouch = touch
        updateStick(with: touch)
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        updateStick(with: touch)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        reset()
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        reset()
    }

    private func updateStick(with touch: UITouch) {
        let p = touch.location(in: self)
        let radius = min(bounds.width, bounds.height) * 0.32
        guard radius > 1 else { return }
        var dx = (p.x - bounds.midX) / radius
        var dy = (p.y - bounds.midY) / radius
        let len = hypot(dx, dy)
        if len > 1 {
            dx /= len
            dy /= len
        }
        let mag = min(1.0, len)
        let scale: CGFloat = mag < 0.12 ? 0.0 : ((mag - 0.12) / 0.88) / mag
        normX = dx
        normY = dy
        onMove?(Float(dx * scale), Float(-dy * scale))
        setNeedsDisplay()
    }

    override func draw(_ rect: CGRect) {
        guard let ctx = UIGraphicsGetCurrentContext() else { return }
        let cx = bounds.midX
        let cy = bounds.midY
        let r = min(bounds.width, bounds.height) * 0.48
        guard r > 2 else { return }

        let baseRect = CGRect(x: cx - r, y: cy - r, width: r * 2, height: r * 2)
        ctx.setFillColor(UIColor(red: 0.25, green: 0.25, blue: 0.23, alpha: 0.60).cgColor)
        ctx.fillEllipse(in: baseRect)

        ctx.setStrokeColor(UIColor(white: 0.93, alpha: 0.73).cgColor)
        ctx.setLineWidth(2.0)
        ctx.strokeEllipse(in: baseRect)

        ctx.setFillColor(UIColor(white: 0.93, alpha: 0.73).cgColor)
        for angle in [0.0, CGFloat.pi * 0.5, CGFloat.pi, CGFloat.pi * 1.5] {
            let ax = cx + cos(angle) * r * 0.79
            let ay = cy + sin(angle) * r * 0.79
            let ux = cos(angle)
            let uy = sin(angle)
            let s = r * 0.105
            ctx.beginPath()
            ctx.move(to: CGPoint(x: ax + ux * s, y: ay + uy * s))
            ctx.addLine(to: CGPoint(x: ax - ux * s - uy * s, y: ay - uy * s + ux * s))
            ctx.addLine(to: CGPoint(x: ax - ux * s + uy * s, y: ay - uy * s - ux * s))
            ctx.closePath()
            ctx.fillPath()
        }

        let knobX = cx + normX * r * 0.48
        let knobY = cy + normY * r * 0.48
        let knobR = r * 0.43
        let knobRect = CGRect(x: knobX - knobR, y: knobY - knobR, width: knobR * 2, height: knobR * 2)
        ctx.setFillColor(UIColor(white: 1.0, alpha: 0.60).cgColor)
        ctx.fillEllipse(in: knobRect)
        ctx.setStrokeColor(UIColor.white.cgColor)
        ctx.setLineWidth(2.0)
        ctx.strokeEllipse(in: knobRect)
    }
}

/// Touch-drag camera look surface with independent UITouch ownership.
final class LookControlView: UIView {
    var startFraction: CGFloat
    var onLook: ((Float, Float) -> Void)?

    private weak var activeTouch: UITouch?
    private var lastPoint: CGPoint = .zero

    init(startFraction: CGFloat = 0.42) {
        self.startFraction = startFraction
        super.init(frame: .zero)
        backgroundColor = .clear
        isOpaque = false
        isMultipleTouchEnabled = true
        accessibilityLabel = "Обзор: проведите пальцем справа"
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func reset() {
        activeTouch = nil
    }

    override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
        guard bounds.contains(point) else { return false }
        return point.x >= bounds.width * startFraction
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard activeTouch == nil, let touch = touches.first else { return }
        let p = touch.location(in: self)
        guard p.x >= bounds.width * startFraction else { return }
        activeTouch = touch
        lastPoint = p
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        let p = touch.location(in: self)
        let rawScale = window?.windowScene?.screen.scale ?? traitCollection.displayScale
        let scale = rawScale > 0 ? rawScale : 2.0
        // Scale points to Android-equivalent pixel delta sensitivity.
        let factor = max(1.0, min(scale, 2.5))
        let dx = Float((p.x - lastPoint.x) * factor)
        let dy = Float((p.y - lastPoint.y) * factor)
        lastPoint = p
        onLook?(dx, dy)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        if let touch = activeTouch, touches.contains(touch) {
            activeTouch = nil
        }
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        if let touch = activeTouch, touches.contains(touch) {
            activeTouch = nil
        }
    }
}

/// Vector-drawn circular/rounded HUD button matching Android's HudButton.
final class HudActionButton: UIControl {
    var caption: String {
        didSet {
            accessibilityLabel = caption
            setNeedsDisplay()
        }
    }
    var symbol: String {
        didSet { setNeedsDisplay() }
    }
    var visualScale: CGFloat = 1.0 {
        didSet { setNeedsDisplay() }
    }
    var danger: Bool = false {
        didSet { setNeedsDisplay() }
    }
    var fireOnPress: Bool = false
    var onTap: (() -> Void)?

    override var isSelected: Bool {
        didSet { setNeedsDisplay() }
    }
    override var isHighlighted: Bool {
        didSet { setNeedsDisplay() }
    }

    init(caption: String, symbol: String) {
        self.caption = caption
        self.symbol = symbol
        super.init(frame: .zero)
        backgroundColor = .clear
        isOpaque = false
        accessibilityLabel = caption
        addTarget(self, action: #selector(handleTouchUpInside), for: .touchUpInside)
        addTarget(self, action: #selector(handleTouchDown), for: .touchDown)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    @objc private func handleTouchDown() {
        if fireOnPress {
            onTap?()
        }
    }

    @objc private func handleTouchUpInside() {
        if !fireOnPress {
            onTap?()
        }
    }

    override func draw(_ rect: CGRect) {
        guard let ctx = UIGraphicsGetCurrentContext() else { return }
        let size = min(bounds.width, bounds.height) * visualScale
        guard size > 4 else { return }
        let cx = bounds.midX
        let cy = bounds.midY
        let box = CGRect(x: cx - size * 0.48, y: cy - size * 0.48, width: size * 0.96, height: size * 0.96)

        let bgColor: UIColor
        if isSelected || isHighlighted {
            bgColor = UIColor(red: 0.32, green: 0.40, blue: 0.35, alpha: 0.68)
        } else if danger {
            bgColor = UIColor(red: 0.62, green: 0.16, blue: 0.16, alpha: 0.65)
        } else {
            bgColor = UIColor(red: 0.19, green: 0.20, blue: 0.18, alpha: 0.48)
        }

        ctx.setFillColor(bgColor.cgColor)
        if symbol == "pause" {
            let path = UIBezierPath(roundedRect: box, cornerRadius: size * 0.12)
            ctx.addPath(path.cgPath)
            ctx.fillPath()
        } else {
            ctx.fillEllipse(in: box)
            ctx.setStrokeColor(UIColor(white: 1.0, alpha: 0.73).cgColor)
            ctx.setLineWidth(max(1.2, size * 0.022))
            ctx.strokeEllipse(in: box)
        }

        ctx.saveGState()
        ctx.translateBy(x: cx - size * 0.5, y: cy - size * 0.5)
        ctx.scaleBy(x: size / 100.0, y: size / 100.0)
        ctx.setStrokeColor(UIColor.white.cgColor)
        ctx.setFillColor(UIColor.white.cgColor)
        ctx.setLineWidth(6.0)
        ctx.setLineCap(.round)
        ctx.setLineJoin(.round)

        func line(_ x1: CGFloat, _ y1: CGFloat, _ x2: CGFloat, _ y2: CGFloat) {
            ctx.beginPath()
            ctx.move(to: CGPoint(x: x1, y: y1))
            ctx.addLine(to: CGPoint(x: x2, y: y2))
            ctx.strokePath()
        }

        switch symbol {
        case "cube":
            ctx.stroke(CGRect(x: 31, y: 28, width: 31, height: 32))
            line(31, 28, 42, 18)
            line(42, 18, 73, 18)
            line(73, 18, 73, 50)
            line(73, 50, 62, 60)
            line(62, 28, 73, 18)
        case "left", "right", "release":
            let start: CGFloat = symbol == "left" ? 70 : 30
            let end: CGFloat = 100 - start
            line(start, 40, end, 40)
            line(end, 40, 50, 22)
            line(end, 40, 50, 58)
        case "rotate":
            ctx.beginPath()
            ctx.addArc(center: CGPoint(x: 50, y: 40), radius: 22, startAngle: 40 * .pi / 180, endAngle: 320 * .pi / 180, clockwise: false)
            ctx.strokePath()
            line(70, 24, 70, 40)
            line(70, 40, 55, 35)
        case "grab":
            ctx.beginPath()
            ctx.addArc(center: CGPoint(x: 50, y: 40), radius: 20, startAngle: 0, endAngle: .pi, clockwise: false)
            ctx.strokePath()
            line(30, 40, 30, 20)
            line(70, 40, 70, 20)
        case "pause":
            ctx.fill(CGRect(x: 29, y: 25, width: 13, height: 50))
            ctx.fill(CGRect(x: 58, y: 25, width: 13, height: 50))
        case "build":
            line(33, 57, 65, 25)
            line(48, 23, 69, 44)
            line(58, 17, 76, 35)
        case "freeze":
            for i in 0..<6 {
                let a = CGFloat(i) * .pi / 3.0
                let dx = cos(a)
                let dy = sin(a)
                line(50, 42, 50 + dx * 25, 42 + dy * 25)
                let x = 50 + dx * 16
                let y = 42 + dy * 16
                line(x, y, x - dx * 6 - dy * 6, y - dy * 6 + dx * 6)
                line(x, y, x - dx * 6 + dy * 6, y - dy * 6 - dx * 6)
            }
        case "home":
            line(24, 45, 50, 22)
            line(50, 22, 76, 45)
            line(31, 41, 31, 71)
            line(31, 71, 69, 71)
            line(69, 71, 69, 41)
            line(45, 71, 45, 53)
            line(45, 53, 56, 53)
            line(56, 53, 56, 71)
        case "switch":
            line(25, 34, 75, 34)
            line(75, 34, 62, 21)
            line(75, 34, 62, 47)
            line(75, 65, 25, 65)
            line(25, 65, 38, 52)
            line(25, 65, 38, 78)
        case "remove":
            line(32, 32, 68, 68)
            line(68, 32, 32, 68)
        case "jump":
            ctx.beginPath()
            ctx.move(to: CGPoint(x: 50, y: 16))
            ctx.addLine(to: CGPoint(x: 28, y: 42))
            ctx.addLine(to: CGPoint(x: 42, y: 42))
            ctx.addLine(to: CGPoint(x: 42, y: 68))
            ctx.addLine(to: CGPoint(x: 58, y: 68))
            ctx.addLine(to: CGPoint(x: 58, y: 42))
            ctx.addLine(to: CGPoint(x: 72, y: 42))
            ctx.closePath()
            ctx.fillPath()
        case "crouch":
            ctx.fillEllipse(in: CGRect(x: 48, y: 15, width: 14, height: 14))
            line(52, 34, 44, 48)
            line(44, 48, 62, 52)
            line(62, 52, 53, 65)
            line(53, 65, 70, 65)
            line(48, 37, 35, 52)
            line(35, 52, 30, 64)
        case "prone":
            ctx.fillEllipse(in: CGRect(x: 18, y: 33, width: 14, height: 14))
            line(37, 48, 60, 53)
            line(60, 53, 81, 53)
            line(39, 48, 24, 57)
            line(24, 57, 16, 53)
            line(58, 53, 77, 62)
        default:
            break
        }
        ctx.restoreGState()

        if !caption.isEmpty {
            let fontSize = size * (caption.count > 10 ? 0.105 : 0.138)
            let attrs: [NSAttributedString.Key: Any] = [
                .font: UIFont.systemFont(ofSize: max(8.5, fontSize), weight: .semibold),
                .foregroundColor: UIColor.white
            ]
            let textSize = (caption as NSString).size(withAttributes: attrs)
            let textOrigin = CGPoint(x: cx - textSize.width * 0.5, y: cy + size * 0.20)
            (caption as NSString).draw(at: textOrigin, withAttributes: attrs)
        }
    }
}

/// First-person 2D tool viewmodel & radial wheel icon art matching ToolArtView.kt.
final class ToolArtControlView: UIView {
    var tool: SandboxHandTool = .handEmpty {
        didSet {
            if oldValue != tool { setNeedsDisplay() }
        }
    }
    var moving: Bool = false
    var running: Bool = false
    private let firstPerson: Bool

    init(firstPerson: Bool = false) {
        self.firstPerson = firstPerson
        super.init(frame: .zero)
        backgroundColor = .clear
        isOpaque = false
        isUserInteractionEnabled = false
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func tickAnimation() {
        if firstPerson && running && moving {
            setNeedsDisplay()
        }
    }

    override func draw(_ rect: CGRect) {
        if firstPerson && (tool == .handEmpty || tool == .deleteTool) { return }
        guard let ctx = UIGraphicsGetCurrentContext(), bounds.width > 1, bounds.height > 1 else { return }

        ctx.saveGState()
        ctx.scaleBy(x: bounds.width / 240.0, y: bounds.height / 150.0)

        if firstPerson && running && moving {
            let phase = CACurrentMediaTime() * 8.0
            ctx.translateBy(x: CGFloat(sin(phase) * 2.5), y: CGFloat(sin(phase * 2.0) * 1.8))
        }

        let glow: UIColor
        switch tool {
        case .spawnGun:
            glow = UIColor(red: 71 / 255, green: 231 / 255, blue: 210 / 255, alpha: 1.0)
        case .gravityGun:
            glow = UIColor(red: 255 / 255, green: 173 / 255, blue: 54 / 255, alpha: 1.0)
        default:
            glow = UIColor(red: 255 / 255, green: 92 / 255, blue: 99 / 255, alpha: 1.0)
        }

        if tool == .droneRemote {
            ctx.setFillColor(UIColor(red: 0.15, green: 0.21, blue: 0.24, alpha: 1.0).cgColor)
            ctx.addPath(UIBezierPath(roundedRect: CGRect(x: 32, y: 40, width: 176, height: 90), cornerRadius: 18).cgPath)
            ctx.fillPath()
            ctx.setFillColor(UIColor(red: 0.45, green: 0.60, blue: 0.65, alpha: 1.0).cgColor)
            ctx.addPath(UIBezierPath(roundedRect: CGRect(x: 91, y: 52, width: 58, height: 38), cornerRadius: 4).cgPath)
            ctx.fillPath()
            ctx.setFillColor(UIColor.lightGray.cgColor)
            ctx.fillEllipse(in: CGRect(x: 45, y: 73, width: 38, height: 38))
            ctx.fillEllipse(in: CGRect(x: 157, y: 73, width: 38, height: 38))
            ctx.setFillColor(UIColor.black.cgColor)
            ctx.fillEllipse(in: CGRect(x: 56, y: 84, width: 16, height: 16))
            ctx.fillEllipse(in: CGRect(x: 168, y: 84, width: 16, height: 16))
            ctx.setStrokeColor(UIColor.lightGray.cgColor)
            ctx.setLineWidth(7)
            ctx.setLineCap(.round)
            ctx.beginPath()
            ctx.move(to: CGPoint(x: 48, y: 42)); ctx.addLine(to: CGPoint(x: 38, y: 9))
            ctx.move(to: CGPoint(x: 192, y: 42)); ctx.addLine(to: CGPoint(x: 202, y: 9))
            ctx.strokePath()
            ctx.restoreGState()
            return
        }

        if tool == .handEmpty {
            ctx.setStrokeColor(UIColor.lightGray.cgColor)
            ctx.setLineWidth(8)
            ctx.setLineCap(.round)
            ctx.addPath(UIBezierPath(roundedRect: CGRect(x: 92, y: 50, width: 59, height: 77), cornerRadius: 18).cgPath)
            ctx.strokePath()
            for i in 0..<4 {
                let x = CGFloat(95 + i * 17)
                let topY = CGFloat(23 + (i % 2) * 7)
                ctx.beginPath()
                ctx.move(to: CGPoint(x: x, y: 77))
                ctx.addLine(to: CGPoint(x: x, y: topY))
                ctx.strokePath()
            }
            ctx.restoreGState()
            return
        }

        func fillPoly(_ pts: [(CGFloat, CGFloat)], color: UIColor) {
            ctx.setFillColor(color.cgColor)
            ctx.beginPath()
            ctx.move(to: CGPoint(x: pts[0].0, y: pts[0].1))
            for idx in 1..<pts.count {
                ctx.addLine(to: CGPoint(x: pts[idx].0, y: pts[idx].1))
            }
            ctx.closePath()
            ctx.fillPath()
        }

        fillPoly([(156, 77), (196, 86), (224, 150), (165, 150), (137, 101)],
                 color: UIColor(red: 0.24, green: 0.28, blue: 0.31, alpha: 1.0))
        fillPoly([(148, 66), (172, 72), (161, 122), (140, 115)],
                 color: UIColor(red: 0.05, green: 0.08, blue: 0.11, alpha: 1.0))
        fillPoly([(48, 43), (131, 33), (194, 63), (180, 99), (106, 93), (47, 69)],
                 color: UIColor(red: 0.25, green: 0.34, blue: 0.40, alpha: 1.0))
        fillPoly([(68, 39), (132, 30), (182, 54), (126, 53)],
                 color: UIColor(red: 0.84, green: 0.88, blue: 0.89, alpha: 1.0))

        ctx.setFillColor(UIColor(red: 0.09, green: 0.17, blue: 0.22, alpha: 1.0).cgColor)
        ctx.addPath(UIBezierPath(roundedRect: CGRect(x: 91, y: 56, width: 57, height: 23), cornerRadius: 5).cgPath)
        ctx.fillPath()

        ctx.setFillColor(glow.cgColor)
        ctx.addPath(UIBezierPath(roundedRect: CGRect(x: 99, y: 61, width: 40, height: 6), cornerRadius: 3).cgPath)
        ctx.fillPath()

        if tool == .gravityGun {
            ctx.setStrokeColor(UIColor(red: 0.44, green: 0.51, blue: 0.54, alpha: 1.0).cgColor)
            ctx.setLineWidth(10)
            ctx.setLineCap(.round)
            ctx.beginPath()
            ctx.move(to: CGPoint(x: 80, y: 43)); ctx.addLine(to: CGPoint(x: 38, y: 20)); ctx.addLine(to: CGPoint(x: 18, y: 34))
            ctx.move(to: CGPoint(x: 73, y: 77)); ctx.addLine(to: CGPoint(x: 31, y: 90)); ctx.addLine(to: CGPoint(x: 14, y: 72))
            ctx.strokePath()

            ctx.setFillColor(glow.withAlphaComponent(0.35).cgColor)
            ctx.fillEllipse(in: CGRect(x: 5, y: 29, width: 48, height: 48))
            ctx.setFillColor(glow.cgColor)
            ctx.fillEllipse(in: CGRect(x: 21, y: 45, width: 16, height: 16))
        } else {
            ctx.setFillColor(UIColor(red: 0.14, green: 0.24, blue: 0.29, alpha: 1.0).cgColor)
            ctx.addPath(UIBezierPath(roundedRect: CGRect(x: 28, y: 40, width: 47, height: 31), cornerRadius: 8).cgPath)
            ctx.fillPath()
            ctx.setFillColor(glow.withAlphaComponent(0.35).cgColor)
            ctx.fillEllipse(in: CGRect(x: 18, y: -4, width: 44, height: 44))
            ctx.setStrokeColor(glow.cgColor)
            ctx.setLineWidth(2.5)
            ctx.stroke(CGRect(x: 27, y: 5, width: 26, height: 26))
        }

        ctx.restoreGState()
    }
}

/// Radial hold-drag-release tool wheel matching ToolWheelView.kt.
final class ToolWheelOverlayView: UIView {
    var onChoose: ((SandboxHandTool) -> Void)?

    private var selectedSector: Int?
    private var currentTool: SandboxHandTool = .handEmpty
    private let iconViews: [ToolArtControlView] = SandboxToolWheelMath.tools.map { tool in
        let v = ToolArtControlView(firstPerson: false)
        v.tool = tool
        return v
    }

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
        isOpaque = false
        isHidden = true
        isUserInteractionEnabled = false
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func beginSelection(current: SandboxHandTool) {
        currentTool = current
        selectedSector = nil
        isHidden = false
        alpha = 1.0
        setNeedsDisplay()
    }

    func updateDrag(dx: CGFloat, dy: CGFloat) {
        let next = SandboxToolWheelMath.sector(dx: dx, dy: dy, deadZone: 20.0)
        if next != selectedSector {
            selectedSector = next
            setNeedsDisplay()
        }
    }

    func finishSelection() -> Bool {
        guard !isHidden else { return false }
        let chosenIndex = selectedSector
        cancel()
        if let idx = chosenIndex {
            onChoose?(SandboxToolWheelMath.tools[idx])
            return true
        }
        return false
    }

    func cancel() {
        selectedSector = nil
        isHidden = true
        setNeedsDisplay()
    }

    override func draw(_ rect: CGRect) {
        guard let ctx = UIGraphicsGetCurrentContext() else { return }
        let radius = min(158.0, min(bounds.width * 0.32, bounds.height * 0.43))
        guard radius > 16 else { return }

        let cx = bounds.width - radius - 76.0
        let cy = bounds.midY
        let innerRadius = radius * 0.36

        for i in 0..<3 {
            let startDeg = -150.0 + Double(i) * 120.0 + 2.0
            let endDeg = startDeg + 116.0
            let startRad = CGFloat(startDeg * .pi / 180.0)
            let endRad = CGFloat(endDeg * .pi / 180.0)

            let path = UIBezierPath()
            path.addArc(withCenter: CGPoint(x: cx, y: cy), radius: radius, startAngle: startRad, endAngle: endRad, clockwise: true)
            path.addArc(withCenter: CGPoint(x: cx, y: cy), radius: innerRadius, startAngle: endRad, endAngle: startRad, clockwise: false)
            path.close()

            let active = (selectedSector == i)
            ctx.setFillColor((active
                ? UIColor(red: 0.58, green: 0.38, blue: 0.14, alpha: 0.80)
                : UIColor(red: 0.13, green: 0.19, blue: 0.24, alpha: 0.78)).cgColor)
            ctx.addPath(path.cgPath)
            ctx.fillPath()

            ctx.setStrokeColor((active
                ? UIColor(red: 1.0, green: 0.71, blue: 0.32, alpha: 1.0)
                : UIColor(red: 0.62, green: 0.86, blue: 0.91, alpha: 0.68)).cgColor)
            ctx.setLineWidth(active ? 2.5 : 1.2)
            ctx.addPath(path.cgPath)
            ctx.strokePath()

            let midRad = CGFloat((-90.0 + Double(i) * 120.0) * .pi / 180.0)
            let ix = cx + cos(midRad) * radius * 0.69
            let iy = cy + sin(midRad) * radius * 0.69
            let iw = radius * 0.60
            let ih = iw * 0.625

            ctx.saveGState()
            ctx.translateBy(x: ix - iw * 0.5, y: iy - ih * 0.5 - 8.0)
            iconViews[i].bounds = CGRect(x: 0, y: 0, width: iw, height: ih)
            iconViews[i].layer.render(in: ctx)
            ctx.restoreGState()

            let title = SandboxToolWheelMath.names[i]
            let attrs: [NSAttributedString.Key: Any] = [
                .font: UIFont.systemFont(ofSize: min(12.0, radius * 0.085), weight: .semibold),
                .foregroundColor: UIColor.white
            ]
            let ts = (title as NSString).size(withAttributes: attrs)
            (title as NSString).draw(at: CGPoint(x: ix - ts.width * 0.5, y: iy + ih * 0.5 - 4.0), withAttributes: attrs)

            if SandboxToolWheelMath.tools[i] == currentTool {
                ctx.setFillColor(UIColor.white.cgColor)
                ctx.fillEllipse(in: CGRect(x: ix - 2.5, y: iy + ih * 0.5 + 12.0, width: 5.0, height: 5.0))
            }
        }

        let centerTitle = selectedSector.map { SandboxToolWheelMath.names[$0] } ?? "Выбор"
        let centerSub = selectedSector == nil ? "Проведите пальцем" : "Отпустите"
        let titleAttrs: [NSAttributedString.Key: Any] = [
            .font: UIFont.systemFont(ofSize: min(11.5, radius * 0.08), weight: .bold),
            .foregroundColor: UIColor.white
        ]
        let subAttrs: [NSAttributedString.Key: Any] = [
            .font: UIFont.systemFont(ofSize: min(9.5, radius * 0.065), weight: .regular),
            .foregroundColor: UIColor(white: 0.9, alpha: 0.9)
        ]
        let tSize = (centerTitle as NSString).size(withAttributes: titleAttrs)
        let sSize = (centerSub as NSString).size(withAttributes: subAttrs)
        (centerTitle as NSString).draw(at: CGPoint(x: cx - tSize.width * 0.5, y: cy - tSize.height), withAttributes: titleAttrs)
        (centerSub as NSString).draw(at: CGPoint(x: cx - sSize.width * 0.5, y: cy + 3.0), withAttributes: subAttrs)
    }
}

/// Switch-weapon button that supports hold-drag-release onto ToolWheelOverlayView AND tap fallback.
final class ToolSwitchButton: UIControl {
    var onQuickTap: (() -> Void)?
    var onWheelBegin: (() -> Void)?
    var onWheelDrag: ((CGFloat, CGFloat) -> Void)?
    var onWheelEnd: (() -> Bool)?
    var onWheelCancel: (() -> Void)?

    private let iconButton = HudActionButton(caption: "", symbol: "switch")
    private weak var activeTouch: UITouch?
    private var startPoint: CGPoint = .zero
    private var draggedOut: Bool = false

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
        iconButton.visualScale = 0.73
        iconButton.isUserInteractionEnabled = false
        addSubview(iconButton)
        accessibilityLabel = "Выбор инструмента: удерживайте, проведите и отпустите"
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        iconButton.frame = bounds
    }

    override func accessibilityActivate() -> Bool {
        guard let onQuickTap = onQuickTap else { return false }
        onQuickTap()
        return true
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard activeTouch == nil, let touch = touches.first else { return }
        activeTouch = touch
        startPoint = touch.location(in: self)
        draggedOut = false
        iconButton.isHighlighted = true
        onWheelBegin?()
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        let p = touch.location(in: self)
        let dx = p.x - startPoint.x
        let dy = p.y - startPoint.y
        if hypot(dx, dy) > 20 {
            draggedOut = true
        }
        onWheelDrag?(dx, dy)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        let point = touch.location(in: self)
        let dx = point.x - startPoint.x
        let dy = point.y - startPoint.y
        if hypot(dx, dy) > 20 { draggedOut = true }
        // UIKit can deliver a final position without a preceding move event.
        onWheelDrag?(dx, dy)
        activeTouch = nil
        iconButton.isHighlighted = false
        let selectedViaWheel = onWheelEnd?() ?? false
        if !selectedViaWheel && !draggedOut {
            onQuickTap?()
        }
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        activeTouch = nil
        iconButton.isHighlighted = false
        onWheelCancel?()
    }
}

/// Mode 2 FPV drone flight overlay (left throttle/yaw, right pitch/roll, camera look, vignette).
final class FpvFlightOverlayView: UIView {
    var onDroneInput: ((Float, Float, Float, Float) -> Void)?
    var onLook: ((Float, Float) -> Void)?
    var onToggleView: (() -> Void)?
    var onExitFpv: (() -> Void)?

    private let cameraLook = LookControlView(startFraction: 0.0)
    private let leftStick = StickControlView()
    private let rightStick = StickControlView()
    private let viewSwitchButton = HudActionButton(caption: "Смена вида", symbol: "switch")
    private let exitButton = HudActionButton(caption: "", symbol: "remove")

    private var throttle: Float = 0
    private var yaw: Float = 0
    private var pitch: Float = 0
    private var roll: Float = 0

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
        isOpaque = false
        isMultipleTouchEnabled = true

        cameraLook.onLook = { [weak self] dx, dy in
            self?.onLook?(dx, dy)
        }
        addSubview(cameraLook)

        leftStick.accessibilityLabel = "FPV левый джойстик: высота и рыскание"
        leftStick.onMove = { [weak self] x, y in
            guard let self = self else { return }
            self.yaw = x
            self.throttle = y
            self.emit()
        }
        addSubview(leftStick)

        rightStick.accessibilityLabel = "FPV правый джойстик: продольный и боковой наклон"
        rightStick.onMove = { [weak self] x, y in
            guard let self = self else { return }
            self.roll = x
            self.pitch = y
            self.emit()
        }
        addSubview(rightStick)

        viewSwitchButton.onTap = { [weak self] in
            self?.onToggleView?()
        }
        addSubview(viewSwitchButton)

        exitButton.danger = true
        exitButton.accessibilityLabel = "Выйти из FPV"
        exitButton.onTap = { [weak self] in
            self?.reset()
            self?.onExitFpv?()
        }
        addSubview(exitButton)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func emit() {
        onDroneInput?(throttle, yaw, pitch, roll)
    }

    func reset() {
        cameraLook.reset()
        leftStick.reset()
        rightStick.reset()
        throttle = 0
        yaw = 0
        pitch = 0
        roll = 0
        emit()
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        cameraLook.frame = bounds
        let safe = safeAreaInsets
        let stickSize = min(170.0, max(124.0, bounds.height * 0.42))
        leftStick.frame = CGRect(x: max(18.0, safe.left + 10.0),
                                 y: bounds.height - stickSize - max(18.0, safe.bottom + 8.0),
                                 width: stickSize,
                                 height: stickSize)
        rightStick.frame = CGRect(x: bounds.width - stickSize - max(18.0, safe.right + 10.0),
                                  y: bounds.height - stickSize - max(18.0, safe.bottom + 8.0),
                                  width: stickSize,
                                  height: stickSize)

        let topY = max(8.0, safe.top + 6.0)
        let rightEdge = bounds.width - max(8.0, safe.right + 8.0)
        exitButton.frame = CGRect(x: rightEdge - 60.0, y: topY, width: 60.0, height: 60.0)
        viewSwitchButton.frame = CGRect(x: rightEdge - 60.0 - 8.0 - 84.0, y: topY, width: 84.0, height: 64.0)
    }

    override func draw(_ rect: CGRect) {
        guard let ctx = UIGraphicsGetCurrentContext(), bounds.width > 1, bounds.height > 1 else { return }
        let colors = [
            UIColor.clear.cgColor,
            UIColor(red: 0.02, green: 0.04, blue: 0.06, alpha: 0.58).cgColor
        ] as CFArray
        let locations: [CGFloat] = [0.35, 1.0]
        if let gradient = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors, locations: locations) {
            let center = CGPoint(x: bounds.midX, y: bounds.midY)
            let radius = max(bounds.width, bounds.height) * 0.65
            ctx.drawRadialGradient(gradient,
                                   startCenter: center,
                                   startRadius: 0,
                                   endCenter: center,
                                   endRadius: radius,
                                   options: .drawsAfterEndLocation)
        }
    }
}
